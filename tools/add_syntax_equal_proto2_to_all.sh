#!/bin/bash
# Add 'syntax="proto2";' to the beginning of all proto files under 
# PWD(recursively) if the proto file does not have it.
for file in $(find . -name "*.proto"); do
    if grep -q 'syntax\s*=\s*"proto2";' $file; then
        echo "[already had] $file";
    else 
        sed -i '1s/^/syntax="proto2";\n/' $file
        echo "[replaced] $file"
    fi
done
