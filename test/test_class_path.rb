#!/usr/bin/env jruby
#. hashdot.profile += shortlived
#. java.class.path += ./test/

require 'java'

import 'foo.Bar'

puts Bar.new.to_s
