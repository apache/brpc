#!/bin/bash

version=$(git rev-parse --short HEAD 2> /dev/null)
branch=$(git rev-parse --abbrev-ref HEAD 2> /dev/null)
if [ $? -eq 0 ]
then
    echo $version-$branch
else
    cat $1/RELEASE_VERSION
fi
