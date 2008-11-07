#!/bin/sh 

rel=`dirname $0`

echo "======= BEGIN: Expected ERROR returns ========="
for tst in $rel/*.htest; do
    echo $tst
    $tst 
    es=$?
    if [ es = '0' ]; then
      exit 20
    fi
done
echo "======== END: Expected ERROR returns =========="
