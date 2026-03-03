//
//  synthdef_compile.hpp
//  synthdef-compiler
//
//  Created by James McCartney on 8/5/24.
//

#ifndef synthdef_compile_hpp
#define synthdef_compile_hpp

#include "synthdef_types2.hpp"
#include "synthdef_compile_link.hpp"

namespace synthdef {
    void test(string synthName, int seconds, std::function<void()> f);

    string codegen(string synthName, std::function<void()> f);

    void runInternalAudioEngine(string dir, string synthName, int seconds);

}

#endif /* synthdef_compile_hpp */
