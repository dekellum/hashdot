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

#include <unistd.h>

#include <apr_general.h>
#include <apr_file_io.h>

#include "runtime.h"
#include "property.h"

static apr_file_t *_pid_file = NULL;

apr_status_t lock_pid_file()
{
    apr_status_t rv = APR_SUCCESS;

    const char * pfile_name = NULL;
    rv = get_property_value( "hashdot.pid_file", 0, 0, &pfile_name );

    if( ( rv == APR_SUCCESS ) && ( pfile_name != NULL ) ) {

        rv = apr_file_open( &_pid_file, pfile_name,
                            ( APR_READ | APR_WRITE | APR_CREATE ),
                            ( APR_FPROT_UREAD | APR_FPROT_UWRITE |
                              APR_FPROT_GREAD | APR_FPROT_WREAD ),
                            _mp );

        if( rv != APR_SUCCESS ) {
            ERROR( "Could not open pid file [%s] for write.", pfile_name );
        }

        if( rv == APR_SUCCESS ) {
            rv = apr_file_lock( _pid_file,
                                APR_FLOCK_EXCLUSIVE | APR_FLOCK_NONBLOCK );

            if( rv != APR_SUCCESS ) {
                WARN( "pid_file [%s] already locked. Exiting.", pfile_name );
                apr_file_close( _pid_file );
            }
        }

        if( rv == APR_SUCCESS ) {
            apr_file_printf( _pid_file, "%d\n", getpid() );
        }
    }

    return rv;
}

apr_status_t unlock_pid_file()
{
    apr_status_t rv = APR_SUCCESS;

    const char * pfile_name = NULL;
    rv = get_property_value( "hashdot.pid_file", 0, 0, &pfile_name );

    if( ( rv == APR_SUCCESS ) && ( pfile_name != NULL ) ) {
        rv = apr_file_remove( pfile_name, _mp );
    }

    if( _pid_file != NULL ) {
        rv = apr_file_close( _pid_file ) || rv;
        _pid_file = NULL;
    }

    return rv;
}
