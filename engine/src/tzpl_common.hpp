//
//  tzpl_common.hpp
//  audio engine
//
//  Created by James McCartney on 2/2/21.
//

#ifndef tzpl_common_hpp
#define tzpl_common_hpp

#include <complex>
#include <vector>
#include <string>
#include <cassert>
#include "tzpl_simd.hpp"

namespace engine {

#pragma mark BASE TYPES

using c8 = char;

using i8  = char;
using i16 = short;
using i32 = int;
using i64 = long;	// int64_t is 'long long' and breaks compatibility with simd.h.
using i128 = __int128_t;

using u8  = unsigned char ;
using u16 = unsigned short;
using u32 = unsigned int;
using u64 = unsigned long;
using u128 = __uint128_t;

using f32 = float;
using f64 = double;

using usize = size_t;
using isize = ssize_t;


}

#endif /* audio_engine_hpp */
