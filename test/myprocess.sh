#!/bin/bash

pid=$$

ps -p $pid
echo
ps -f -p $pid
echo
pgrep -lf 'myprocess.sh'
echo

while true; do sleep 100; done
