#!/usr/bin/env hashdot
## Above hash-bang could be /usr/local/bin/scala for simplicity
#. hashdot.profile = scala shortlived
!#
object HelloWorld {
    def main(args: Array[String]) {
    println("Hello, world! " + args.toList)
  }
}
HelloWorld.main(args)
