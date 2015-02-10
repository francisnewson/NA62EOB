#!/bin/sh
source /afs/cern.ch/sw/lcg/contrib/gcc/4.7/x86_64-slc6/setup.sh
export BOOST=/afs/cern.ch/sw/lcg/external/Boost/1.55.0_python2.7/x86_64-slc6-gcc47-opt
export LD_LIBRARY_PATH=$BOOST/lib:$LD_LIBRARY_PATH
export LD_PRELOAD=/usr/lib64/libXrdPosixPreload.so:${LD_PRELOAD}
