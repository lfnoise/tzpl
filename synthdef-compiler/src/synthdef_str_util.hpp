#pragma once
#include "synthdef_types2.hpp"
#include <memory>


namespace synthdef {
    int encodeRune(u32 utf, char *out);
    string encodeRune(u32 utf);
    u32 decodeRune(const char*& s);

    void spaces(string& s, int n);
    void dotindent(string& s, int n, int dotinterval);
    void tabIndent(string& s, int n);
    
    string ftos(i32 x);
    string ftos(i64 x);
    string ftos(f64 x);
    string ftos(f32 x);
}

