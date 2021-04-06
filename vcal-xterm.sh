#!/bin/bash

# This script launches the output from vcal in the 'less' pager in a terminal window.

if [[ $1 == STOP ]]; then
   exec 2>&1 vcalendar "$2" | less
fi

exec xfce4-terminal --geometry 95x40 -H -x "$0" STOP "$1"
