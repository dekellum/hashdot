/**************************************************************************
 * Copyright (C) 2008 David Kellum
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

#if defined(__APPLE__) && ( defined(__MACH__) || defined(__DARWIN__) ) && !defined(__MacOS_X__)
#define __MacOS_X__ 1
#endif

#ifdef __MacOS_X__
#include <mach-o/dyld.h>
#include <dlfcn.h>
#define LIB_PATH_VAR "DYLD_LIBRARY_PATH"
#define CREATE_JVM_FUNCTION_NAME "JNI_CreateJavaVM_Impl"
#else
#include <sys/prctl.h>
#define LIB_PATH_VAR "LD_LIBRARY_PATH"
#define CREATE_JVM_FUNCTION_NAME "JNI_CreateJavaVM"
#endif

#include <apr_general.h>
#include <apr_file_io.h>
#include <apr_strings.h>
#include <apr_hash.h>
#include <apr_fnmatch.h>
#include <apr_dso.h>
#include <apr_env.h>
#include <apr_lib.h>
#include <apr_signal.h>

#include <jni.h>

static apr_status_t
parse_hashdot_header( const char *fname, 
                      apr_hash_t *props,
                      apr_hash_t *rprops,
                      apr_pool_t *mp );

static apr_status_t
parse_profile( const char *pname, 
               apr_hash_t *props,
               apr_hash_t *rprops,
               apr_pool_t *mp );

static apr_status_t
parse_line( char *line, 
            apr_hash_t *props,
            apr_hash_t *rprops, 
            apr_pool_t *mp );

static apr_status_t
expand_recursive_props( apr_hash_t *props,
                        apr_hash_t *rprops, 
                        apr_pool_t *mp );

static void 
print_error( apr_status_t rv, const char * info );

static apr_status_t
glob_values( apr_pool_t *mp, 
             apr_array_header_t *values, 
             apr_array_header_t **tvalues );

static apr_status_t
compact_option_flags( apr_array_header_t **values, 
                      apr_pool_t *mp );

static apr_status_t
set_user_prop( apr_hash_t *props, 
               apr_pool_t *mp );

static apr_status_t
check_hashdot_cwd( apr_hash_t *props, 
                   const char **script,
                   apr_pool_t *mp );

           
static apr_status_t
check_daemonize( apr_hash_t *props, 
                 apr_pool_t *mp );


static apr_status_t
init_jvm( apr_pool_t *mp, 
          apr_hash_t *props, 
          int argc, 
          const char *argv[] );

static char *
property_to_option( apr_pool_t *mp,
                    const char *name,
                    apr_array_header_t *vals,
                    char separator );

typedef jint (*create_java_vm_f)(JavaVM **, JNIEnv **, JavaVMInitArgs *);

static apr_status_t 
get_create_jvm_function( apr_pool_t *mp,
                         const char *lib_name,
                         create_java_vm_f *symbol );

static char *
convert_class_name( apr_pool_t *mp,
                    const char *cname );

static apr_status_t
set_hashdot_env( apr_hash_t *props,
                 apr_pool_t *mp );

static apr_status_t
exec_self( apr_hash_t *props,
           int argc,
           const char *argv[],
           apr_pool_t *mp );

static apr_status_t
find_self_exe( const char **exe_name,
               apr_pool_t *mp );

static apr_status_t
set_process_name( int argc,
                  const char *argv[],
                  char *name );

static apr_status_t
skip_flags( apr_hash_t *props,
            int argc,
            const char *argv[],
            int *file_offset );

static apr_array_header_t *
get_property_array( apr_hash_t *props, 
                    const char *name );

static apr_status_t
get_property_value( apr_pool_t *mp,
                    apr_hash_t *props,
                    const char *name,
                    char separator,
                    int required,
                    const char **value );

static void
set_property_array( apr_hash_t *props, 
                    const char *name,
                    apr_array_header_t *vals );

static apr_status_t
set_property_value( apr_pool_t *mp,
                    apr_hash_t *props,
                    const char *name, 
                    const char * value );

static apr_status_t install_hup_handler();

static void reopen_streams( int signo );

static const char * _redirect_fname = NULL;
static int _debug = 0;

#define DEBUG(format, args...) \
    if( _debug ) { fprintf( stderr, "HASHDOT DEBUG: " ); \
                   fprintf( stderr, format , ## args); \
                   fprintf( stderr, "\n" ); \
                   fflush( stderr ); }

#define ERROR(format, args...) \
    fprintf( stderr, "HASHDOT ERROR: " ); \
    fprintf( stderr, format , ## args); \
    fprintf( stderr, "\n" );

int main( int argc, const char *argv[] ) 
{
    apr_status_t rv = apr_initialize();
    if( rv != APR_SUCCESS ) {
        return -1;
    }

    apr_pool_t *mp = NULL;
    rv = apr_pool_create( &mp, NULL );

    char * value;
    if( ( rv == APR_SUCCESS ) && 
        ( apr_env_get( &value, "HASHDOT_DEBUG", mp ) == APR_SUCCESS ) ) { 
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
                ERROR( "script-file argument required.\n"
                       "#. hashdot.version = %s\n"
                       "Usage: %s script-file", 
                       HASHDOT_VERSION, argv[0] );
                rv = 1;
            }
        }
    }

   
    apr_hash_t *props = NULL;
    apr_hash_t *rprops = NULL;
    if( rv == APR_SUCCESS ) {
        props = apr_hash_make( mp );
        rprops = apr_hash_make( mp );
    }

    if( rv == APR_SUCCESS ) {
        rv = set_user_prop( props, mp );
    }

    if( rv == APR_SUCCESS ) {
        rv = parse_profile( "default", props, rprops, mp );
    }

    if( ( rv == APR_SUCCESS ) && ( file_offset > 0 ) ) {
        set_property_value( mp, props, "hashdot.script", argv[ file_offset ] );
    }

    if( ( rv == APR_SUCCESS ) && 
        ( apr_env_get( &value, "HASHDOT_PROFILE", mp ) == APR_SUCCESS ) ) {
        rv = parse_profile( value, props, rprops, mp );
    }

    if( ( rv == APR_SUCCESS ) && ( called_as != NULL ) ) {
        rv = parse_profile( called_as, props, rprops, mp );
    }

    if( ( rv == APR_SUCCESS ) && ( called_as != NULL ) ) {
        rv = skip_flags( props, argc, argv, &file_offset );
    }

    if( ( rv == APR_SUCCESS ) && ( file_offset > 0 ) ) {
        rv = parse_hashdot_header( argv[ file_offset ], props, rprops, mp );
    }
    
    // Late expand any "recursive" rprops and fold in to props
    if( rv == APR_SUCCESS ) {
        rv = expand_recursive_props( props, rprops, mp );
    }
    
    if( rv == APR_SUCCESS ) {
        rv = exec_self( props, argc, argv, mp ); //if needed
    }

    if( rv == APR_SUCCESS ) {
        rv = set_hashdot_env( props, mp );
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
        set_property_value( mp, props, "hashdot.version", HASHDOT_VERSION );
    }

    if( rv == APR_SUCCESS ) {
        rv = check_hashdot_cwd( props, 
                                (file_offset > 0) ? argv + file_offset : NULL, 
                                mp );
    }

    if( rv == APR_SUCCESS ) {
        rv = check_daemonize( props, mp );
    }

    if( rv == APR_SUCCESS ) {
        rv = init_jvm( mp, props, argc-1, argv+1 );
    }

    if( rv > APR_OS_START_ERROR ) {
        print_error( rv, "" );
    }

    if( mp != NULL ) {
        apr_pool_destroy( mp );
    }

    apr_terminate();

    return rv;
}

static apr_status_t
set_user_prop( apr_hash_t *props, 
               apr_pool_t *mp )
{
    struct passwd *pentry = getpwuid( getuid() );
    return set_property_value( mp, props, "hashdot.user.home", pentry->pw_dir );
}

static apr_status_t
glob_values( apr_pool_t *mp, 
             apr_array_header_t *values, 
             apr_array_header_t **tvalues )
{
    apr_status_t rv = APR_SUCCESS;
    int i;
    *tvalues = apr_array_make( mp, 16, sizeof( const char* ) );

    for( i = 0; i < values->nelts; i++ ) {
        const char *val = ((const char **) values->elts )[i];
        char *lpath = strrchr( val, '/' ); 
        //FIXME: UNIX only, replace with apr_filepath_root?
        char *path = ( lpath == NULL ) ? "" : 
            apr_pstrndup( mp, val, lpath - val + 1 );

        apr_array_header_t *globs;
        rv = apr_match_glob( val, &globs, mp );
        if( rv != APR_SUCCESS ) {
            print_error( rv, val );
            rv = 2;
            break;
        }
        else if( apr_is_empty_array( globs ) ) {
            rv = APR_FNM_NOMATCH;
            ERROR( "[%d]: %s not found\n", rv, val );
            break;
        }
        int j;
        for( j = 0; j < globs->nelts; j++ ) {
            const char *g = ((const char **) globs->elts )[j];
            *( (const char **) apr_array_push( *tvalues ) ) = 
                apr_pstrcat( mp, path, g, NULL );
        }
    }
    
    return rv;
}

static apr_status_t
compact_option_flags( apr_array_header_t **values, 
                      apr_pool_t *mp )
{
    // Options with these prefixes are equal, take the last.
    static const char * PREFIXES[] = {
        "-Xms",
        "-Xmx",
        "-Xss",
        "-Xloggc:",
        "-Xshare:",
        "-Xbootclasspath:",
        "-splash:",
        NULL
    };

    apr_hash_t * occurred = apr_hash_make( mp );

    apr_array_header_t * tmp = 
        apr_array_make( mp, (*values)->nelts, sizeof( const char* ) );

    apr_status_t rv = APR_SUCCESS;
    int i;

    // Copy options to tmp in reverse order, removing earlier equal
    // prefix options.
    for( i = (*values)->nelts; --i >= 0; ) {
        const char *val = ((const char **) (*values)->elts )[i];
        
        //Options with '=' are the same matching chars prior to =
        const char * found = strchr( val, '=' );
        int equal_pos = 0;
        if( found != NULL ) equal_pos = found - val;
            
        //Otherwise any of the prefixes are the same
        if( equal_pos == 0 ) {
            int p;
            for( p = 0; PREFIXES[p] != NULL; p++ ) {
                int plen = strlen( PREFIXES[p] );
                if( ( plen < strlen( val ) ) &&
                    strncmp( PREFIXES[p], val, plen ) == 0 ) {
                    equal_pos = plen;
                    break;
                }
            }
        }

        //Otherwise only exact match is the same (only take one
        //identical option).
        if( equal_pos == 0 ) equal_pos = strlen( val );

        //Add to tmp if its the first (from the back) of its type.
        if( apr_hash_get( occurred, val, equal_pos ) == NULL ) {
            apr_hash_set( occurred, val, equal_pos, val );
            *(const char **) apr_array_push( tmp ) = val;
        }
    }
    
    // Reverse tmp back to values in original order.
    *values = apr_array_make( mp, tmp->nelts, sizeof( const char* ) );
    while( tmp->nelts > 0 ) {
        *(const char **) apr_array_push( *values ) = 
            *(const char **) apr_array_pop( tmp );
    }
    
    return rv;
}


static apr_status_t
parse_profile( const char *pname, 
               apr_hash_t *props,
               apr_hash_t *rprops,
               apr_pool_t *mp )
{
    apr_status_t rv = APR_SUCCESS;
    apr_file_t *in = NULL;
    char line[4096];
    apr_size_t length;

    char * fname = apr_psprintf( mp, "%s/%s.hdp", 
                                 HASHDOT_PROFILE_DIR, 
                                 pname );

    rv = apr_file_open( &in, fname, 
                        APR_FOPEN_READ, 
                        APR_OS_DEFAULT, mp );

    if( rv != APR_SUCCESS ) {
        ERROR( "Could not open file [%s].", fname );
        return rv;
    }
    else {
        DEBUG( "Parsing profile [%s].", fname );
    }

    while( rv == APR_SUCCESS ) {

        rv = apr_file_gets( line, sizeof( line ) - 1, in );

        if( rv == APR_EOF ) {
            rv = APR_SUCCESS;
            break;
        }
        
        if( rv != APR_SUCCESS ) break;

        length = strlen( line );

        if( ( length > 0 ) && line[0] != '#' ) {
            rv = parse_line( line, props, rprops, mp );
        }
    }
    
    apr_file_close( in );

    return rv;
}

static apr_status_t
parse_hashdot_header( const char *fname, 
                      apr_hash_t *props,
                      apr_hash_t *rprops, 
                      apr_pool_t *mp )
{
    apr_status_t rv = APR_SUCCESS;
    apr_file_t *in = NULL;
    char line[4096];
    apr_size_t length;



    DEBUG( "Parsing hashdot header from %s", fname );

    rv = apr_file_open( &in, fname, 
                        APR_FOPEN_READ, 
                        APR_OS_DEFAULT, mp );
    
    if( rv != APR_SUCCESS ) {
        ERROR( "Could not open file [%s].", fname );
        return rv;
    }

    while( rv == APR_SUCCESS ) {

        rv = apr_file_gets( line, sizeof( line ) - 1, in );

        if( rv == APR_EOF ) {
            rv = APR_SUCCESS;
            break;
        }
        
        if( rv != APR_SUCCESS ) break;

        length = strlen( line );

        // Test for end of first comment block
        if( ( length == 0 ) || line[0] != '#' ) break; 
        
        if( ( length > 2 ) && line[1] == '.' ) {
            rv = parse_line( line + 2, props, rprops, mp );
        }
    }

    apr_file_close( in );

    return rv;
}

#define ST_BEFORE_NAME   0
#define ST_NAME          1
#define ST_AFTER_NAME    2
#define ST_VALUES        3
#define ST_VALUE_TOKEN   4
#define ST_VALUE_VAR     5
#define ST_QUOTED        6
#define ST_QUOTED_VAR    7


#define IS_WS( c ) ( ( c == '\t' ) || ( c == ' ' ) || \
                     ( c == '\n' ) || ( c == '\r' ) )

#define SAFE_APPEND( buff, pbuff, src, len )        \
    if( ( pbuff - buff + len ) < sizeof( buff ) ) { \
        memcpy( pbuff, src, len ); pbuff += len; \
    } \
    else { \
        ERROR( "Buffer length exceeded: %d >= %d", \
               (int) (pbuff - buff + len), (int) sizeof( buff ) ); \
        rv = 8; \
    }

static apr_status_t
parse_line( char *line, 
            apr_hash_t *props, 
            apr_hash_t *rprops,
            apr_pool_t *mp )
{
    static char * PARSE_LINE_ERRORS[] = {
        NULL,
        "Incomplete property expression (name only)",
        "Invalid character escape",
        "Unterminated string value", 
        "Missing '}' property reference terminal",
        NULL
    };

    apr_status_t rv = APR_SUCCESS;
    
    int state = ST_BEFORE_NAME;
    char *p = line;
    char *b = line;
    char *name = NULL;

    char value[4096];
    char *voutp = value;

    apr_array_header_t *values = NULL;

    while( rv == APR_SUCCESS ) {
        switch( state ) {

        case ST_BEFORE_NAME:
            if( IS_WS( *p ) ) p++;
            else if( *p == '\0' ) goto END_LOOP;
            else {                
                b = p;
                state = ST_NAME; 
            }
            break;

        case ST_NAME:
            if( IS_WS( *p ) || ( *p == '+' ) || ( *p == '=' ) || ( *p == ':' ) ) {
                name = apr_pstrndup( mp, b, p - b );
                state = ST_AFTER_NAME;
            }
            else if( *p == '\0' ) { rv = 11; goto END_LOOP; }
            else p++;
            break;

        case ST_AFTER_NAME:
            if( IS_WS( *p ) ) p++;
            else if( *p == '=' ) {
                values = apr_array_make( mp, 16, sizeof( const char* ) );
                state = ST_VALUES;
                p++;
            }
            else if( ( *p == '+' ) && ( *(++p) == '=' ) ) {
                // Append to old value, but only if not
                // "hashdot.profile" in which case it will be appended
                // (for both += and =) below.
                if( strcmp( name, "hashdot.profile" ) != 0 ) {
                    values = get_property_array( props, name );
                }
                if( values == NULL ) {
                    values = apr_array_make( mp, 16, sizeof( const char* ) );
                }
                state = ST_VALUES;
                p++;
            }
            else if( ( *p == ':' ) && ( *(++p) == '=' ) ) {
                apr_hash_set( rprops, apr_pstrdup( mp, name ), strlen( name ) + 1, 
                              apr_pstrdup( mp, ++p ) );
                return rv;
            }
            else { rv = 11; goto END_LOOP; }
            break;

        case ST_VALUES:
            if( IS_WS( *p ) ) p++;
            else if( *p == '\0' ) goto END_LOOP;
            else if( *p == '\"' ) {
                b = ++p;
                state = ST_QUOTED;
            }
            else {
                b = p;
                state = ST_VALUE_TOKEN;
            }
            break;

        case ST_VALUE_TOKEN:
            if( IS_WS( *p ) || ( *p == '\0' ) ) {
                SAFE_APPEND( value, voutp, b, p - b );
                char *vstr = apr_pstrndup( mp, value, voutp - value );
                *(const char **) apr_array_push( values ) = vstr;
                voutp = value;
                state = ST_VALUES;
            }
            else if( ( *p == '$' ) && ( *(++p) == '{' ) ) {
                SAFE_APPEND( value, voutp, b, p - b - 1 );
                state = ST_VALUE_VAR;
                b = ++p;
            }
            else p++;
            break;

        case ST_QUOTED:
            if( *p == '\"' ) {
                SAFE_APPEND( value, voutp, b, p - b );
                char *vstr = apr_pstrndup( mp, value, voutp - value );
                *(const char **) apr_array_push( values ) = vstr;
                voutp = value;
                state = ST_VALUES;
                p++;
            }
            else if ( *p == '\\' ) {
                SAFE_APPEND( value, voutp, b, p - b );
                p++;
                if( ( voutp - value + 1 ) >= sizeof( value ) ) {
                    ERROR( "Buffer length exceeded: %d", 
                           (int) sizeof( value ) );
                    rv = 8;
                    goto END_LOOP;
                }
                switch( *p ) {
                case 'n' : *(voutp++) = '\n'; break;
                case 'r' : *(voutp++) = '\r'; break;
                case 't' : *(voutp++) = '\t'; break;
                case '\\': *(voutp++) = '\\'; break;
                case '"' : *(voutp++) = '"' ; break;
                case '$' : *(voutp++) = '$' ; break;
                default:
                    rv = 12;
                    goto END_LOOP;
                }
                b = ++p;
            }
            else if( ( *p == '$' ) && ( *(++p) == '{' ) ) {
                SAFE_APPEND( value, voutp, b, p - b - 1);
                state = ST_QUOTED_VAR;
                b = ++p;
            }
            else if( *p == '\0' ) { rv = 13; goto END_LOOP; }
            else p++;
            break;

        case ST_QUOTED_VAR: 
        case ST_VALUE_VAR:
            if( *p == '}' ) {
                char * vname = apr_pstrndup( mp, b, p - b );
                apr_array_header_t *vals = get_property_array( props, vname );
                if( !vals ) {
                    ERROR( "Unknown property ${%s}.\n", vname );
                    rv = 21;
                    goto END_LOOP;
                }
                if( vals->nelts != 1 ) {
                    if( state == ST_VALUE_VAR ) {
                        ERROR( "Non scholar property used in replacement ${%s}.",
                               vname );
                        rv = 22;
                        goto END_LOOP;
                    }
                }
                int i;
                for( i = 0; i < vals->nelts; i++ ) {
                    if( i > 0 ) *(voutp++) = ' ';
                    const char *rval = ((const char **) vals->elts )[i];
                    DEBUG( "Variable name: %s, replacement value: %s", 
                           vname, rval );
                    int rlen = strlen( rval );
                    SAFE_APPEND( value, voutp, rval, rlen );
                }
                state = ( state == ST_VALUE_VAR ) ? ST_VALUE_TOKEN : ST_QUOTED;
                b = ++p;
            }
            else if( ( *p == '\0' ) || 
                     ( ( *p == '"' ) && ( state == ST_QUOTED_VAR ) ) ) {
                rv = 14;
                goto END_LOOP;
            }
            else p++;
            break;
        }
    }

 END_LOOP:

    if( ( rv > 10 ) && ( rv < 20 ) ) {
        char *j = p;
        while( j > line ) {
            if( IS_WS( *j ) ) *j = ' ';
            else if( *j != '\0' ) break;
            j--;
        }
        ERROR( "%s [%d, %d] at: %.*s[%c]", 
               PARSE_LINE_ERRORS[ rv-10 ], rv, state, (int) (p - line), line,
               ( IS_WS( *p ) || ( *p == '\0' ) ) ? ' ' : *p );
    }
    

    if( ( rv == APR_SUCCESS ) && 
        ( name != NULL ) && ( strcmp( name, "hashdot.profile" ) == 0 ) ) {
        int i;
        for( i = 0; ( i < values->nelts ) && ( rv == APR_SUCCESS ); i++ ) {
            const char *value = ((const char **) values->elts )[i];
            rv = parse_profile( value, props, rprops, mp );
        }

        // As special case append now processed values to old values
        // (implicit append).
        apr_array_header_t *old_vals = get_property_array( props, name );
        if( old_vals != NULL ) {
            apr_array_cat( old_vals, values );
            values = old_vals;
        }
    }

    if( ( name != NULL ) && ( rv == APR_SUCCESS ) ) {
        set_property_array( props, apr_pstrdup( mp, name ), values );
        DEBUG( "Set %s = %s", name, apr_array_pstrcat( mp, values, ' ' ) );
    }

    return rv;
}


static apr_status_t
expand_recursive_props( apr_hash_t *props,
                        apr_hash_t *rprops, 
                        apr_pool_t *mp )
{
    apr_status_t rv = APR_SUCCESS;

    const char *name = NULL;
    const char *value = NULL;
    apr_hash_index_t *p;
    for( p = apr_hash_first( mp, rprops ); p; p = apr_hash_next( p ) ) {
        
        apr_hash_this( p, (const void **) &name, NULL, (void **) &value );
        
        char *line = apr_psprintf( mp, "%s=%s", name, value );
        rv = parse_line( line, props, rprops, mp );

        if( rv != APR_SUCCESS ) break;
    }

    return rv;
}


static void 
print_error( apr_status_t rv, const char * info )
{
    char errbuf[ 256 ];
    apr_strerror( rv, errbuf, sizeof(errbuf) );
    ERROR( "[%d]: %s: %s", rv, errbuf, info );
}


static apr_status_t
init_jvm( apr_pool_t *mp, 
          apr_hash_t *props, 
          int argc, 
          const char *argv[] )
{
    apr_status_t rv = APR_SUCCESS;
    JavaVMInitArgs vm_args;
    apr_array_header_t *vals;
    int opt = 0;

    const char *lib_name = NULL;
    rv = get_property_value( mp, props, "hashdot.vm.lib", 0, 1, &lib_name );

    create_java_vm_f create_jvm_func = NULL;

    rv = get_create_jvm_function( mp, lib_name, &create_jvm_func );
    
    if( rv != APR_SUCCESS ) return rv;

    int options_len = apr_hash_count( props );
    
    vals = get_property_array( props, "hashdot.vm.options" );
    
    if( vals ) {
        rv = compact_option_flags( &vals, mp );
        set_property_array( props, "hashdot.vm.options", vals );

        if( rv != APR_SUCCESS) return rv;
        options_len += vals->nelts;
    }

    JavaVMOption options[ options_len ]; 

    if( vals ) {
        int i;
        for( i = 0; i < vals->nelts; i++ ) {
            const char *val = ((const char **) vals->elts )[i];
            options[opt  ].optionString = (char *) val;
            options[opt++].extraInfo = NULL;
        }
    }

    // Add java.class.path first (required by JVM)
    vals = get_property_array( props, "java.class.path" );
    if( vals ) {
        apr_array_header_t *tvals = NULL;
        rv = glob_values( mp, vals, &tvals );
        if( rv == APR_SUCCESS ) {
            options[opt].optionString = 
                property_to_option( mp, "java.class.path", tvals, ':' );
            options[opt++].extraInfo = NULL;
        }
    }

    if( rv != APR_SUCCESS ) return rv;

    // Add all other properties.
    const char *name = NULL;
    apr_hash_index_t *p;
    for( p = apr_hash_first( mp, props ); p; p = apr_hash_next( p ) ) {
        apr_hash_this( p, (const void **) &name, NULL, (void **) &vals );
        if( strcmp( name, "java.class.path" ) != 0 ) {
            options[opt].optionString = 
                property_to_option( mp, name, vals, ' ' );
            options[opt++].extraInfo = NULL;
        }
        if( rv != APR_SUCCESS ) break;
    }
    
    vm_args.version = JNI_VERSION_1_2; /* 1.2 is minimal for our purposes */
    vm_args.options = options;
    vm_args.nOptions = opt;
    vm_args.ignoreUnrecognized = JNI_FALSE;

    JavaVM * vm = NULL;
    JNIEnv * env = NULL;

    if( rv == APR_SUCCESS ) {
        rv = (*create_jvm_func)(&vm, &env, &vm_args);
    }
    
    if( rv == APR_SUCCESS ) {
        rv = install_hup_handler();
    }

    const char *main_name = NULL;
    rv = get_property_value( mp, props, "hashdot.main", 0, 1, &main_name );
    main_name = convert_class_name( mp, main_name );

    jclass cls = NULL;
    if( rv == APR_SUCCESS ) {
        cls = (*env)->FindClass( env, main_name );
            
        if( !cls ) {
            (*env)->ExceptionDescribe(env);
            rv = 3;
        }
    }

    jmethodID main_method = NULL;
    if( rv == APR_SUCCESS ) {
        main_method = (*env)->GetStaticMethodID( env, cls, "main", 
                                                 "([Ljava/lang/String;)V" );
        if( !main_method ) {
            (*env)->ExceptionDescribe(env);
            rv = 4;
        }
    }

    jclass string_cls = NULL;
    if( rv == APR_SUCCESS ) {
        string_cls = (*env)->FindClass( env, "java/lang/String" ); // . -> / ? 
        if( !string_cls ) {
            (*env)->ExceptionDescribe(env);
            rv = 5;
        }
    }
    
    jobjectArray args = NULL;
    if( rv == APR_SUCCESS ) {
        vals = get_property_array( props, "hashdot.args.pre" );

        jsize args_total = argc;
        int argp = 0;    
        if( vals ) {
            args_total += vals->nelts;
        }

        args = (*env)->NewObjectArray( env, args_total, string_cls, NULL );

        if( args && vals ) {
            int i;
            for( i = 0; i < vals->nelts; i++ ) {
                jstring arg = (*env)->NewStringUTF( env, 
                                  ((const char **) vals->elts )[i] );
                if( arg ) {
                    (*env)->SetObjectArrayElement( env, args, argp++, arg );
                    (*env)->DeleteLocalRef( env, arg );
                }
                else {
                    (*env)->ExceptionDescribe(env);
                    rv = 6;
                    break;
                }
            }
        }

        if( args ) {
            int i;
            for( i = 0; i < argc; i++ ) {  //start at arg 0 (post adjusted)
                jstring arg = (*env)->NewStringUTF( env, argv[i] );
                DEBUG( "setting: %s", argv[i] );
                if( arg ) {
                    (*env)->SetObjectArrayElement( env, args, argp++, arg );
                    (*env)->DeleteLocalRef( env, arg );
                }
                else {
                    (*env)->ExceptionDescribe(env);
                    rv = 6;
                    break;
                }
            }
        }
        else {
            (*env)->ExceptionDescribe(env);
            rv = 7;
        }
    }

    if( rv == APR_SUCCESS ) {
        (*env)->CallStaticVoidMethod( env, cls, main_method, args );
        DEBUG( "EXIT: returned from main." );
        (*env)->ExceptionDescribe(env);
    }

    if( args ) (*env)->DeleteLocalRef( env, args );

    if( rv == APR_SUCCESS ) {
        (*vm)->DestroyJavaVM(vm);
    }

    return rv;
}

