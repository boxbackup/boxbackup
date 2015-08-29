#!/bin/sh

source `dirname $0`/environment.sh

export PATH=$DEP_PATH/sys-root/mingw/bin:$PATH

exec `dirname $0`/../../runtest.pl "$@"
