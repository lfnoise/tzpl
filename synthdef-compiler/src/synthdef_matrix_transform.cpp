//
//  synthdef_matrix_transform.cpp
//  synthdef-compiler
//
//  Created by James McCartney on 3/22/24.
//

#include "tzpl_matrix_transform.hpp"

void test_transforms() {
    printf("test_transforms\n");
    using namespace synthdef;
    
    constexpr usize Rows = 4;
    constexpr usize Cols = 4;
    f32 indata[Rows * Cols] = {
        1.0, 2.0, 3.0, 4.0,
        5.0, 6.0, 7.0, 8.0,
        9.0, 10.0, 11.0, 12.0,
        13.0, 14.0, 15.0, 16.0
    };
    f32 indata2[9] = {
        1.0, 2.0, 3.0, 
        4.0, 5.0, 6.0, 
        7.0, 8.0, 9.0
    };
    {
        f32 outdata[8];
        f32 expected[8] = {2.0, 4.0, 6.0, 8.0, 10.0, 12.0, 14.0, 16.0};
        stride_slice_cols<f32, Rows, Cols, 1, 2, 2>(outdata, indata);
        for (usize i = 0; i < 8; ++i) {
            assert(outdata[i] == expected[i]);
        }
    }
    {
        f32 outdata[8];
        f32 expected[8] = {5.0, 6.0, 7.0, 8.0, 13.0, 14.0, 15.0, 16.0};
        stride_slice_rows<f32, Rows, Cols, 1, 2, 2>(outdata, indata);
        for (usize i = 0; i < 8; ++i) {
            assert(outdata[i] == expected[i]);
        }
    }
    {
        f32 outdata[8];
        f32 expected[8] = {1.0, 4.0, 5.0, 8.0, 9.0, 12.0, 13.0, 16.0};
        stride_cols<f32, Rows, Cols, 3>(outdata, indata);
        for (usize i = 0; i < 8; ++i) {
            assert(outdata[i] == expected[i]);
        }
    }
    {
        f32 outdata[8] = {-99.0, -99.0, -99.0, -99.0, -99.0, -99.0, -99.0, -99.0};
        f32 expected[8] = {1.0, 2.0, 3.0, 4.0, 13.0, 14.0, 15.0, 16.0};
        stride_rows<f32, Rows, Cols, 3>(outdata, indata);
        for (usize i = 0; i < 8; ++i) {
            assert(outdata[i] == expected[i]);
        }
    }
    {
        f32 outdata[32];
        f32 expected[32] = {1.0, 1.0, 2.0, 2.0, 3.0, 3.0, 4.0, 4.0,
                            5.0, 5.0, 6.0, 6.0, 7.0, 7.0, 8.0, 8.0,
                            9.0, 9.0, 10.0, 10.0, 11.0, 11.0, 12.0, 12.0,
                            13.0, 13.0, 14.0, 14.0, 15.0, 15.0, 16.0, 16.0};
        stutter_cols<f32, Rows, Cols, 2>(outdata, indata);
        for (usize i = 0; i < 32; ++i) {
            assert(outdata[i] == expected[i]);
        }
    }
    {
        f32 outdata[32];
        f32 expected[32] = {1.0, 2.0, 3.0, 4.0, 1.0, 2.0, 3.0, 4.0,
                            5.0, 6.0, 7.0, 8.0, 5.0, 6.0, 7.0, 8.0,
                            9.0, 10.0, 11.0, 12.0, 9.0, 10.0, 11.0, 12.0,
                            13.0, 14.0, 15.0, 16.0, 13.0, 14.0, 15.0, 16.0};
        stutter_rows<f32, Rows, Cols, 2>(outdata, indata);
        for (usize i = 0; i < 32; ++i) {
            assert(outdata[i] == expected[i]);
        }
    }
    {
        f32 outdata[16];
        f32 expected[16] = {2.0, 3.0, 4.0, 1.0, 6.0, 7.0, 8.0, 5.0, 10.0, 11.0, 12.0, 9.0, 14.0, 15.0, 16.0, 13.0};
        rotate_cols<f32, Rows, Cols>(outdata, indata, 1);
        for (usize i = 0; i < 16; ++i) {
            assert(outdata[i] == expected[i]);
        }
    }
    {
        f32 outdata[16];
        f32 expected[16] = {5.0, 6.0, 7.0, 8.0, 9.0, 10.0, 11.0, 12.0, 13.0, 14.0, 15.0, 16.0, 1.0, 2.0, 3.0, 4.0};
        rotate_rows<f32, Rows, Cols>(outdata, indata, 1);
        for (usize i = 0; i < 16; ++i) {
            assert(outdata[i] == expected[i]);
        }
    }
    {
        f32 outdata[16];
        f32 expected[16] = {4.0, 1.0, 2.0, 3.0, 8.0, 5.0, 6.0, 7.0, 12.0, 9.0, 10.0, 11.0, 16.0, 13.0, 14.0, 15.0};
        rotate_cols<f32, Rows, Cols>(outdata, indata, 3);
//        for (usize i = 0; i < 16; ++i) {
//            printf("rotate_cols %2zu %4.1f %4.1f\n", i, outdata[i], expected[i]);
//        }
        for (usize i = 0; i < 16; ++i) {
            assert(outdata[i] == expected[i]);
        }
    }
    {
        f32 outdata[16];
        f32 expected[16] = {13.0, 14.0, 15.0, 16.0, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0, 10.0, 11.0, 12.0};
        rotate_rows<f32, Rows, Cols>(outdata, indata, 3);
//        for (usize i = 0; i < 16; ++i) {
//            printf("rotate_rows %2zu %4.1f %4.1f\n", i, outdata[i], expected[i]);
//        }
        for (usize i = 0; i < 16; ++i) {
            assert(outdata[i] == expected[i]);
        }
    }
    {
        f32 outdata[16];
        f32 expected[16] = {1.0, 5.0, 9.0, 13.0, 2.0, 6.0, 10.0, 14.0, 3.0, 7.0, 11.0, 15.0, 4.0, 8.0, 12.0, 16.0};
        transpose<f32, Rows, Cols>(outdata, indata);
        for (usize i = 0; i < 16; ++i) {
            assert(outdata[i] == expected[i]);
        }
    }
    {
        f32 outdata[16];
        f32 expected[16] = {4.0, 3.0, 2.0, 1.0, 8.0, 7.0, 6.0, 5.0, 12.0, 11.0, 10.0, 9.0, 16.0, 15.0, 14.0, 13.0};
        reverse_cols<f32, Rows, Cols>(outdata, indata);
        for (usize i = 0; i < 16; ++i) {
            assert(outdata[i] == expected[i]);
        }
    }
    {
        f32 outdata[16];
        f32 expected[16] = {13.0, 14.0, 15.0, 16.0, 9.0, 10.0, 11.0, 12.0, 5.0, 6.0, 7.0, 8.0, 1.0, 2.0, 3.0, 4.0};
        reverse_rows<f32, Rows, Cols>(outdata, indata);
        for (usize i = 0; i < 16; ++i) {
            assert(outdata[i] == expected[i]);
        }
    }
    {
        f32 outdata[18];
        f32 expected[18] = {1.0, 2.0, 3.0, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0, 7.0, 8.0, 9.0};
        cycle_cols<f32, 3, 3, 2>(outdata, indata2);
        for (usize i = 0; i < 18; ++i) {
            assert(outdata[i] == expected[i]);
        }
    }
    {
        f32 outdata[18];
        f32 expected[18] = {1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0};
        cycle_rows<f32, 3, 3, 2>(outdata, indata2);
//        for (usize i = 0; i < 18; ++i) {
//            printf("cycle_rows %2zu %4.1f %4.1f\n", i, outdata[i], expected[i]);
//        }
        for (usize i = 0; i < 18; ++i) {
            assert(outdata[i] == expected[i]);
        }
    }
    {
        f32 outdata[4];
        f32 expected[4] = {6.0, 7.0, 10.0, 11.0};
        slice<f32, 4, 4, 2, 2>(outdata, indata, 1, 1);
//        for (usize i = 0; i < 4; ++i) {
//            printf("tile %2zu %4.1f %4.1f\n", i, outdata[i], expected[i]);
//        }
        for (usize i = 0; i < 4; ++i) {
            assert(outdata[i] == expected[i]);
        }
    }
    {
        f32 outdata[4];
        f32 expected[4] = {9, 10, 13, 14};
        slice<f32, 4, 4, 2, 2>(outdata, indata, 2, 0);
//        for (usize i = 0; i < 4; ++i) {
//            printf("tile %2zu %4.1f %4.1f\n", i, outdata[i], expected[i]);
//        }
        for (usize i = 0; i < 4; ++i) {
            assert(outdata[i] == expected[i]);
        }
    }
    {
        f32 outdata[8];
        f32 expected[8] = {5, 6, 7, 8, 9, 10, 11, 12};
        slice_rows<f32, 4, 4, 2>(outdata, indata, 1);
//        for (usize i = 0; i < 8; ++i) {
//            printf("slice_rows %2zu %4.1f %4.1f\n", i, outdata[i], expected[i]);
//        }
        for (usize i = 0; i < 8; ++i) {
            assert(outdata[i] == expected[i]);
        }
    }
    {
        f32 outdata[8];
        f32 expected[8] = {2, 3, 6, 7, 10, 11, 14, 15};
        slice_cols<f32, 4, 4, 2>(outdata, indata, 1);
//        for (usize i = 0; i < 8; ++i) {
//            printf("slice_cols %2zu %4.1f %4.1f\n", i, outdata[i], expected[i]);
//        }
        for (usize i = 0; i < 8; ++i) {
            assert(outdata[i] == expected[i]);
        }
    }
    {
        f32 outdata[18];
        f32 expected[18] = {1, 1, 3, 2, 2, 3, 4, 4, 6, 5, 5, 6, 7, 7, 9, 8, 8, 9};
        u64 index[6] = {0, 0, 2, 1, 1, 2};
        permute_cols<f32, u64, 3, 3, 6>(outdata, indata2, index);
        for (usize i = 0; i < 18; ++i) {
            assert(outdata[i] == expected[i]);
        }
    }
    {
        f32 outdata[18];
        f32 expected[18] = {1, 2, 3, 1, 2, 3, 7, 8, 9, 4, 5, 6, 4, 5, 6, 7, 8, 9};
        u64 index[6] = {0, 0, 2, 1, 1, 2};
        permute_rows<f32, u64, 3, 3, 6>(outdata, indata2, index);
        for (usize i = 0; i < 18; ++i) {
            assert(outdata[i] == expected[i]);
        }
    }
    
        
}





