#!./hashdot
#. hashdot.profile = jruby-shortlived
#
## Test properity setters via jruby Test::Unit on results.
#
## test_whitespace_and_add
#.test.prop.a+=value1
#.test.prop.a=value2
#.test.prop.a+=value3
#.test.prop.b +=value1
#.test.prop.b= value2
#.test.prop.b +=value3
#.test.prop.c+=value1
#.test.prop.c=value2
#.test.prop.c+=value3
#. test.prop.d  +=  value1
#.  test.prop.d =   value2
#. test.prop.d  +=   value3
#
## test_add_equals
#. a = value-a
#. ab = ${a} value-b
#. abab += "${ab} ${ab}"
#. c = value-c
#. c += ${c}
#
## test_value_chars
#. vc1 = "string  value"
#. vc2 = "string  \"quotes\""
#. vc3 = "\tstring\\line\n\r"
#. vc4 =  "array with strings"  string"2
#. vc5 = value=5
#. vc6 =  value+6  +=value7 value=8
#. vc7 = "value = 7"
#
## glob test
#. java.class.path += test/test_prop*.rb
#
## recursive/delayed (:=) prop test
#. rprop := first ${cprop}
#. cprop = second

require 'test/unit'

class TestProperties < Test::Unit::TestCase

  def test_whitespace_and_add
    assert_prop( "test.prop.a", "value2 value3" )
    assert_prop( "test.prop.b", "value2 value3" )
    assert_prop( "test.prop.c", "value2 value3" )
    assert_prop( "test.prop.d", "value2 value3" )
  end

  def test_add_equals
    assert_prop( "a", "value-a" )
    assert_prop( "ab", "value-a value-b" )
    assert_prop( "abab", "value-a value-b value-a value-b" )
    assert_prop( "c", "value-c value-c" )
  end

  def test_profile_append
    assert_prop( "hashdot.profile", "shortlived jruby jruby-shortlived" )
  end

  def test_glob_class_path
    assert( property( "java.class.path" ).include?( "test/test_props.rb" ) )
  end

  def test_value_chars
    assert_prop( "vc1", "string  value" )
    assert_prop( "vc2", "string  \"quotes\"" )
    assert_prop( "vc3", "\tstring\\line\n\r" )
    assert_prop( "vc4", "array with strings string\"2" )
    assert_prop( "vc5", "value=5" )
    assert_prop( "vc6", "value+6 +=value7 value=8")
    assert_prop( "vc7", "value = 7" )
  end

  def test_recursive_prop
    assert_prop( "rprop", "first second" )
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
