#!/usr/bin/env hashdot
# -*- ruby -*-
#. hashdot.profile = jruby-shortlived
## List shared libraries loaded.

# libnio.so gets loaded by IO operations, so prime it first, otherwise
# its a race condition if it will be in the lsof output below.
IO.popen( "echo hello", "r" ) { |f| f.read }

def loaded_libs 
  libs = []
  IO.popen( "lsof -p #{Process.pid}", "r" ) do |f|
    f.each do |line|
      if line =~ /(\S+\.(dylib|so))$/
        libs << $1
      end
    end
  end
  libs
end

puts "Libraries Loaded:"
loaded_libs.sort!.each { |lib| puts lib }
