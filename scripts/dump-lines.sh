#!/bin/bash

function print_usage() {
    echo "Usage:"
    echo "  $0 <executable> <compilation-unit-name>"
}

if [ $# -ne 2 ]; then
    print_usage
    exit 1
fi

args=("$@")

if [ -z $(command -v readelf) ]; then
    objdump --dwarf=decodedline ${args[0]} | \
        egrep "${args[1]}[ ]+[0-9]+" | \
        awk '{print $1 " " $2 " " $3}'
else
    readelf --debug-dump=decodedline ${args[0]} | \
        egrep "${args[1]}[ ]+[0-9]+" | \
        awk '{print $1 " " $2 " " $3}'
fi
