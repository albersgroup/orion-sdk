#!/usr/bin/bash

PATH=.:./x86:$PATH  #Added to allow this work from the build directory or the install directory

RelayRunner.bash
sleep 1
KillRelays.bash