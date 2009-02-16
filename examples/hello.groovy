#!/usr/bin/env groovy
// Must find symlink groovy -> hashdot* in path, since groovy.hdp 
// sets hashdot.header.comment = "//" for below to work:
//.test.prop = "hello world!"
//

msg = System.getProperty( "test.prop" )
if( msg == null ) {
    throw new RuntimeException( "Property 'test.prop' not set" )
}

println( msg )
