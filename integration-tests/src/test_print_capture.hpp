// Tzopilotl
// Copyright (C) 2026 James McCartney
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

// A FILE* the tests hand to VM::setPrintOutput, whose contents finish() returns
// as a string. open_memstream on POSIX; Windows has no memory streams, so a
// delete-on-close temp file stands in.

#pragma once

#include <cstdio>
#include <cstdlib>
#include <string>
#ifdef _WIN32
#include <filesystem>
#include <process.h>
#endif

struct TestPrintCapture {
    FILE* file = nullptr;

    TestPrintCapture() {
#ifdef _WIN32
        static int counter = 0;
        auto path = std::filesystem::temp_directory_path()
                  / ("tzpl_test_capture_" + std::to_string(_getpid()) + "_"
                     + std::to_string(++counter) + ".txt");
        file = std::fopen(path.string().c_str(), "w+bD");  // D: delete on close
#else
        file = open_memstream(&buf_, &len_);
#endif
    }
    ~TestPrintCapture() { finish(); }

    // Flush, close, and return everything written so far. Idempotent.
    std::string finish() {
        if (file) {
            std::fflush(file);
#ifdef _WIN32
            std::fseek(file, 0, SEEK_END);
            long n = std::ftell(file);
            std::string s(n > 0 ? (size_t)n : 0, '\0');
            std::rewind(file);
            if (n > 0) s.resize(std::fread(s.data(), 1, (size_t)n, file));
            std::fclose(file);
            output_ = std::move(s);
#else
            std::fclose(file);
            output_ = buf_ ? std::string(buf_, len_) : std::string();
            std::free(buf_);
            buf_ = nullptr;
#endif
            file = nullptr;
        }
        return output_;
    }

private:
    std::string output_;
#ifndef _WIN32
    char* buf_ = nullptr;
    size_t len_ = 0;
#endif
};
