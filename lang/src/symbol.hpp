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

//
//  symbol.hpp
//  lang
//
//  Lock-free global symbol table. Symbols use std::string (system allocator)
//  so they can be shared across VM instances and threads.
//

#ifndef symbol_hpp
#define symbol_hpp

#include <string>
#include <string_view>

namespace ts {

// Interned string - can compare by pointer equality
class Symbol {
    friend class SymbolTable;
    std::string s_;
    size_t hash_;
public:
    Symbol(std::string str)
        : s_(std::move(str))
        , hash_(std::hash<std::string>{}(s_)) {}

    std::string_view str() const noexcept { return s_; }
    const char* cstr() const noexcept { return s_.c_str(); }
    size_t hash() const noexcept { return hash_; }

    bool isPublic() const noexcept {
        return !s_.empty() && s_[0] != '_';
    }
};

using SymbolPtr = const Symbol*;

// Intern a symbol in the global lock-free symbol table.
// Thread-safe. The returned pointer is valid for the lifetime of the process.
SymbolPtr intern(std::string_view name);

} // namespace ts

#endif /* symbol_hpp */
