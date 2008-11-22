#!/usr/bin/env hashdot
# -*- ruby -*-
#. hashdot.profile = jruby-shortlived

require 'java'
import 'java.lang.System'

props = System.properties.key_set.map do |key|
  [ key, System.get_property( key ) ]
end
props.sort! { |p,n| p[0] <=> n[0] }

props.each do |key,value|
  puts "#{key} = #{value}"
end
