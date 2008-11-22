#!./jruby
# -*- ruby -*-
#. hashdot.profile = jruby-shortlived

# Save of orginals args since test/unit will consume them below
ARGV_ORIG = ARGV.dup

require 'test/unit'

class TestCmdLine < Test::Unit::TestCase
  
  def test_script_is_arg_zero
    assert_equal( __FILE__ , $0 )
  end
  
  def test_ps_command_line
    args = `ps -o command -p #{Process.pid}`.split
    args.shift
    assert_equal( [ 'jruby', $0 ] + ARGV_ORIG, args )
  end

  def test_ps_short_command
    args = `ps -o comm -p #{Process.pid}`.split
    args.shift
    assert_equal( "test_cmdline.rb", args.first )
  end
end
