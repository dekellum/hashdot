#!./hashdot
#.hashdot.profile = jruby-shortlived
#.hashdot.chdir = ./test


require 'test/unit'

class TestEnv < Test::Unit::TestCase

  def test_chdir
    assert( Dir.getwd, File.basename( __FILE__ ) )
    assert( File.exists?( './test_chdir.rb' ) ) 
  end

  def test_hashdot_script
    assert_prop( "hashdot.script", File.expand_path( __FILE__) )
    assert_prop( "hashdot.script.dir", 
                 File.dirname( File.expand_path( __FILE__ ) ) )
  end

  def assert_prop( name, expected, message = nil )
    actual = property( name )
    full_message = 
      build_message( message, 
                     "Property #{name}: <?> expected but was <?>.\n",
                     expected, actual )
    assert_block( full_message ) { expected == actual }
  end


  def property( name )
    Java::java.lang.System.getProperty( name )
  end

end
