#!/usr/bin/env jruby
#. hashdot.profile += shortlived
#. java.class.path += ./test/foobar.jar

puts Java::foo.Bar.new.to_s
