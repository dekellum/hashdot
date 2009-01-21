#!./hashdot
#.hashdot.profile = jruby-shortlived
#
#.hashdot.env.HVAR_1 = value
#.hashdot.env.HVAR_2 = list values

require 'java'
require 'test/unit'

class TestEnv < Test::Unit::TestCase
  import 'java.lang.System'

  def test_properties
    assert_prop( "hashdot.env.HVAR_1", "value" )
    assert_prop( "hashdot.env.HVAR_2", "list values" )
  end

  def test_env
    assert_equal( ENV['HVAR_1'], "value" )
    assert_equal( ENV['HVAR_2'], "list values" )
  end

  def test_child
    assert_equal( `sh -c 'echo $HVAR_1'`.strip, "value" )
  end

  def assert_prop( name, value )
    assert_equal( value, property( name ) )
  end

  def property( name )
    System.getProperty( name )
  end

end
