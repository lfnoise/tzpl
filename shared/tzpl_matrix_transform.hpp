//
//  synthdef_matrix_transform.hpp
//  tzpl
//
//  Created by James McCartney on 3/22/24.
//

#pragma once

#include "synthdef_types.hpp"
#include <bit>

namespace synthdef {

template <typename T, size_t R, size_t C>
    requires std::is_trivially_copyable_v<T>
struct MatrixRef {
    static constexpr size_t Rows = R;
    static constexpr size_t Cols = C;
    T* data;
};

inline u64 nextPowerOfTwo(u64 n) {
    return std::bit_ceil(n);
}

template <typename T>
    requires std::is_floating_point_v<T>
T mod(T a, T b) {
    T r = std::fmod(a, b);
    return r < 0. ? r + b : r;
}

template <typename T>
constexpr T fold(T a, T b) {
    return b - std::abs(b - mod(a, 2*b));
}



template <typename T, usize N>
T sel(usize n, std::array<T,N> const& a) {
    return a[mod(n, N-1)];
}

template <typename T, usize Size>
void broadcast_scalar(T* dst, T const* src) {
    for (usize i = 0; i < Size; ++i) {
        dst[i] = src[0];
    }
}

template <typename T, usize Rows, usize Cols>
void broadcast_row(T* dst, T const* src) {
    for (usize i = 0, k = 0; i < Rows; ++i) {
        for (usize j = 0; j < Cols; ++j, ++k) {
            dst[j] = src[j];
        }
    }
}

template <typename T, usize Rows, usize Cols>
void broadcast_col(T* dst, T const* src) {
    for (usize i = 0, k = 0; i < Rows; ++i) {
        for (usize j = 0; j < Cols; ++j, ++k) {
            dst[k] = src[i];
        }
    }
}

template <typename T, usize Rows, usize Cols, usize OutRows>
    requires std::is_trivially_copyable_v<T>
void slice_rows(T* outdata, T const* indata, usize start_row) {
    start_row = std::min(start_row, Rows-OutRows);
    T const* in = indata + start_row * Cols;
    memcpy(outdata, in, OutRows * Cols * sizeof(T));
}

template <typename T, usize Rows, usize Cols, usize OutCols>
    requires std::is_trivially_copyable_v<T>
void slice_cols(T* outdata, T const* indata, usize start_col) {
    start_col = std::min(start_col, Cols-OutCols);
    T* end = outdata + Rows * OutCols;
    T* out = outdata;
    T const* in = indata + start_col;
    for (; out < end;  out += OutCols, in += Cols) {
        memcpy(out, in, OutCols * sizeof(T));
    }
}

template <typename T, usize Rows, usize Cols, usize OutRows, usize OutCols>
    requires std::is_trivially_copyable_v<T>
void slice(T* outdata, T const* indata, usize start_row, usize start_col) {
    start_row = std::min(start_row, Rows-OutRows);
    start_col = std::min(start_col, Cols-OutCols);
    T* end = outdata + OutRows * OutCols;
    T* out = outdata;
    T const* in = indata + start_row * Cols + start_col;
    for (; out < end;  out += OutCols, in += Cols) {
        memcpy(out, in, OutCols * sizeof(T));
    }
}

template <typename T, usize Rows, usize Cols, usize Skip, usize Keep, usize Stride>
void stride_slice_cols(T* outdata, T const* indata) {
    constexpr usize OutCols = std::min(Keep, (Cols - Skip + Stride - 1) / Stride);
    T* end = outdata + Rows * OutCols;
    T* out = outdata;
    T const* in = indata; 
    for (; out < end;  out += OutCols, in += Cols) {
        for (usize j = 0, k = Skip; j < OutCols; ++j, k += Stride) {
            out[j] = in[k];
        }
    }
}

template <typename T, usize Rows, usize Cols, usize Skip, usize Keep, usize Stride>
    requires std::is_trivially_copyable_v<T>
void stride_slice_rows(T* outdata, T const* indata) {
    constexpr usize OutRows = std::min(Keep, (Rows - Skip + Stride - 1) / Stride);
    T* end = outdata + OutRows * Cols;
    T* out = outdata;
    T const* in = indata + Skip * Cols; 
    for (; out < end;  out += Cols, in += Stride*Cols) {
        memcpy(out, in, Cols * sizeof(T));
    }
}

template <typename T, usize Rows, usize Cols, usize N>
    requires std::is_trivially_copyable_v<T>
void skip_cols(T* outdata, T const* indata) {
    constexpr usize OutCols = Cols - N;
    T* end = outdata + Rows * OutCols;
    T* out = outdata;
    T const* in = indata; 
    for (; out < end;  out += OutCols, in += Cols) {
        memcpy(out, in+N, OutCols * sizeof(T));
    }
}

template <typename T, usize Rows, usize Cols, usize N>
    requires std::is_trivially_copyable_v<T>
void skip_rows(T* outdata, T const* indata) {
    memcpy(outdata, indata + N * Cols, (Rows - N) * Cols * sizeof(T));
}

template <typename T, usize Rows, usize Cols, usize N>
    requires std::is_trivially_copyable_v<T>
void take_cols(T* outdata, T const* indata) {
    constexpr usize OutCols = N;
    T* end = outdata + Rows * OutCols;
    T* out = outdata;
    T const* in = indata; 
    for (; out < end;  out += OutCols, in += Cols) {
        memcpy(out, in, OutCols * sizeof(T));
    }
}

template <typename T, usize Rows, usize Cols, usize N>
    requires std::is_trivially_copyable_v<T>
void take_rows(T* outdata, T const* indata) {
    memcpy(outdata, indata, N * Cols * sizeof(T));
}

template <typename T, usize Rows, usize Cols, usize N>
void stride_cols(T* outdata, T const* indata) {
    constexpr usize OutCols = (Cols + N - 1) / N;
    T* end = outdata + Rows * OutCols;
    T* out = outdata;
    T const* in = indata; 
    for (; out < end;  out += OutCols, in += Cols) {
        for (usize j = 0, k = 0; j < OutCols; ++j, k += N) {
            out[j] = in[k];
        }
    }
}

template <typename T, usize Rows, usize Cols, usize N>
    requires std::is_trivially_copyable_v<T>
void stride_rows(T* outdata, T const* indata) {
    constexpr usize OutRows = (Rows + N - 1) / N;
    constexpr usize InRowStride = Cols * N;
    T* end = outdata + OutRows * Cols;
    T* out = outdata;
    T const* in = indata; 
    for (; out < end;  out += Cols, in += InRowStride) {
        memcpy(out, in, Cols * sizeof(T));
    }
}

template <typename T, usize Rows, usize Cols, usize N>
void stutter_cols(T* outdata, T const* indata) {
    T* end = outdata + Rows * Cols * N;
    T* out = outdata;
    T const* in = indata; 
    for (; out < end;  out += Cols*N, in += Cols) {
        for (usize j = 0, m = 0; j < Cols; ++j) {
            T value = in[j];
            for (usize k = 0; k < N; ++k, ++m) {
                out[m] = value;
            }
        }
    }
}

template <typename T, usize Rows, usize Cols, usize N>
    requires std::is_trivially_copyable_v<T>
void stutter_rows(T* outdata, T const* indata) {
    // This is exactly the same as cycle_cols. The actual difference is in the matrix dimensions of the output.
    T* end = outdata + Rows * Cols * N;
    T* out = outdata;
    T const* in = indata; 
    for (; out < end;  in += Cols) {
        for (usize j = 0; j < N; ++j, out += Cols) {
            memcpy(out, in, Cols * sizeof(T));
        }
    }
}

template <typename T, usize Rows, usize Cols>
    requires std::is_trivially_copyable_v<T>
void rotate_cols(T* outdata, T const* indata, isize nn) {
    usize n = mod(nn, isize(Cols));
    usize col_n = Cols - n;
    usize first = col_n * sizeof(T);
    usize second = n * sizeof(T);
    T* end = outdata + Rows * Cols;
    T* out = outdata;
    T const* in = indata; 
    for (; out < end;  out += Cols, in += Cols) {
        memcpy(out, in + n, first);
        memcpy(out + col_n, in, second);
    }
}

template <typename T, usize Rows, usize Cols>
    requires std::is_trivially_copyable_v<T>
void rotate_rows(T* outdata, T const* indata, isize nn) {
    usize n = mod(nn, isize(Rows));
    usize row_n = Rows - n;
    T* out = outdata;
    T const* in1 = indata + n * Cols;
    T const* in2 = indata;
    memcpy(out, in1, row_n * Cols * sizeof(T));
    memcpy(out + row_n * Cols, in2, n * Cols * sizeof(T));
}

template <typename T, usize Rows, usize Cols, isize N>
    requires std::is_trivially_copyable_v<T>
void cycle_cols(T* outdata, T const* indata) {
    // This is exactly the same as stutter_rows. The actual difference is in the matrix dimensions of the output.
    T* end = outdata + Rows * Cols * N;
    T* out = outdata;
    T const* in = indata; 
    for (; out < end;  in += Cols) {
        for (usize j = 0; j < N; ++j, out += Cols) {
            memcpy(out, in, Cols * sizeof(T));
        }
    }
}

template <typename T, usize Rows, usize Cols, isize N>
    requires std::is_trivially_copyable_v<T>
void cycle_rows(T* outdata, T const* indata) {
    T* end = outdata + Rows * Cols * N;
    T* out = outdata;
    for (; out < end;  out += Rows * Cols) {
        memcpy(out, indata, Rows * Cols * sizeof(T));
    }
}

template <typename T, usize Rows, usize Cols>
void transpose(T* outdata, T const* indata) {
    T* end = outdata + Rows * Cols;
    T* out = outdata;
    T const* in = indata; 
    for (; out < end;  out += Rows, in += 1) {
        for (usize j = 0; j < Rows; ++j) {
            out[j] = in[j * Cols];
        }
    }
}

template <typename T, usize Rows, usize Cols>
void reverse_cols(T* outdata, T const* indata) {
    T* end = outdata + Rows * Cols;
    T* out = outdata;
    T const* in = indata; 
    for (; out < end;  out += Cols, in += Cols) {
        for (usize j = 0, k = Cols - 1; j < Cols; ++j, --k) {
            out[j] = in[k];
        }
    }
}

template <typename T, usize Rows, usize Cols>
    requires std::is_trivially_copyable_v<T>
void reverse_rows(T* outdata, T const* indata) {
    T* end = outdata + Rows * Cols;
    T* out = outdata;
    T const* in = indata + (Rows - 1) * Cols; 
    for (; out < end;  out += Cols, in -= Cols) {
        memcpy(out, in, Cols * sizeof(T));
    }
}

template <typename T, typename I, usize TableSize, usize IndexSize>
void at(T* outdata, T const* indata, I const* index) {
    for (usize i = 0; i < IndexSize; ++i) {
        outdata[i] = indata[mod(index[i], TableSize)];
    }
}

template <usize A, usize B>
constexpr usize broadcast_dim() {
    static_assert(A == 1 || B == 1 || A == B);
    if constexpr (A == 1) return B;
    else if constexpr (B == 1 || A == B) return A;
    else return 9999; // unreachable
}

template <typename T, typename I, 
    usize ARows, usize ACols, 
    usize IRows, usize ICols, 
    usize VRows, usize VCols>
void put(T* outdata, T const* indata, I const* indices, T const* values) {
    static constexpr usize ASize = ARows * ACols;
    static constexpr usize PutRows = broadcast_dim<IRows, VRows>();
    static constexpr usize PutCols = broadcast_dim<ICols, VCols>();
    
    memcpy(outdata, indata, ASize * sizeof(T));
    for (usize i = 0; i < PutRows; ++i) {
        usize indexRowOffset = umod<IRows, PutRows>(i) * ICols;
        usize valueRowOffset = umod<VRows, PutRows>(i) * VCols;
        for (usize j = 0; j < PutCols; ++j) {
            usize indexOffset = umod<ICols, PutCols>(j) + indexRowOffset;
            usize valueOffset = umod<VCols, PutCols>(j) + valueRowOffset;
            usize index = indices[indexOffset];
            outdata[mod(index, ASize)] = values[valueOffset];
        }
    }
}


//template <typename T, typename I, usize TableSize, usize IndexSize>


// the index is a table of column indices.
template <typename T, typename I, usize Rows, usize Cols, usize IndexSize>
void permute_cols(T* outdata, T const* indata, I const* index) {
    usize OutCols = IndexSize;
    T* end = outdata + Rows * OutCols;
    T* out = outdata;
    T const* in = indata;
    for (; out < end; out += OutCols, in += Cols) {
        for (usize j = 0; j < IndexSize; ++j) {
            usize k = mod(index[j], I(Cols-1));
            out[j] = in[k];
        }
    }
}

// the index is a table of row indices.
template <typename T, typename I, usize Rows, usize Cols, usize IndexSize>
    requires std::is_trivially_copyable_v<T>
void permute_rows(T* outdata, T const* indata, I const* index) {
    T* out = outdata;
    T const* in = indata;
    for (usize j = 0; j < IndexSize; ++j, out += Cols) {
        usize k = mod(index[j], I(Rows-1));
        memcpy(out, in + k * Cols, Cols * sizeof(T));
    }
}


//template <typename T, usize Size> 
//void lace(T* outdata, T const* a, T const* b) {
//    for (usize i = 0; i < Size; ++i) {
//        outdata[2*i] = a[i];
//        outdata[2*i+1] = b[i];
//    }
//}

//template <typename T, usize ARows, usize ACols, usize AColOffset, usize BRows, usize BCols>
//    requires std::is_trivially_copyable_v<T>
//void cat_cols(T* a, T const* b) {
//    for (usize i = 0; i < ARows; ++i) {
//        memcpy(a + i * ACols + AColOffset, b + i * BCols, BCols * sizeof(T));
//    }
//}
//
//template <typename F, typename I, usize ARows, usize ACols, usize AColOffset, usize BRows, usize BCols>
//void cat_cols(F* a, I const* b) {
//    for (usize i = 0; i < ARows; ++i) {
//        for (usize j = 0; j < BCols; ++j) {
//            a[i * ACols + AColOffset + j] = static_cast<F>(b[i * BCols + j]);
//        }
//    }
//}

template <usize Cols, typename T, typename F> 
T reduce_cols(usize row, T const* in, F f) {
    T const* inp = in + row * Cols;
    T z = inp[0];
    for (usize col = 1; col < Cols; ++col) {
        z = f(z, inp[col]);
    } 
    return z;
}

template <usize Rows, usize Cols, typename T, typename F> 
T reduce_rows(usize col, T const* in, F f) {
    T const* inp = in + col;
    T z = inp[0];
    for (usize row = 1; row < Rows; ++row) {
        z = f(z, inp[row * Cols]);
    } 
    return z;
}

template <typename T, usize Rows, usize Middle, usize Cols>
void matrix_multiply(T const* a, T const* b, T const* c) {
    for (usize i = 0; i < Rows; ++i) {
        T* arow = a + i * Middle;
        T* crow = c + i * Cols;
        for (usize j = 0; j < Cols; ++j) {
            T sum = T(0);
            for (usize k = 0, m = j; k < Middle; ++k, m+=Cols) {
                sum += arow[k] * b[m];
            }
            crow[j] = sum;
        }
    }
}

// Helper to compute the total number of rows in variadic input
template <typename... Matrices>
constexpr size_t total_rows() {
    return (Matrices::Rows + ...);
}

// Helper to compute the total number of columns in variadic input
template <typename... Matrices>
constexpr size_t total_columns() {
    return (Matrices::Cols + ...);
}

// Variadic concatenation function
template <typename T, size_t Rows, size_t TotalCols, typename... Matrices>
    requires ((Matrices::Rows == 1 || Matrices::Rows == Rows) && ...) // Ensure broadcasting or matching rows
void cat_cols(T* result, const Matrices&... matrices) {
    static_assert(TotalCols == total_columns<Matrices...>(),
                  "The total columns must match the result matrix.");
    static_assert(((Matrices::Cols >= 1) && ...), "All matrices must have at least one column.");

    size_t col_offset = 0;
    ([&] {
        constexpr size_t input_rows = Matrices::Rows;
        constexpr size_t input_cols = Matrices::Cols;

        if constexpr (input_rows == Rows) {
            // Case: Matrix with matching rows
            for (size_t row = 0; row < Rows; ++row) {
                std::memcpy(
                    result + row * TotalCols + col_offset,
                    matrices.data + row * input_cols,
                    input_cols * sizeof(T)
                );
            }
        } else if constexpr (input_rows == 1) {
            // Case: Single row matrix, broadcasted to all rows
            for (size_t row = 0; row < Rows; ++row) {
                std::memcpy(
                    result + row * TotalCols + col_offset,
                    matrices.data,
                    input_cols * sizeof(T)
                );
            }
        }

        col_offset += input_cols;
    }(), ...);
}


// Variadic row concatenation function
template <typename T, size_t TotalRows, size_t Cols, typename... Matrices>
    requires ((Matrices::Cols == 1 || Matrices::Cols == Cols) && ...) // Ensure broadcasting or matching columns
void cat_rows(T* result, const Matrices&... matrices) {
    static_assert(TotalRows == total_rows<Matrices...>(),
                  "The total rows must match the result matrix.");
    static_assert(((Matrices::Rows >= 1) && ...), "All matrices must have at least one row.");

    size_t row_offset = 0;
    ([&] {
        constexpr size_t input_rows = Matrices::Rows;
        constexpr size_t input_cols = Matrices::Cols;

        if constexpr (input_cols == Cols) {
            // Case: Matrix with matching columns
            for (size_t row = 0; row < input_rows; ++row) {
                std::memcpy(
                    result + (row + row_offset) * Cols,
                    matrices.data + row * input_cols,
                    Cols * sizeof(T)
                );
            }
        } else if constexpr (input_cols == 1) {
            // Case: Single column matrix, broadcasted to all columns
            for (size_t row = 0; row < input_rows; ++row) {
                for (size_t col = 0; col < Cols; ++col) {
                    result[(row + row_offset) * Cols + col] = matrices.data[row];
                }
            }
        }

        row_offset += input_rows;
    }(), ...);
}

template <typename T, size_t Rows, size_t Cols, typename... Matrices>
    requires ((true && ... && (Matrices::Rows == 1 || Matrices::Rows == Rows)) &&  // Ensure all matrices have compatible rows
              (true && ... && (Matrices::Cols == 1 || Matrices::Cols == Cols)))    // Ensure all matrices have compatible columns
void lace_cols(T* result, const Matrices&... matrices) {
    static_assert(sizeof...(Matrices) > 0, "At least one matrix must be provided.");

    constexpr size_t num_matrices = sizeof...(Matrices);
    //constexpr size_t output_cols = Cols * num_matrices;
    
    size_t column_offset = 0;
    // Interlace columns from each matrix
    ([&] {
        constexpr size_t input_rows = Matrices::Rows;
        constexpr size_t input_cols = Matrices::Cols;
        printf("M %zu %zu\n", input_rows, input_cols);
        
        if constexpr (input_rows == 1) {
            if constexpr (input_cols == 1) {
                T val = matrices.data[0];
                
                for (size_t row = 0, dst_pos = column_offset; row < Rows; ++row) {
                    for (size_t col = 0; col < Cols; ++col, dst_pos += num_matrices) {
                        result[dst_pos] = val;
                    }
                }
            } else {
                for (size_t row = 0, dst_pos = column_offset; row < Rows; ++row) {
                    for (size_t col = 0; col < Cols; ++col, dst_pos += num_matrices) {
                        result[dst_pos] = matrices.data[col];
                    }
                }
            }
        } else {
            if constexpr (input_cols == 1) {
                for (size_t row = 0, dst_pos = column_offset; row < Rows; ++row) {
                    T val = matrices.data[row];
                    for (size_t col = 0; col < Cols; ++col, dst_pos += num_matrices) {
                        result[dst_pos] = val;
                    }
                }
            } else {
                for (size_t dst_pos = column_offset, src_pos = 0; src_pos < Rows*Cols; 
                    dst_pos += num_matrices, ++src_pos) 
                {
                    result[dst_pos] = matrices.data[src_pos];
                }
            }
        }
        ++column_offset;
    }(), ...);
}
// Variadic interlace rows function
template <typename T, size_t Rows, size_t Cols, typename... Matrices>
    requires ((true && ... && (Matrices::Rows == 1 || Matrices::Rows == Rows)) &&  // Ensure all matrices have compatible rows
              (true && ... && (Matrices::Cols == 1 || Matrices::Cols == Cols)))    // Ensure all matrices have compatible columns
void lace_rows(T* result, const Matrices&... matrices) {
    static_assert(sizeof...(Matrices) > 0, "At least one matrix must be provided.");

    constexpr size_t num_matrices = sizeof...(Matrices);
    constexpr size_t row_stride = Cols * num_matrices;
    
    size_t row_offset = 0;
    // Interlace rows from each matrix
    ([&] {
        constexpr size_t input_rows = Matrices::Rows;
        constexpr size_t input_cols = Matrices::Cols;
        printf("M %zu %zu : ro %zu\n", input_rows, input_cols, row_offset);
        
        if constexpr (input_rows == 1) {
            if constexpr (input_cols == 1) {
                T val = matrices.data[0];
                
                for (size_t row = 0, dst_pos = row_offset; row < Rows; ++row, dst_pos += row_stride) {
                    T* outp = result + dst_pos;
                    for (size_t col = 0; col < Cols; ++col) {
                        outp[col] = val;
                    }
                }
            } else {
                for (size_t row = 0, dst_pos = row_offset; row < Rows; ++row, dst_pos += row_stride) {
                    memcpy(result+dst_pos, matrices.data, Cols * sizeof(T));
                }
            }
        } else {
            if constexpr (input_cols == 1) {
                for (size_t row = 0, dst_pos = row_offset; row < Rows; ++row, dst_pos += row_stride) {
                    T val = matrices.data[row];
                    T* outp = result + dst_pos;
                    for (size_t col = 0; col < Cols; ++col) {
                        outp[col] = val;
                    }
                }
            } else {
                for (size_t row = 0, dst_pos = row_offset, src_pos = 0; row < Rows; 
                    ++row, dst_pos += row_stride, src_pos += Cols) {
                    memcpy(result+dst_pos, matrices.data+src_pos, Cols * sizeof(T));
                }
            }
        }
        row_offset += Cols;
    }(), ...);
}




} // namespace synthdef

