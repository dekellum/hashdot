#!./jruby
#. hashdot.profile += jruby-shortlived
#. java.class.path += ${hashdot.script.dir}/foo*.jar
#. hashdot.chdir = /tmp

puts Java::foo.Bar.new.to_s
