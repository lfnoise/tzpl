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
