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

#include <apr_file_io.h>
#include <apr_strings.h>
#include <apr_fnmatch.h>

#include "runtime.h"
#include "property.h"

static apr_status_t
parse_line( char *line,
            apr_hash_t *rprops );

apr_hash_t *_props = NULL;

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

apr_array_header_t *
get_property_array( const char *name )
{
    return apr_hash_get( _props, name, strlen( name ) + 1 );
}

apr_status_t
get_property_value( const char *name,
                    char separator,
                    int required,
                    const char **value )
{
    apr_status_t rv = APR_SUCCESS;
    apr_array_header_t *vals = get_property_array( name );
    if( vals != NULL ) {
        if( vals->nelts == 1 ) {
            *value = ( (const char **) vals->elts )[0];
        }
        else if( ( vals->nelts > 1 ) && ( separator != 0 ) ) {
            *value = apr_array_pstrcat( _mp, vals, separator );
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

void
set_property_array( const char *name,
                    apr_array_header_t *vals )
{
    apr_hash_set( _props, name, strlen( name ) + 1, vals );
    DEBUG( "Set %s = %s", name, apr_array_pstrcat( _mp, vals, ' ' ) );
}

apr_status_t
set_property_value( const char *name,
                    const char * value )
{
    apr_status_t rv = APR_SUCCESS;
    apr_array_header_t *vals = apr_array_make( _mp, 1, sizeof( const char* ) );
    *( (const char **) apr_array_push( vals ) ) = apr_pstrdup( _mp, value );
    set_property_array( name, vals );
    return rv;
}

apr_status_t
glob_values( apr_array_header_t *values,
             apr_array_header_t **tvalues )
{
    apr_status_t rv = APR_SUCCESS;
    int i;
    *tvalues = apr_array_make( _mp, 16, sizeof( const char* ) );

    for( i = 0; i < values->nelts; i++ ) {
        const char *val = ((const char **) values->elts )[i];

        if( apr_fnmatch_test( val ) ) {
            char *lpath = strrchr( val, '/' );
            char *path = ( lpath == NULL ) ?
                "" : apr_pstrndup( _mp, val, lpath - val + 1 );

            apr_array_header_t *globs;
            rv = apr_match_glob( val, &globs, _mp );
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
                    apr_pstrcat( _mp, path, g, NULL );
            }
        }
        else {
            apr_finfo_t info;
            rv = apr_stat( &info, val, APR_FINFO_TYPE, _mp );
            if( rv != APR_SUCCESS ) {
                print_error( rv, val );
                rv = 2;
                break;
            }
            if( ( info.filetype != APR_DIR ) && ( info.filetype != APR_REG ) ) {
                ERROR( "%s not a file or directory [%d]\n",
                       val, (int) info.filetype );
                rv = 9;
                break;
            }
            *( (const char **) apr_array_push( *tvalues ) ) = val;
        }
    }

    return rv;
}

apr_status_t
parse_profile( const char *pname,
               apr_hash_t *rprops )
{
    apr_status_t rv = APR_SUCCESS;
    apr_file_t *in = NULL;
    char line[4096];
    apr_size_t length;

    char * fname = apr_psprintf( _mp, "%s/%s.hdp",
                                 HASHDOT_PROFILE_DIR,
                                 pname );

    rv = apr_file_open( &in, fname,
                        APR_FOPEN_READ,
                        APR_OS_DEFAULT, _mp );

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
            rv = parse_line( line, rprops );
        }
    }

    apr_file_close( in );

    return rv;
}

