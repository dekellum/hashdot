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

#include <apr_strings.h>
#include <apr_env.h>

#include "property.h"
#include "runtime.h"
#include "libpath.h"

#ifdef __MacOS_X__
#  include <mach-o/dyld.h>
#  include <dlfcn.h>
#  define LIB_PATH_VAR "DYLD_LIBRARY_PATH"
#else
#  define LIB_PATH_VAR "LD_LIBRARY_PATH"
#endif

static apr_status_t find_self_exe( const char **exe_name );

apr_status_t exec_self( int argc,
                        const char *argv[] )
{
    apr_status_t rv = APR_SUCCESS;
    apr_array_header_t *dpaths =
        get_property_array( "hashdot.vm.libpath" );

    if( dpaths == NULL ) return rv;

    char * ldpenv = NULL;
    apr_env_get( &ldpenv, LIB_PATH_VAR, _mp );

    apr_array_header_t *newpaths =
        apr_array_make( _mp, 8, sizeof( const char* ) );
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
        ldpenv = apr_array_pstrcat( _mp, newpaths, ':' );
        DEBUG( "New %s = [%s]", LIB_PATH_VAR, ldpenv );
        rv = apr_env_set( LIB_PATH_VAR, ldpenv, _mp );

        const char *exe_name = NULL;
        if( rv == APR_SUCCESS ) {
            rv = find_self_exe( &exe_name );
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

static apr_status_t find_self_exe( const char **exe_name )
{
    apr_status_t rv = APR_SUCCESS;
    char buffer[1024];
#ifdef __MacOS_X__
    uint32_t length = sizeof (buffer);
    if (_NSGetExecutablePath( buffer, &length ) == 0 && buffer[0] == '/') {
        *exe_name = apr_pstrndup( _mp, buffer, length );
    }
    else {
        ERROR( "failed to find exe: %s", strerror(errno) );
        rv = APR_FROM_OS_ERROR( errno );
    }
#else
    ssize_t size = readlink( "/proc/self/exe", buffer, sizeof( buffer ) );
    if( size < 0 ) {
        ERROR( "readlink failed: %s", strerror(errno) );
        rv = APR_FROM_OS_ERROR( errno );
    }
    else {
        *exe_name = apr_pstrndup( _mp, buffer, size );
    }
#endif
    return rv;
}
