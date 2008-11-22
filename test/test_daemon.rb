#!./hashdot
#. hashdot.profile = jruby-shortlived

require 'test/unit'
require 'fileutils'

TEST_DIR = File.dirname( __FILE__ )

class TestCmdLine < Test::Unit::TestCase
  include FileUtils

  LOG = File.join( TEST_DIR, "daemon.log" )

  def setup
    rm_rf LOG
  end

  def test_daemon
    assert( system( "#{TEST_DIR}/daemon" ),
            "daemon.rb: returned status $?" )
    tries = 5
    while tries > 0
      sleep 1
      break if( File.exists?( LOG ) && File.size( LOG ) > 0 )
      tries -= 1
    end
    
    assert_equal( "hello from daemon", File.read( LOG ).strip )
  end

end
