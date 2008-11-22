#!./hashdot
#.hashdot.profile = jruby-shortlived
#.hashdot.chdir = ./test

require 'test/unit'

class TestEnv < Test::Unit::TestCase

  def test_chdir
    assert( Dir.getwd, File.basename( __FILE__ ) )
    assert( File.exists?( './test_chdir.rb' ) ) 
  end

end
