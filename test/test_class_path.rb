#!/usr/bin/env jruby
#. hashdot.profile += shortlived
#. java.class.path += ./test

# FIXME: Note however that "./test/" (trailing slash) currently fails.

require 'java'

import 'foo.Bar'

puts Bar.new.to_s
