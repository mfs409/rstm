#!/usr/bin/env sh
if [ `dirname $0` == '.' ]; then
    echo "Must use $0 script from and out-of-source directory\n"
    exit 1
fi

rm -rf * && cmake -C `dirname $0`/libitm2stm/dtmc.cmake `dirname $0`
make $@ itm2stm64