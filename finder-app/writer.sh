#!/bin/sh
if [ "$#" -ne 2 ]; then
    echo "No parameters specified"
    return 1
fi

directory=$(dirname $1)
mkdir -p $directory
echo "$2" > $1