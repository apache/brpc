#!/bin/bash

output=$(cat $1/RELEASE_VERSION)
version=$(git log -1 --format="%h\\|%cI" 2> /dev/null)
branch=$(git rev-parse --abbrev-ref HEAD 2> /dev/null)
if [ $? -eq 0 ]
then
    output=$output"\\|"$branch"\\|"$version
fi
echo $output
