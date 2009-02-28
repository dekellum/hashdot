#!/usr/bin/env groovy
//.hashdot.profile = shortlived
//.test.prop = "hello world!"
//

msg = System.getProperty( "test.prop" )
if( msg == null ) {
    throw new RuntimeException( "Property 'test.prop' not set" )
}

println( msg )
