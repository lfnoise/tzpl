#include "synthdef_str_util.hpp"
#include <iomanip>

namespace synthdef {

    /**
     * Encode a code point using UTF-8
     *
     * @author Ondřej Hruška <ondra@ondrovo.com>
     * @license MIT
     *
     * @param out - output buffer (min 5 characters), will be 0-terminated
     * @param utf - code point 0-0x10FFFF
     * @return number of bytes on success, 0 on failure (also produces S+FFFD, which uses 3 bytes)
     */
    int encodeRune(u32 utf, char *out) {
        if (utf <= 0x7F) {
            // Plain ASCII
            out[0] = (char) utf;
            out[1] = 0;
            return 1;
        } else if (utf <= 0x07FF) {
            // 2-byte unicode
            out[0] = (char) (((utf >> 6) & 0x1F) | 0xC0);
            out[1] = (char) (((utf >> 0) & 0x3F) | 0x80);
            out[2] = 0;
            return 2;
        } else if (utf <= 0xFFFF) {
            // 3-byte unicode
            out[0] = (char) (((utf >> 12) & 0x0F) | 0xE0);
            out[1] = (char) (((utf >>  6) & 0x3F) | 0x80);
            out[2] = (char) (((utf >>  0) & 0x3F) | 0x80);
            out[3] = 0;
            return 3;
        } else if (utf <= 0x10FFFF) {
            // 4-byte unicode
            out[0] = (char) (((utf >> 18) & 0x07) | 0xF0);
            out[1] = (char) (((utf >> 12) & 0x3F) | 0x80);
            out[2] = (char) (((utf >>  6) & 0x3F) | 0x80);
            out[3] = (char) (((utf >>  0) & 0x3F) | 0x80);
            out[4] = 0;
            return 4;
        } else {
            // error - use replacement character
            out[0] = (char) 0xEF;
            out[1] = (char) 0xBF;
            out[2] = (char) 0xBD;
            out[3] = 0;
            return 0;
        }
    }

    string encodeRune(u32 utf) {
        char s[8];
        encodeRune(utf, s);
        return s;
    }

    u32 decodeRune(const char*& s) {
        u32 c;
        if ((u8)s[0] < 0x80) {
            c = (u8)s[0];
            if (c == 0) return 0;
            s += 1;
        } else if (((u8)s[0] & 0xe0) == 0xc0) {
            c = ((u32)((u8)s[0] & 0x1f) <<  6) |
                ((u32)((u8)s[1] & 0x3f) <<  0);
            s += 2;
        } else if (((u8)s[0] & 0xf0) == 0xe0) {
            c = ((u32)((u8)s[0] & 0x0f) << 12) |
                ((u32)((u8)s[1] & 0x3f) <<  6) |
                ((u32)((u8)s[2] & 0x3f) <<  0);
            s += 3;
        } else if (((u8)s[0] & 0xf8) == 0xf0 && ((u8)s[0] <= 0xf4)) {
            c = ((u32)((u8)s[0] & 0x07) << 18) |
                ((u32)((u8)s[1] & 0x3f) << 12) |
                ((u32)((u8)s[2] & 0x3f) <<  6) |
                ((u32)((u8)s[3] & 0x3f) <<  0);
            s += 4;
        } else {
            c = 0xFFFD; // invalid. 0xFFFD is 'REPLACEMENT CHARACTER
            s += 1;
        }
        if (c >= 0xd800 && c <= 0xdfff) {
            c = 0xFFFD; // surrogate half. 0xFFFD is 'REPLACEMENT CHARACTER
        }
        return c;
    }

    void spaces(string& s, int n) {
        for (int i = 0; i < n; ++i) {
            s += ' ';
        }
    }

    void dotindent(string& s, int n, int dotinterval) {
        if (n < 0) return;
        for (int i = 0; i < n; ++i) {
            if ((i%dotinterval) == 0) s += '.';
            else s += ' ';
        }
    }

    void tabIndent(string& s, int indent) {
        for (int i = 0; i < indent; ++i) {
            s += "\t";
        }
    }




    template <typename T>
    string floatToStringWithPrecision(T value, int precision, bool scientific) {
        std::ostringstream out;
        if (scientific) {
            out << std::scientific;
        } else {
            out << std::fixed;
        }
        out << std::setprecision(precision) << value;
        string result = out.str();
        
        // Ensure we do not end with a dot and remove trailing zeros.
        if (!scientific && result.find('.') != string::npos) {
            result.erase(result.find_last_not_of('0') + 1, string::npos);
            if (result.back() == '.') {
                result += '0'; // Add '0' to keep decimal for integer values.
            }
        }

        return result;
    }


    string ftos(i32 value) {
        return std::to_string(value);
    }
    string ftos(i64 value) {
        return std::to_string(value) + "LL";
    }

    template <typename T>
    string ftos_(T value) {
        switch (std::fpclassify(value)) {
            case FP_INFINITE:
                return std::signbit(value) ? "-inf" : "inf";
            case FP_NAN:
                return "nan";
            case FP_SUBNORMAL:
                // This has to be handled separately, because std::stod 
                // throws an out of range exception for subnormal values.
                char buffer[100];
                if constexpr (std::is_same_v<T, float>) {
                    snprintf(buffer, sizeof(buffer), "%.9e", value);
                } else {
                    snprintf(buffer, sizeof(buffer), "%.18e", value);
                }
                return buffer;
            case FP_ZERO:
                return std::signbit(value) ? "-0.0" : "0.0";
            case FP_NORMAL:
            default:
                break;
        }

        bool useScientific = std::abs(value) < T(1e-4) || std::abs(value) >= T(1e8);

        // Handle integer-like values separately.
        if (!useScientific && std::floor(value) == value) {
            return floatToStringWithPrecision(value, 1, useScientific);
        }

        int low = 1;
        int high = std::numeric_limits<double>::max_digits10;
        string result;

        int mid = low + (high - low) / 2;
        while (low <= high) {
            string midStr = floatToStringWithPrecision(value, mid, useScientific);

            T roundTrip;
            try {
                if constexpr (std::is_same_v<T, double>) {
                    roundTrip = std::stod(midStr);
                } else {
                    roundTrip = std::stof(midStr);
                }
                if (value == roundTrip) {
                    result = midStr;
                    high = mid - 1;
                } else {
                    low = mid + 1;
                }
            } catch (std::exception& e) {
                // Ignore the exception, treat it as a round trip failure, 
                // and continue with the binary search.
                low = mid + 1;
            }

            mid = low + (high - low) / 2;
        }
        if (result.empty()) {
            // failsafe
            result = floatToStringWithPrecision(value, mid, useScientific);
        }
        return result;
    }
    
    string ftos(f64 value) {
        return ftos_(value);
    }
    string ftos(f32 value) {
        return ftos_(value) + "f";
    }

}