static apr_array_header_t *
get_property_array( apr_hash_t *props, const char *name )
{
    return apr_hash_get( props, name, strlen( name ) + 1 );
}

static apr_status_t
get_property_value( apr_pool_t *mp,
                    apr_hash_t *props,
                    const char *name,
                    char separator,
                    int required,
                    const char **value )
{
    apr_status_t rv = APR_SUCCESS;
    apr_array_header_t *vals = get_property_array( props, name );
    if( vals != NULL ) {
        if( vals->nelts == 1 ) {
            *value = ( (const char **) vals->elts )[0];
        }
        else if( ( vals->nelts > 1 ) && ( separator != 0 ) ) {
            *value = apr_array_pstrcat( mp, vals, separator );
        }
        else {
            ERROR( "Need single value for property %s", name );
            rv = 1;
        }
    }
    else if( required ) {
        ERROR( "Required property %s not set.", name );
        rv = 1;
    }
    return rv;
}

static void
set_property_array( apr_hash_t *props, 
                    const char *name,
                    apr_array_header_t *vals )
{
    apr_hash_set( props, name, strlen( name ) + 1, vals );
}

static apr_status_t
set_property_value( apr_pool_t *mp,
                    apr_hash_t *props,
                    const char *name, 
                    const char * value )
{
    apr_status_t rv = APR_SUCCESS;
    apr_array_header_t *vals = apr_array_make( mp, 1, sizeof( const char* ) );
    *( (const char **) apr_array_push( vals ) ) = apr_pstrdup( mp, value );
    set_property_array( props, name, vals );
    return rv;
}

