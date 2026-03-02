//
//  symbol.hpp
//  static-lang-3
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
