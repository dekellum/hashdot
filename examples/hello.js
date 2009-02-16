#!/usr/bin/env rhino
// Must find symlink rhino -> hashdot* in path, since rhino.hdp 
// sets hashdot.header.comment = "//" for below to work:
//.test.prop = "hello world!"
//

msg = java.lang.System.getProperty( "test.prop" )
if( msg == null ) {
    throw "Property 'test.prop' not set"
}

print( msg )
