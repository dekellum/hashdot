#!./jruby
#. hashdot.profile += jruby-shortlived
#. java.class.path += ./test/

puts Java::foo.Bar.new.to_s
