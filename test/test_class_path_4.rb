#!/usr/bin/env jruby
#. hashdot.profile += shortlived
#. java.class.path += ./test/foo*.jar

puts Java::foo.Bar.new.to_s
