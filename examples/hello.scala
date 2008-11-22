#!/usr/bin/env hashdot
## Above hashbang could be /opt/bin/scala for simplicity
#. hashdot.profile = scala shortlived
!#
class HelloWorld {
    def foo = {
      println("Hello, world!")
      Thread.sleep(30000);
    }
}
val h = new HelloWorld
h.foo
