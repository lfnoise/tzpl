//
//  synthdef_compile.hpp
//  synthdef-compiler
//
//  Created by James McCartney on 8/5/24.
//

#ifndef synthdef_compile_hpp
#define synthdef_compile_hpp

#include "synthdef_types2.hpp"

namespace synthdef {
    void test(string synthName, int seconds, std::function<void()> f);

    string codegen(string synthName, std::function<void()> f);

    string getBuildDir();
    void writeCodeToFile(string dir, string synthName, string ccode);
    void compileAndLink(string dir, string synthName);
    void runInternalAudioEngine(string dir, string synthName, int seconds);

}

#endif /* synthdef_compile_hpp */
