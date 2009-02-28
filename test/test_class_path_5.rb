#!/usr/bin/env jruby
#. hashdot.profile += jruby-shortlived
#. java.class.path += ./te[sS]t

puts Java::foo.Bar.new.to_s