static char *
property_to_option( apr_pool_t *mp,
                    const char *name,
                    apr_array_header_t *vals,
                    char separator )
{
    return apr_psprintf( mp, "-D%s=%s", 
                         name, apr_array_pstrcat( mp, vals, separator ) );
}


static apr_status_t 
get_create_jvm_function( apr_pool_t *mp, 
                         const char *lib_name,
                         create_java_vm_f *symbol )
{
    apr_status_t rv = APR_SUCCESS;

    apr_dso_handle_t *lib;

    DEBUG( "Loading vm lib: %s", lib_name );
   
    rv = apr_dso_load( &lib, lib_name, mp );

    if( rv == APR_SUCCESS ) {
        rv = apr_dso_sym( (apr_dso_handle_sym_t *) symbol,
                          lib,
                          CREATE_JVM_FUNCTION_NAME );
    }

    if( rv != APR_SUCCESS ) {
        char errbuf[ 256 ];
        apr_dso_error( lib, errbuf, sizeof(errbuf) );
        ERROR( "[%d]: Loading jvm: %s", rv, errbuf );
    }
    return rv;
}

static char *
convert_class_name( apr_pool_t *mp, const char *cname )
{
    char * nname = apr_pstrdup( mp, cname );

    char * p = nname;
    while( *p != '\0' ) {
        if( *p == '.' ) *p = '/';
        ++p;
    }

    return nname;
}

