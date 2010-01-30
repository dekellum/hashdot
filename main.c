/**************************************************************************
 * Copyright (C) 2008-2010 David Kellum
 * This file is part of Hashdot.
 *
 * Hashdot is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3, or (at your option)
 * any later version.
 *
 * Hashdot is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Hashdot. If not, see http://www.gnu.org/licenses/.
 *
 * Dynamically linking other modules to this executable is making a
 * combined work based on this executable.  Thus, the terms and
 * conditions of the GNU General Public License cover the whole
 * combination.
 *
 * As a special exception, the Hashdot copyright holder gives you
 * permission to dynamically link independent modules to this
 * executable, regardless of the license terms of these independent
 * modules, and to copy and distribute the combination under terms of
 * your choice, provided that you also meet, for each linked
 * independent module, the terms and conditions of the license of that
 * module.  An independent module is a module which is not derived
 * from or based from the source of Hashdot.  If you modify Hashdot,
 * you may extend this exception to your version, but you are not
 * obligated to do so.  If you do not wish to do so, delete this
 * exception statement from your version.
 *************************************************************************/

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <pwd.h>

#include <apr_strings.h>
#include <apr_env.h>
#include <apr_lib.h>
#include <apr_file_io.h>

#include "runtime.h"
#include "property.h"
#include "daemon.h"
#include "pidfile.h"
#include "jvm.h"
#include "libpath.h"

#ifndef __MacOS_X__
#  include <sys/prctl.h>
#endif

static apr_status_t
skip_flags( int argc,
            const char *argv[],
            int *file_offset );

static apr_status_t
set_user_prop();

static apr_status_t
set_script_props( const char *script );

static apr_status_t
set_hashdot_env();

static apr_status_t
set_process_name( int argc,
                  const char *argv[],
                  char *name );

static apr_status_t
check_hashdot_cwd( const char **script );

int main( int argc, const char *argv[] )
{
    apr_status_t rv = rt_initialize();

    char * value;
    if( ( rv == APR_SUCCESS ) &&
        ( apr_env_get( &value, "HASHDOT_DEBUG", _mp ) == APR_SUCCESS ) ) {
        _debug = 1;
        DEBUG( "DEBUG output enabled." );
    }

    int file_offset = 0;
    char * called_as = NULL;
    if( rv == APR_SUCCESS ) {
        called_as = (char *) apr_filepath_name_get( argv[0] );
        DEBUG( "Run as argv[0] = [%s]", called_as );
        if( strcmp( "hashdot", called_as ) == 0 ) {
            called_as = NULL;
            if( argc > 1 ) {
                file_offset = 1;
            }
            else {
                ERROR( "Missing required <script-file> argument.\n"
                       "#. hashdot.version = %s\n"
                       "Usage: %s <script-file>",
                       HASHDOT_VERSION, argv[0] );
                rv = 1;
            }
        }
    }

    apr_hash_t *rprops = NULL;
    if( rv == APR_SUCCESS ) {
        _props = apr_hash_make( _mp );
        rprops = apr_hash_make( _mp );
    }

    if( rv == APR_SUCCESS ) {
        rv = set_user_prop();
    }

    if( rv == APR_SUCCESS ) {
        rv = parse_profile( "default", rprops );
    }

    if( ( rv == APR_SUCCESS ) &&
        ( apr_env_get( &value, "HASHDOT_PROFILE", _mp ) == APR_SUCCESS ) ) {
        rv = parse_profile( value, rprops );
    }

    if( ( rv == APR_SUCCESS ) && ( called_as != NULL ) ) {
        rv = parse_profile( called_as, rprops );
    }

    if( ( rv == APR_SUCCESS ) && ( called_as != NULL ) ) {
        rv = skip_flags( argc, argv, &file_offset );
    }

    if( ( rv == APR_SUCCESS ) && ( file_offset > 0 ) ) {
        rv = set_script_props( argv[ file_offset ] );
    }

    if( ( rv == APR_SUCCESS ) && ( file_offset > 0 ) ) {
        rv = parse_hashdot_header( argv[ file_offset ], rprops );
    }

    // Late expand any "recursive" rprops and fold in to props
    if( rv == APR_SUCCESS ) {
        rv = expand_recursive_props( rprops );
    }

    if( rv == APR_SUCCESS ) {
        rv = exec_self( argc, argv ); //if needed
    }

    if( rv == APR_SUCCESS ) {
        rv = set_hashdot_env();
    }

    // Set process name to filename minus path of our script (if
    // present) or argv[0] (which may be symlink called_as)
    if( rv == APR_SUCCESS ) {
        char * rename = NULL;
        if( file_offset > 0 ) {
            rename = (char *) apr_filepath_name_get( argv[ file_offset ] );
        }
        else if( called_as != NULL ) {
            rename = called_as;
        }
        if( rename != NULL ) {
            rv = set_process_name(argc, argv, rename);
        }
    }

    if( rv == APR_SUCCESS ) {
        set_property_value( "hashdot.version", HASHDOT_VERSION );
    }

    // Change directory to hashdot.chdir if set, and make any script
    // path absolute.
    if( rv == APR_SUCCESS ) {
        rv = check_hashdot_cwd( (file_offset > 0) ? argv + file_offset : NULL );
    }

    if( rv == APR_SUCCESS ) {
        rv = check_daemonize();
    }

    if( rv == APR_SUCCESS ) {
        rv = lock_pid_file();
    }

    if( rv == APR_SUCCESS ) {
        // Note: java.class.path is expanded/globed/resolved here
        rv = init_jvm( argc-1, argv+1 );
    }

    if( rv == APR_SUCCESS ) {
        rv = unlock_pid_file();
    }

    if( rv > APR_OS_START_ERROR ) {
        print_error( rv, "" );
    }

    rt_shutdown();

    return rv;
}

