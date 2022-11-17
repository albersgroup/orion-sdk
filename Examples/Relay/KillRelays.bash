    PIDS=`ps -eo pid,args | grep '^ *[0-9]' |grep Relay | cut -d ' ' -f2`
    echo "Killing $PIDS"
    kill -9 $PIDS