static apr_status_t
set_hashdot_env( apr_hash_t *props,
                 apr_pool_t *mp )
{
    static const char *HASHDOT_ENV_PRE = "hashdot.env.";
    int plen = strlen( HASHDOT_ENV_PRE );
    apr_status_t rv = APR_SUCCESS;

    apr_array_header_t *vals;
    const char *name = NULL;
    apr_hash_index_t *p;
    for( p = apr_hash_first( mp, props ); p && (rv == APR_SUCCESS); 
         p = apr_hash_next( p ) ) {
        
        apr_hash_this( p, (const void **) &name, NULL, (void **) &vals );
        int nlen = strlen( name );
        if( ( nlen > plen ) && strncmp( HASHDOT_ENV_PRE, name, plen ) == 0 ) {
            const char *val = apr_array_pstrcat( mp, vals, ' ' );
            rv = apr_env_set( name + plen, val, mp );
        }
    }
    return rv;
}

static apr_status_t
exec_self( apr_hash_t *props,
           int argc,
           const char *argv[],
           apr_pool_t *mp )
{
    apr_status_t rv = APR_SUCCESS;
    apr_array_header_t *dpaths = 
        get_property_array( props, "hashdot.vm.libpath" );

    if( dpaths == NULL ) return rv;

    char * ldpenv = NULL;
    apr_env_get( &ldpenv, LIB_PATH_VAR, mp );

    apr_array_header_t *newpaths = 
        apr_array_make( mp, 8, sizeof( const char* ) );
    int i;
    for( i = 0; i < dpaths->nelts; i++ ) {
        const char *path = ((const char **) dpaths->elts )[i];
        if( !ldpenv || !strstr( ldpenv, path ) ) {
            *( (const char **) apr_array_push( newpaths ) ) = path;
            DEBUG( "New path to add: %s", path );
        }
    }

    // Need to set LD_LIBRARY_PATH, new paths found.
    if( newpaths->nelts > 0 ) {
        if( ldpenv ) {
            *( (const char **) apr_array_push( newpaths ) ) = ldpenv;
        }
        ldpenv = apr_array_pstrcat( mp, newpaths, ':' );
        DEBUG( "New %s = [%s]", LIB_PATH_VAR, ldpenv );
        rv = apr_env_set( LIB_PATH_VAR, ldpenv, mp );

        const char *exe_name = NULL;
        if( rv == APR_SUCCESS ) {
            rv = find_self_exe( &exe_name, mp );
        }

        // Exec using linux(-only?) /proc/self/exe link to self,
        // instead of argv[0], since the later will not specify a path
        // when initial call is made via PATH, and since execve won't
        // itself look at PATH.
        // Note: Can't use apr_proc_create for this, since it always
        // forks first.

        if( rv == APR_SUCCESS ) {
            DEBUG( "Exec'ing self as %s", argv[0] );
            execv( exe_name, (char * const *) argv );
            rv = APR_FROM_OS_ERROR( errno ); //shouldn't return from execv call
        }
    }
    
    return rv;
}