static apr_status_t
skip_flags( int argc,
            const char *argv[],
            int *file_offset )
{
    apr_status_t rv = APR_SUCCESS;

    apr_array_header_t *ignore_flags =
        get_property_array( "hashdot.parse_flags.value_args" );

    apr_array_header_t *terminal_flags =
        get_property_array( "hashdot.parse_flags.terminal" );

    int i;
    for( i = 1; i < argc; i++ ) {
        if( argv[i][0] == '-' ) {
            if( terminal_flags != NULL ) {
                int a;
                for( a = 0; a < terminal_flags->nelts; a++ ) {
                    const char *flag = ((const char **) terminal_flags->elts )[a];
                    if( strcmp( argv[i], flag ) == 0 ) {
                        goto TERMINAL;
                    }
                }
            }
            if( ignore_flags != NULL ) {
                int a;
                for( a = 0; a < ignore_flags->nelts; a++ ) {
                    const char *flag = ((const char **) ignore_flags->elts )[a];
                    if( strcmp( argv[i], flag ) == 0 ) {
                        ++i; //skip the flag and the following arg
                        break;
                    }
                }
            }
            // otherwise just fall through and skip this flag.
        }
        else {
            DEBUG( "Skip flags to script: %s", argv[i] );
            *file_offset = i;
            break;
        }
    }
 TERMINAL:
    return rv;
}

static apr_status_t
set_user_prop()
{
    struct passwd *pentry = getpwuid( getuid() );
    return set_property_value( "hashdot.user.home", pentry->pw_dir );
}

static apr_status_t
set_script_props( const char *script )
{
    apr_status_t rv = APR_SUCCESS;

    char *absolute_script = NULL;
    apr_filepath_merge( &absolute_script, NULL, script, 0, _mp );

    rv = set_property_value( "hashdot.script", absolute_script );

    char *lpath = strrchr( absolute_script, '/' );
    char *dir = ( lpath == NULL ) ? "." :
        apr_pstrndup( _mp, absolute_script, lpath - absolute_script );

    rv = set_property_value( "hashdot.script.dir", dir );

    return rv;
}

static apr_status_t set_hashdot_env()
{
    static const char *HASHDOT_ENV_PRE = "hashdot.env.";
    int plen = strlen( HASHDOT_ENV_PRE );
    apr_status_t rv = APR_SUCCESS;

    apr_array_header_t *vals;
    const char *name = NULL;
    apr_hash_index_t *p;
    for( p = apr_hash_first( _mp, _props ); p && (rv == APR_SUCCESS);
         p = apr_hash_next( p ) ) {

        apr_hash_this( p, (const void **) &name, NULL, (void **) &vals );
        int nlen = strlen( name );
        if( ( nlen > plen ) && strncmp( HASHDOT_ENV_PRE, name, plen ) == 0 ) {
            const char *val = apr_array_pstrcat( _mp, vals, ' ' );
            rv = apr_env_set( name + plen, val, _mp );
        }
    }
    return rv;
}

static apr_status_t
set_process_name( int argc,
                  const char *argv[],
                  char *name )
{
    apr_status_t rv = apr_initialize();
    DEBUG( "Renaming Process to: %s", name );
#ifdef __MacOS_X__
    // TODO: set proc title on OSX
    DEBUG( "Process rename not currently available on OSX" );
    return rv;
#else
    if( prctl( PR_SET_NAME, name, 0, 0, 0 ) == -1 ) {
        rv = APR_FROM_OS_ERROR( errno );
    }
    return rv;
#endif
}

static apr_status_t
check_hashdot_cwd( const char **script )
{
    apr_status_t rv = APR_SUCCESS;

    char * nwd = NULL;

    const char * rel_wd = NULL;
    rv = get_property_value( "hashdot.chdir", '/', 0, &rel_wd );
    if( ( rv == APR_SUCCESS ) && ( rel_wd != NULL ) ) {
        rv = apr_filepath_merge( &nwd, NULL, rel_wd, 0, _mp );
    }

    if( ( rv == APR_SUCCESS ) && ( nwd != NULL ) ) {
        char * cwd = NULL;
        rv = apr_filepath_get( &cwd, 0, _mp );
        if( ( rv == APR_SUCCESS ) && ( strcmp( cwd, nwd ) != 0 ) ) {

            // Convert to absolute script path if provided.
            if( script != NULL ) {
                rv = get_property_value( "hashdot.script", 0, 1, script );
            }

            DEBUG( "Changing working dir to %s", nwd );
            rv = apr_filepath_set( nwd, _mp );

            if( rv != APR_SUCCESS ) {
                print_error( rv, nwd );
            }
        }
    }
    return rv;
}
