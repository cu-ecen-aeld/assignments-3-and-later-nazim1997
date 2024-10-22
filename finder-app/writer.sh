#!/bin/bash

writefile=$1
writestr=$2

if [ $# -ne 2 ]
then
    echo "Error: You must provide two arguments: <writefile> and <writestr>"
    exit 1
fi

writedir=$(dirname "$writefile")

if [ ! -d "$writedir" ]
then
    mkdir -p "$writedir"
    if [ $? -ne 0 ]
    then
        echo "Error: Could not create directory $writedir"
        exit 1
    fi
fi

echo "$writestr" > "$writefile"
if [ $? -ne 0 ]
then
    echo "Error: Could not write to file $writefile"
    exit 1
fi

exit 0
