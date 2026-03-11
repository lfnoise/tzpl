#!/bin/bash
cmake -B build -DCMAKE_BUILD_TYPE=Release -DTZPL_BUILD_APP=ON
cmake --build build -j$(sysctl -n hw.ncpu)
