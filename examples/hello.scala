#!/usr/bin/env scala
#.hashdot.profile = shortlived
!#
class HelloWorld {
  def foo = {
    println("hello world!")
  }
}
val h = new HelloWorld
h.foo
