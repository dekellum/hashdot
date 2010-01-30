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

#include "jvm.h"

#include "runtime.h"
#include "property.h"
#include "daemon.h"
#include "pidfile.h"

#include <apr_strings.h>
#include <apr_hash.h>
#include <apr_dso.h>

#include <jni.h>

#ifdef __MacOS_X__
#  define CREATE_JVM_FUNCTION_NAME "JNI_CreateJavaVM_Impl"
#else
#  define CREATE_JVM_FUNCTION_NAME "JNI_CreateJavaVM"
#endif

typedef jint (*create_java_vm_f)(JavaVM **, JNIEnv **, JavaVMInitArgs *);

static char *
property_to_option( const char *name,
                    apr_array_header_t *vals,
                    char separator );

static apr_status_t
get_create_jvm_function( const char *lib_name,
                         create_java_vm_f *symbol );

static char *
convert_class_name( const char *cname );

static apr_status_t
compact_option_flags( apr_array_header_t **values );

static void jvm_abort_hook();
static void jvm_exit_hook( int status );

apr_status_t init_jvm( int argc, const char *argv[] )
{
    apr_status_t rv = APR_SUCCESS;
    JavaVMInitArgs vm_args;
    apr_array_header_t *vals;
    int opt = 0;

    const char *lib_name = NULL;
    rv = get_property_value( "hashdot.vm.lib", 0, 1, &lib_name );

    create_java_vm_f create_jvm_func = NULL;

    rv = get_create_jvm_function( lib_name, &create_jvm_func );

    if( rv != APR_SUCCESS ) return rv;

    int options_len = 2 + apr_hash_count( _props );

    vals = get_property_array( "hashdot.vm.options" );

    if( vals ) {
        rv = compact_option_flags( &vals );
        set_property_array( "hashdot.vm.options", vals );

        if( rv != APR_SUCCESS) return rv;
        options_len += vals->nelts;
    }

    JavaVMOption options[ options_len ];

    // Install exit and abort hooks (first 2)
    options[opt  ].optionString = "exit";
    options[opt++].extraInfo    = &jvm_exit_hook;
    options[opt  ].optionString = "abort";
    options[opt++].extraInfo    = &jvm_abort_hook;

    if( vals ) {
        int i;
        for( i = 0; i < vals->nelts; i++ ) {
            const char *val = ((const char **) vals->elts )[i];
            options[opt  ].optionString = (char *) val;
            options[opt++].extraInfo = NULL;
        }
    }

    // Add java.class.path first (required by JVM)
    vals = get_property_array( "java.class.path" );
    if( vals ) {
        apr_array_header_t *tvals = NULL;
        rv = glob_values( vals, &tvals );
        if( rv == APR_SUCCESS ) {
            options[opt].optionString =
                property_to_option( "java.class.path", tvals, ':' );
            options[opt++].extraInfo = NULL;
        }
    }

    if( rv != APR_SUCCESS ) return rv;

    // Add all other properties.
    const char *name = NULL;
    apr_hash_index_t *p;
    for( p = apr_hash_first( _mp, _props ); p; p = apr_hash_next( p ) ) {
        apr_hash_this( p, (const void **) &name, NULL, (void **) &vals );
        if( strcmp( name, "java.class.path" ) != 0 ) {
            options[opt].optionString =
                property_to_option( name, vals, ' ' );
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
    rv = get_property_value( "hashdot.main", 0, 1, &main_name );
    main_name = convert_class_name( main_name );

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
        vals = get_property_array( "hashdot.args.pre" );

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
                DEBUG( "Argument: %s", argv[i] );
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

static apr_status_t
compact_option_flags( apr_array_header_t **values )
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

    apr_hash_t * occurred = apr_hash_make( _mp );

    apr_array_header_t * tmp =
        apr_array_make( _mp, (*values)->nelts, sizeof( const char* ) );

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
    *values = apr_array_make( _mp, tmp->nelts, sizeof( const char* ) );
    while( tmp->nelts > 0 ) {
        *(const char **) apr_array_push( *values ) =
            *(const char **) apr_array_pop( tmp );
    }

    return rv;
}

static char *
property_to_option( const char *name,
                    apr_array_header_t *vals,
                    char separator )
{
    return apr_psprintf( _mp, "-D%s=%s",
                         name, apr_array_pstrcat( _mp, vals, separator ) );
}

static apr_status_t
get_create_jvm_function( const char *lib_name,
                         create_java_vm_f *symbol )
{
    apr_status_t rv = APR_SUCCESS;

    apr_dso_handle_t *lib;

    DEBUG( "Loading vm lib: %s", lib_name );

    rv = apr_dso_load( &lib, lib_name, _mp );

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
convert_class_name( const char *cname )
{
    char * nname = apr_pstrdup( _mp, cname );

    char * p = nname;
    while( *p != '\0' ) {
        if( *p == '.' ) *p = '/';
        ++p;
    }

    return nname;
}

static void jvm_abort_hook()
{
    WARN( "abort hook: abnormal exit." );
    unlock_pid_file();
}

static void jvm_exit_hook( int status )
{
    DEBUG( "exit hook: status %d.", status );
    unlock_pid_file();
}
