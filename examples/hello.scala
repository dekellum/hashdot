#!/usr/bin/env scala
#.hashdot.profile = shortlived
#.hashdot.args.pre += -nocompdaemon
!#
class HelloWorld {
  def foo = {
    println("hello world!")
  }
}
val h = new HelloWorld
h.foo