static apr_status_t
find_self_exe( const char **exe_name,
               apr_pool_t *mp )
{
    apr_status_t rv = APR_SUCCESS;
    char buffer[1024];
#ifdef __MacOS_X__
    uint32_t length = sizeof (buffer);
    if (_NSGetExecutablePath(buffer, &length) == 0 && buffer[0] == '/') {
        *exe_name = apr_pstrndup( mp, buffer, length );
    } 
    else {
        ERROR("failed to find exe: %s", strerror(errno));
        rv = APR_FROM_OS_ERROR( errno );
    }
#else
    ssize_t size = readlink( "/proc/self/exe", buffer, sizeof( buffer ) );
    if( size < 0 ) {
        ERROR("readlink failed: %s", strerror(errno));
        rv = APR_FROM_OS_ERROR( errno );
    } 
    else {
        *exe_name = apr_pstrndup( mp, buffer, size );
    }
#endif
    return rv;
}

static apr_status_t
set_process_name( int argc,
                  const char *argv[],
                  char *name )
{       
    apr_status_t rv = apr_initialize(); 
    DEBUG("Renaming Process to: %s",  name);
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
skip_flags( apr_hash_t *props,
            int argc,
            const char *argv[],
            int *file_offset )
{
    apr_status_t rv = APR_SUCCESS;

    apr_array_header_t *ignore_flags = 
        get_property_array( props, "hashdot.parse_flags.value_args" );

    apr_array_header_t *terminal_flags = 
        get_property_array( props, "hashdot.parse_flags.terminal" );

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
check_hashdot_cwd( apr_hash_t *props, 
                   const char **script,
                   apr_pool_t *mp )
{
    apr_status_t rv = APR_SUCCESS;

    char * nwd = NULL;

    const char * rel_wd = NULL;
    rv = get_property_value( mp, props, "hashdot.chdir", '/', 0, &rel_wd );
    if( ( rv == APR_SUCCESS ) && ( rel_wd != NULL ) ) {
        rv = apr_filepath_merge( &nwd, NULL, rel_wd, 0, mp );
    }

    if( ( rv == APR_SUCCESS ) && ( nwd != NULL ) ) {
        char * cwd = NULL;
        rv = apr_filepath_get( &cwd, 0, mp );
        if( ( rv == APR_SUCCESS ) && ( strcmp( cwd, nwd ) != 0 ) ) {
            
            // Convert to absolute script path if provided.
            if( script != NULL ) {
                char *absolute_script = NULL;
                apr_filepath_merge( &absolute_script, NULL, *script, 0, mp );
                *script = absolute_script;
                DEBUG( "Absolute script path is %s", absolute_script );
            }

            DEBUG( "Changing working dir to %s", nwd );
            rv = apr_filepath_set( nwd, mp );

            if( rv != APR_SUCCESS ) {
                print_error( rv, nwd );
            }
        }
    }
    return rv;
}

static apr_status_t
check_daemonize( apr_hash_t *props, 
                 apr_pool_t *mp )
{
    apr_status_t rv = APR_SUCCESS;

    const char * flag = NULL;
    rv = get_property_value( mp, props, "hashdot.daemonize", 0, 0, &flag );

    int daemon = 0;
    if( ( rv == APR_SUCCESS ) && ( flag != NULL ) && 
        ( strcmp( flag, "false" ) != 0 ) ) {

        DEBUG( "Forking daemon." );

        pid_t pid = fork();
        if( pid < 0 ) {
            rv = APR_FROM_OS_ERROR( errno );
        }
        if( pid > 0 ) { // Parent exit normally.
            exit( 0 );
        }
        if( rv == APR_SUCCESS ) {
            pid_t sid = setsid();
            if( sid < 0 ) {
                rv = APR_FROM_OS_ERROR( errno );
            }
        }
        daemon = 1;
    }

    if( rv == APR_SUCCESS ) {
        const char *fname = NULL;
        rv = get_property_value( mp, props, "hashdot.io_redirect.file", '/', 0, 
                                 &fname );
        if( ( fname != NULL ) ) {
            flag = NULL;
            rv = get_property_value( mp, props, "hashdot.io_redirect.append", 
                                     0, 0, &flag );
            int append = 1;    
            if( ( flag != NULL ) && ( strcmp( flag, "false" ) == 0 ) ) {
                append = 0;
            }

            DEBUG( "Redirecting stdout/stderr to %s", fname );
            
            if( ( freopen( "/dev/null", "r", stdin ) == NULL ) || 
                ( freopen( fname, append ? "a" : "w", stdout ) == NULL ) ||
                ( freopen( fname, append ? "a" : "w", stderr ) == NULL ) ) {
                rv = APR_FROM_OS_ERROR( errno );
            }

            if( daemon ) _redirect_fname = fname;
        }
    }

    return rv;
}

static void reopen_streams( int signo )
{
    if( _redirect_fname != NULL ) {
        if( freopen( _redirect_fname, "a", stdout ) == NULL ) {
            print_error( APR_FROM_OS_ERROR( errno ), "freopen stdout" );
        }
        if( freopen( _redirect_fname, "a", stderr ) == NULL ) {
            print_error( APR_FROM_OS_ERROR( errno ), "freopen stderr" );
        }
    }
}

static apr_status_t install_hup_handler()
{
    if( _redirect_fname != NULL ) {
        apr_signal( SIGHUP, &reopen_streams );
    }
    return APR_SUCCESS;
}
