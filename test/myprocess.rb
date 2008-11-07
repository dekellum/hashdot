#!/usr/local/bin/jruby-shortlived
# -*- ruby -*-
#. hashdot.vm.options += -Xmx256m
## Deamonize on startup:
#. hashdot.profile += daemon
#. hashdot.io_redirect.file = ./myprocess.log

# Low level stat interface used by top and non-full ps)
# File.open( "/proc/self/stat", 'rb' ) do |cout|
#  puts cout.readlines
# end

# Show this process as seen by various commands:
commands = [ "ps -p #{Process.pid}", 
             "ps -f -p #{Process.pid}" ]

commands << "pgrep -lf #{$0}" unless $0 == '-' 

commands.each do |command| 
  puts command
  IO.popen( command, "r" ) do |cout|
    puts cout.readlines
  end
  puts
end      

10.times do
  printf( "%s: ", Time.now )
  Kernel.sleep 1
  puts "Sleeping..."
end
