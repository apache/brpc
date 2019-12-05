#!/bin/bash

v=$(git rev-parse --short HEAD 2> /dev/null)
if [ $? -eq 0 ]
then
    echo $v
else
    cat $1/RELEASE_VERSION
fi
