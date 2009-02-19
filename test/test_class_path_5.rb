#!/usr/bin/env jruby
#. hashdot.profile += shortlived
#. java.class.path += ./te[sS]t

puts Java::foo.Bar.new.to_s
