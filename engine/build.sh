#!/bin/bash

# Build script for engine

# Create build directory if it doesn't exist
mkdir -p build

# Change to build directory
cd build

# Configure with CMake
cmake ..

# Build the project
make -j$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)

echo "Build complete! Executable is in build/bin/engine"
