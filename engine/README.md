# Audio Engine

A C++ real-time audio engine that loads native synth plugins and supports dynamic patching
with multi-channel connections, cross-fading, and sample-accurate command scheduling.

## Building with CMake

### Prerequisites

- CMake 3.16 or higher
- A C++ compiler with C++23 support (Clang on macOS)
- On macOS: Xcode Command Line Tools

### Quick Build

Use the provided build script:

```bash
./build.sh
```

### Manual Build

1. Create a build directory:
   ```bash
   mkdir build
   cd build
   ```

2. Configure with CMake:
   ```bash
   cmake ..
   ```

3. Build the project:
   ```bash
   make -j$(sysctl -n hw.ncpu)
   ```

The executable will be created at `build/bin/engine`.

### Build Options

```bash
# Debug build
cmake -DCMAKE_BUILD_TYPE=Debug ..

# Release build with optimizations
cmake -DCMAKE_BUILD_TYPE=Release ..

# Specify compiler
cmake -DCMAKE_CXX_COMPILER=clang++ ..
```

## Project Structure

- `engine/` - Source code directory
  - `main.cpp` - Main entry point, built-in test plugins, Voicer template
  - `RtAudio.cpp/h` - Cross-platform audio I/O library (CoreAudio on macOS)
  - `jscs_client_interface.cpp/hpp` - Public API: engine lifecycle, command bundling, plugin loading
  - `jscs_engine.cpp/hpp` - Engine struct, safety limiter, background threads
  - `jscs_silo.cpp/hpp` - Parallel processing units, topological sort, audio processing
  - `jscs_node.cpp/hpp` - Node, port, and control data model
  - `jscs_command.hpp` - Command base class and scheduler queue
  - `jscs_command_subclasses.hpp` - All concrete command types
  - `jscs_xfader.cpp/hpp` - Cross-fader with 7 fade curves
  - `jscs_atomic_fifo.hpp` - Lock-free SPSC FIFO for RT-safe communication
  - `jscs_sexpr.cpp/hpp` - S-expression parser for text-based commands
  - `jscs_common.hpp` - Type aliases and SIMD types
  - `jscs_hash.hpp` - 64-bit hash function
  - `jscs_random.hpp` - SIMD-templated PRNG
- `../shared/jscs_plugin_abi.h` - Pure C plugin ABI (shared with synthdef-compiler)
- `CMakeLists.txt` - CMake build configuration
- `build.sh` - Build script

## Dependencies

- **CoreAudio** (macOS) - Audio framework
- **CoreFoundation** (macOS) - Foundation framework
- **RtAudio** - Cross-platform audio I/O library (included in source)

## Platform Support

Currently configured for macOS with CoreAudio support.
