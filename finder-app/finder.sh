#!/bin/bash

filesdir=$1
searchstr=$2

if [ $# -ne 2 ]
then
    echo "Error: You must provide two arguments: <filesdir> and <searchstr>"
    exit 1
fi

if [ ! -d "$filesdir" ]
then
    echo "Error: $filesdir is not a valid directory or does not exist."
    exit 1
fi

x=$(find "$filesdir" -type f | wc -l)

y=$(find "$filesdir" -type f -print0 | xargs -0 grep -s "$searchstr" | wc -l)

echo "The number of files are $x and the number of matching lines are $y"

exit 0
