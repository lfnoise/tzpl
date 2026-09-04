#!/bin/bash
cmake -B build -DCMAKE_BUILD_TYPE=Release -DTZPL_BUILD_APP=ON
cmake --build build -j$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)
