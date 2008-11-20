#!/usr/bin/env hashdot
## Above hash-bang could be /usr/local/bin/scala for simplicity
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
