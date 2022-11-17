#!/usr/bin/bash

PATH=.:./x86:$PATH  #Added to allow this work from the build directory or the install directory
#echo $PATH

echo "Starting $1"
PROG="Relay"
echo " Program is: $PROG"
EXT="*.relay"
echo " Looking for configuration files matching: $EXT"
ARGUMENTS=""

shutdown() {
    echo "Shutting Down!"
    
    PIDS=`ps -eo pid,args | grep '^ *[0-9]' |grep Relay | grep -v grep | cut -d ' ' -f2`
    echo "Killing $PIDS"
    echo $PIDS | xargs kill -9
    exit 0
}

trap shutdown SIGHUP SIGINT SIGQUIT SIGKILL SIGTERM

#trap "echo child exited"  SIGCHLD

getArguments() {
    INFILE=$1
    echo "Reading configuration file: $INFILE"
    BOO=""

    while read -r line; do
        if [[ $line =~ [0-9] ]]; then
            #echo -e "$line"
            LINE2=`echo $line|sed -e 's/[\r\n]//g'`
            BOO=`echo "$BOO $LINE2"`
            #echo ">> $BOO"
        fi
    done <$INFILE
    ARGUMENTS="$BOO"
}

echo""

for file in $(ls $EXT)
do
   #echo ">$file"
   getArguments $file
   CMD="$PROG $ARGUMENTS"
   #echo $CMD
   echo "Starting process for $file"
   $CMD 1>$file.out 2>$file.err &
done

while  true; do
#   echo "Enter 'x' to terminate program."
#   read input
#   if [[ $input =~ [xX] ]]; then
#      shutdown
#   else
      echo ""
      date +"%Y-%m-%d %T"
      ps -f|grep Relay|grep -v grep
      sleep 10
#    fi
done
