#!/bin/bash

mkdir src-build
cd src-build

# QTDIR=~/Qt/5.3/clang_64 
echo $QTDIR

cmake -DCMAKE_BUILD_TYPE=Release ../src -D"Unix Makefiles"
make install

$QTDIR/bin/macdeployqt ./Application/drive.app
make package

