#pragma once
#include <functional>
#include <cassert>
#include <vector>

namespace synthdef {
    extern thread_local struct Arena* gArena;

    struct ArenaObj {
        ArenaObj();
    };

    struct Arena {
		std::vector<ArenaObj*> objs;

        Arena() = default;
        Arena(Arena const&) = delete;
        Arena& operator=(Arena const&) = delete;

        ~Arena() {
            clear();
        }

        void clear() {
			for (ArenaObj* obj : objs) {
                delete obj;
            }
            objs.clear();
        }
    };

    class PushArena {
        Arena* prevArena;
    public: 
        PushArena(Arena* newArena) {
            assert(newArena);
            prevArena = gArena;
            gArena = newArena;
        }

        ~PushArena() {
            gArena = prevArena;
        }
    };


}
