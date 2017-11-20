#! /usr/bin/env python
#
# Compile *.proto files
# Written by Kevin.XU @ 2017.11.16

import glob
import os
import subprocess
import sys
import platform


from distutils.command.clean import clean as _clean

if sys.version_info[0] == 3:
    # Python 3
    from distutils.command.build_py import build_py_2to3 as _build_py
else:
    # Python 2
    from distutils.command.build_py import build_py as _build_py
from distutils.spawn import find_executable


# Find the Protocol Compiler.
if 'PROTOC' in os.environ and os.path.exists(os.environ['PROTOC']):
    protoc = os.environ['PROTOC']
elif os.path.exists("../src/protoc"):
    protoc = "../src/protoc"
elif os.path.exists("../src/protoc.exe"):
    protoc = "../src/protoc.exe"
elif os.path.exists("../vsprojects/Debug/protoc.exe"):
    protoc = "../vsprojects/Debug/protoc.exe"
elif os.path.exists("../vsprojects/Release/protoc.exe"):
    protoc = "../vsprojects/Release/protoc.exe"
else:
    protoc = find_executable("protoc")

print('Found {}'.format(protoc))

PROTOBUF_HOME = os.environ['PROTOBUF_HOME']
print('PROTOBUF_HOME = {}'.format(PROTOBUF_HOME))

def generate_proto(common_include_path, outputDir, source, require = True):
    """Invokes the Protocol Compiler to generate a .cc from the given
    .proto file.  Does nothing if the output already exists and is newer than
    the input."""

    if not require and not os.path.exists(source):
        return
    
    print('-------------------------------------------------------')
    print(source)
    # find directory which contains this *.proto
    dirPath = './'
    realPath = os.path.realpath(source)
    index = realPath.rfind("/")
    if index != -1:
        dirPath = realPath[0:index]

    # remove old files generated 
    protoFileName = os.path.basename(source)
    pos = protoFileName.rfind('.')
    if pos != -1:
        baseName = protoFileName[0:pos]
    cppFilePath = dirPath + '/' + baseName + '.pb.cc'
    headerFilePath = dirPath + '/' + baseName + '.pb.h'
    if os.path.exists(cppFilePath):
        print('remove old cpp file : {}'.format(cppFilePath))
        os.remove(cppFilePath)
    if os.path.exists(headerFilePath):
        print('remove old header file : {}'.format(headerFilePath))
        os.remove(headerFilePath)

    if ( os.path.exists(dirPath) and os.path.exists(source) ):
        print("Generate cpp files for %s" % source)

        if not os.path.exists(source):
            sys.stderr.write("Can't find required file: %s\n" % source)
            sys.exit(-1)

        if protoc is None:
            sys.stderr.write(
                "protoc is not installed nor found in ../src.  Please compile it "
                "or install the binary package.\n")
            sys.exit(-1)

        protoc_command = [ protoc, '--cpp_out={}'.format(outputDir), 
            '--proto_path={}'.format( PROTOBUF_HOME + '/include/' ), 
            '--proto_path={}'.format(common_include_path), source ]

        # Must change to the directory which contains *.proto
        protoc_command_str = ' '.join(protoc_command)
        print(protoc_command_str)
        if subprocess.call(protoc_command_str, shell=True) != 0:
            sys.exit(-1)
        if os.path.exists(cppFilePath):
            print('>>genereated cpp file : {}'.format(cppFilePath))
        if os.path.exists(headerFilePath):
            print('>>genereated header file : {}'.format(headerFilePath))


def travel_directory(directory_path): 
    result = []
    for lists in os.listdir(directory_path): 
        path = os.path.join(directory_path, lists) 
        if os.path.isdir(path): 
            result += travel_directory(path)
        else:
            if path.endswith(".proto"):
                result.append(path)
    return result
    

def process_dir(directory_path):
    proto_file_path_list = travel_directory(directory_path)
    for item in proto_file_path_list:
        generate_proto(directory_path,directory_path,item)

    
if __name__ == '__main__':
    currentRealPath = os.path.realpath('.')
    print(currentRealPath)
    process_dir('./src')
    process_dir('./example')
        
