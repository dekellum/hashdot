#!/usr/bin/env rhino
//.hashdot.profile = shortlived
//.test.prop = "hello world!"
//

msg = java.lang.System.getProperty( "test.prop" )
if( msg == null ) {
    throw "Property 'test.prop' not set"
}

print( msg )
