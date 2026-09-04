# Linux development environment for TZPL.
#
# clang-19 from noble-updates: clang 18 does not define __cpp_concepts >=
# 202002L, which libstdc++ requires before it exposes C++23 <expected>.
# g++-14 is installed for its libstdc++ headers/runtime: clang picks the
# newest GCC toolchain it finds, and libstdc++ 13 (the 24.04 default) lacks
# C++23 <print>.
#
# Build (from the repo root):
#     docker build -t tzpl-linux dev/linux
#
# Iterate -- keep build artifacts OUT of the source mount (the repo lives in
# Dropbox; a named volume avoids sync churn and slow bind-mount I/O):
#     docker volume create tzpl-build
#     docker run -it --rm -v "$PWD":/src -v tzpl-build:/build tzpl-linux
# then inside the container:
#     cmake -S /src -B /build/rel -G Ninja -DCMAKE_BUILD_TYPE=Release \
#           -DTZPL_BUILD_APP=ON -DTZPL_BUILD_TESTS=ON
#     cmake --build /build/rel -j$(nproc)
#     TZPL_BIN=/build/rel/lang/tzpl bash /src/lang/tests/run_tests.sh
#     /build/rel/synthdef-compiler/synthdef-compiler --test
#
# The first configure needs network access (FetchContent: sleef, JUCE, ...).
FROM ubuntu:24.04
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    clang-19 lld-19 g++-14 cmake ninja-build pkg-config git ca-certificates curl \
    libasound2-dev libjack-jackd2-dev libpulse-dev libsndfile1-dev \
    libx11-dev libxcomposite-dev libxcursor-dev libxext-dev libxinerama-dev \
    libxrandr-dev libxrender-dev libfreetype-dev libfontconfig1-dev \
    libglu1-mesa-dev libcurl4-openssl-dev xvfb python3-numpy \
    && rm -rf /var/lib/apt/lists/*
ENV CC=clang-19 CXX=clang++-19
WORKDIR /src
