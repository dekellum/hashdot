#!/usr/bin/env jruby
#. hashdot.profile += shortlived
#. hashdot.chdir = ./test
#. java.class.path += ./foobar.jar

puts Java::foo.Bar.new.to_s
