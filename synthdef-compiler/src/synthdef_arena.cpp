//
//  synthdef_arena.cpp
//  synthdef-after-rust-cpp-xcode
//
//  Created by James McCartney on 1/30/24.
//

#include "synthdef_arena.hpp"

namespace synthdef {

    thread_local Arena* gArena = nullptr;
    
    ArenaObj::ArenaObj() {
        gArena->objs.push_back(this);
    }

}
