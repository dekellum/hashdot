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

#include <apr_signal.h>

#include "runtime.h"
#include "daemon.h"
#include "property.h"

static const char * _redirect_fname = NULL;

static void reopen_streams( int signo );

apr_status_t check_daemonize()
{
    apr_status_t rv = APR_SUCCESS;

    const char * flag = NULL;
    rv = get_property_value( "hashdot.daemonize", 0, 0, &flag );

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
        rv = get_property_value( "hashdot.io_redirect.file", '/', 0, &fname );
        if( ( fname != NULL ) ) {
            flag = NULL;
            rv = get_property_value( "hashdot.io_redirect.append",
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

apr_status_t install_hup_handler()
{
    if( _redirect_fname != NULL ) {
        apr_signal( SIGHUP, &reopen_streams );
    }
    return APR_SUCCESS;
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
