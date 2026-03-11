//
//  builtins.hpp
//  lang
//
//  Built-in math function registration
//

#ifndef builtins_hpp
#define builtins_hpp

#include "type_checker.hpp"

namespace ts {

// Register all built-in math functions into the function table
void registerBuiltinFunctions(Compiler& compiler,
    std::unordered_map<std::string, std::vector<FuncInfo>>& functions);

} // namespace ts

#endif /* builtins_hpp */
