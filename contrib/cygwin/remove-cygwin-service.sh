#!/bin/bash

# Contributed to the boxbackup project by Per Reedtz Thomsen. pthomsen@reedtz.com

# This script removes the 'boxbackup' service from the Windows service manager
# using the cygrunsrv utility. 

# Date      Who                      Comments
# 20041005  pthomsen@reedtz.com      Created 

cygrunsrv -R boxbackup

echo "Service \"boxbackup\" removed."