apr_status_t
parse_hashdot_header( const char *fname,
                      apr_hash_t *rprops )
{
    apr_status_t rv = APR_SUCCESS;
    apr_file_t *in = NULL;
    char line[4096];
    apr_size_t length;

    DEBUG( "Parsing hashdot header from %s", fname );

    const char * comment = "#";
    rv = get_property_value( "hashdot.header.comment", 0, 0, &comment );
    if( rv != APR_SUCCESS ) return rv;
    int clen = strlen( comment );

    rv = apr_file_open( &in, fname,
                        APR_FOPEN_READ,
                        APR_OS_DEFAULT, _mp );

    if( rv != APR_SUCCESS ) {
        ERROR( "Could not open file [%s].", fname );
        return rv;
    }

    int first = 1;

    while( rv == APR_SUCCESS ) {

        rv = apr_file_gets( line, sizeof( line ) - 1, in );

        if( rv == APR_EOF ) {
            rv = APR_SUCCESS;
            break;
        }

        if( rv != APR_SUCCESS ) break;

        length = strlen( line );

        // Ignore hashbang as first line.
        if( first ) {
            first = 0;
            if( ( length >= 2 ) && ( line[0] == '#' ) && ( line[1] == '!' ) ) {
                continue;
            }
        }

        // Test for end of first comment block
        if( ( length < clen ) || ( strncmp( line, comment, clen ) != 0 ) ) break;

        // Parse any hashdot directives.
        if( ( length > ( clen + 1 ) ) && line[clen] == '.' ) {
            rv = parse_line( line + clen + 1, rprops );
        }
    }

    apr_file_close( in );

    return rv;
}

static apr_status_t
parse_line( char *line,
            apr_hash_t *rprops )
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
                name = apr_pstrndup( _mp, b, p - b );
                state = ST_AFTER_NAME;
            }
            else if( *p == '\0' ) { rv = 11; goto END_LOOP; }
            else p++;
            break;

        case ST_AFTER_NAME:
            if( IS_WS( *p ) ) p++;
            else if( *p == '=' ) {
                values = apr_array_make( _mp, 16, sizeof( const char* ) );
                state = ST_VALUES;
                p++;
            }
            else if( ( *p == '+' ) && ( *(++p) == '=' ) ) {
                // Append to old value, but only if not
                // "hashdot.profile" in which case it will be appended
                // (for both += and =) below.
                if( strcmp( name, "hashdot.profile" ) != 0 ) {
                    values = get_property_array( name );
                }
                if( values == NULL ) {
                    values = apr_array_make( _mp, 16, sizeof( const char* ) );
                }
                state = ST_VALUES;
                p++;
            }
            else if( ( *p == ':' ) && ( *(++p) == '=' ) ) {
                apr_hash_set( rprops, apr_pstrdup( _mp, name ), strlen( name ) + 1,
                              apr_pstrdup( _mp, ++p ) );
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
                char *vstr = apr_pstrndup( _mp, value, voutp - value );
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
                char *vstr = apr_pstrndup( _mp, value, voutp - value );
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
                char * vname = apr_pstrndup( _mp, b, p - b );
                apr_array_header_t *vals = get_property_array( vname );
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
            rv = parse_profile( value, rprops );
        }

        // As special case append now processed values to old values
        // (implicit append).
        apr_array_header_t *old_vals = get_property_array( name );
        if( old_vals != NULL ) {
            apr_array_cat( old_vals, values );
            values = old_vals;
        }
    }

    if( ( name != NULL ) && ( rv == APR_SUCCESS ) ) {
        set_property_array( apr_pstrdup( _mp, name ), values );
    }

    return rv;
}

apr_status_t
expand_recursive_props( apr_hash_t *rprops )
{
    apr_status_t rv = APR_SUCCESS;

    const char *name = NULL;
    const char *value = NULL;
    apr_hash_index_t *p;
    for( p = apr_hash_first( _mp, rprops ); p; p = apr_hash_next( p ) ) {

        apr_hash_this( p, (const void **) &name, NULL, (void **) &value );

        char *line = apr_psprintf( _mp, "%s=%s", name, value );
        rv = parse_line( line, rprops );

        if( rv != APR_SUCCESS ) break;
    }

    return rv;
}
