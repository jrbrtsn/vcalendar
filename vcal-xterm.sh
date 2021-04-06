#!/bin/bash

# This script launches the output from vcalendar in the 'less' pager in a terminal window.

[[ $1 == STOP ]] && exec 2>&1 vcalendar "$2" | less

exec xfce4-terminal --geometry 95x40 -H -x "$0" STOP "$1"
