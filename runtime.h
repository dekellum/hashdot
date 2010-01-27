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

#ifndef _RUNTIME_H
#define _RUNTIME_H

#if defined(__APPLE__) && ( defined(__MACH__) || defined(__DARWIN__) ) && !defined(__MacOS_X__)
#  define __MacOS_X__ 1
#endif

#include <apr_general.h>

#define DEBUG(format, args...) \
    if( _debug ) { fprintf( stderr, "HASHDOT DEBUG: " ); \
                   fprintf( stderr, format , ## args); \
                   fprintf( stderr, "\n" ); \
                   fflush( stderr ); }

#define WARN(format, args...) \
    fprintf( stderr, "HASHDOT WARN: " ); \
    fprintf( stderr, format , ## args); \
    fprintf( stderr, "\n" );

#define ERROR(format, args...) \
    fprintf( stderr, "HASHDOT ERROR: " ); \
    fprintf( stderr, format , ## args); \
    fprintf( stderr, "\n" );

apr_status_t rt_initialize();

void rt_shutdown();

void print_error( apr_status_t rv, const char * info );

extern apr_pool_t *_mp;
extern int _debug;

#endif
