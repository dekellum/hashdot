#!/usr/bin/env jruby
#. hashdot.profile += jruby-shortlived
#. java.class.path += ./test/foo*.jar

puts Java::foo.Bar.new.to_s
