//
//  synthdef_matrix.hpp
//  synthdef-compiler
//
//  Created by James McCartney on 3/19/24.
//

#pragma once
#include "synthdef_signal_type.hpp"
#include <variant>
#include "synthdef_math_ops.hpp"
#include "synthdef_expr.hpp"
#include "jscs_matrix_transform.hpp"
#include "synthdef_str_util.hpp"

namespace synthdef {

template<typename T>
void extend_to_next_power_of_two(std::vector<T>& vec) {
    // Get current size
    size_t current_size = vec.size();
    
    // If empty, no need to change
    if (current_size == 0) {
        return;
    }
    
    // Calculate next power of 2 greater than or equal to the current size
    // std::bit_ceil gives the smallest power of 2 >= n
    size_t next_power_of_two = std::bit_ceil(current_size);
    
    // Resize the vector with default value of T (0 for numeric types)
    vec.resize(next_power_of_two);
}

template<typename T>
struct VectorT {
    vector<T> values;
    
    VectorT() : values(1, 0) {}
    VectorT(T value) : values(1, value) {}
    VectorT(vector<T> inValues) 
        : values(std::move(inValues)) 
        {
            extend_to_next_power_of_two(values);
        }
    VectorT(usize chans, T value) 
        : values(asChans(chans), value)
        {}

    bool operator==(VectorT const& other) const noexcept {
        return values == other.values;
    }

    usize size() const noexcept { return values.size(); }
    T& operator[](usize index) noexcept { return values[synthdef::mod(index, size())]; }
    T const& operator[](usize index) const noexcept { return values[synthdef::mod(index, size())]; }

    bool is_scalar() const noexcept { return size() == 1; }
    bool is_vector() const noexcept { return size() > 1; }
    
    T& at(usize chan) noexcept { 
        return values[chan & (size()-1)]; 
    }
    T const& at(usize chan) const noexcept {
        return values[chan & (size()-1)]; 
    }
    
    i64 i64_at(usize chan) const { 
        return static_cast<i64>(at(chan)); 
    }
    f64 f64_at(usize chan) const { 
        return static_cast<f64>(at(chan)); 
    }
    
    void resize(usize size, T value) {
        values.resize(size, value);
    }
    
    string str() const {
        if (size() == 1) {
            return synthdef::ftos(values[0]);
        } else {
            string s = "{";
            for (usize i = 0; i < size(); ++i) {
                if (i > 0) { s += ", "; }
                s += synthdef::ftos(values[i]);
            }
            s += "}";
            return s;
        }
    }
    string str(NumType type) const {
        auto scalarToStr = [=](T x) -> string {
            switch (type.flags) {
                case I32: return synthdef::ftos(i32(x));
                case I64: return synthdef::ftos(i64(x));
                case F32: return synthdef::ftos(f32(x));
                case F64: return synthdef::ftos(f64(x));
                default:  return synthdef::ftos(x);
            }
        };
        if (size() == 1) {
            return scalarToStr(values[0]);
        } else {
            string s = "{";
            for (usize i = 0; i < size(); ++i) {
                if (i > 0) { s += ", "; }
                s += scalarToStr(values[i]);
            }
            s += "}";
            return s;
        }
    }
    u64 hash() const {
        return hash64(values.size() * sizeof(T), values.data());
    }

#if 0    
    auto slice(usize skip, usize keep, usize stride) const {
        usize new_rows;
        new_rows = skip > shape.rows ? 0 : shape.rows - skip;
        new_rows = std::min(new_rows, keep);
        new_rows = (new_rows + stride - 1) / stride;

        vector<T> new_values;
        new_values.reserve(new_rows * shape.cols);
        for (usize i = skip; i < skip + keep; i += stride) {
            for (usize j = 0; j < shape.cols; ++j) {
                new_values.push_back(at(i, j));
            }
        }
        return VectorT<T>({new_rows, shape.cols}, std::move(new_values));
    }
    
    auto skip(usize n) const {
        return slice(n, shape.rows - n, 1);
    }
    auto take(usize n) const {
        return slice(0, n, 1);
    }
    auto stride(usize n) const {
        slice(0, shape.rows, n);
    }
    auto stutter(usize n) const {
        n = asChans(n);
        vector<T> new_values;
        new_values.reserve(n * size());
        for (usize i = 0; i < shape.rows; ++i) {
            for (usize k = 0; k < n; ++k) {
                for (usize j = 0; j < shape.cols; ++j) {
                    new_values.push_back(at(i, j));
                }
            }
        }
        return VectorT<T>(shape.scale(n, Row), std::move(new_values));
    }
    auto cyc(usize n) const {
        n = asChans(n);
        usize new_size = n * size();
        vector<T> new_values;
        new_values.reserve(new_size);
        for (usize i = 0; i < n; ++i) {
        for (usize j = 0; j < size(); ++j) {
            new_values.push_back(at(j));
        }
        return VectorT<T>(std::move(new_values));
    }
    auto reverse(usize rows = 1) const {
        vector<T> new_values;
        new_values.reserve(chans);
        usize cols = size() / rows;
        for (usize i = rows; i > 0; --i) {
            for (usize j = 0; j < shape.cols; ++j) {
                new_values.push_back(at(i-1, j));
            }
        }
        return VectorT<T>(shape, std::move(new_values));
    }
    template <typename I>
    auto permute(VectorT<I> const& index) const {
        Shape new_shape(index.size(), shape.cols);
        vector<T> new_values;
        new_values.reserve(new_shape.size());
        for (usize row = 0; row < new_shape.rows; ++row) {
            usize r = std::clamp(usize(index.at(row)), usize(0), shape.rows);
            for (usize col = 0; col < new_shape.cols; ++col) {
                new_values.push_back(at(r, col));
            }
        }
        return VectorT<T>(new_shape, std::move(new_values));
    }
    
    auto rotate(isize n) const {
        n = mod(n, isize(shape.rows));
        vector<T> new_values;
        new_values.reserve(chans);
        for (usize i = n; i < shape.rows; ++i) {
            for (usize j = 0; j < shape.cols; ++j) {
                new_values.push_back(at(i, j));
            }
        }
        for (usize i = 0; i < n; ++i) {
            for (usize j = 0; j < shape.cols; ++j) {
                new_values.push_back(at(i, j));
            }
        }
        return VectorT<T>(shape, std::move(new_values));
    }
    auto transpose() const {
        vector<T> new_values;
        new_values.reserve(chans);
        for (usize j = 0; j < shape.cols; ++j) {
            for (usize i = 0; i < shape.rows; ++i) {
                new_values.push_back(at(i, j));
            }
        }
        return VectorT<T>(shape.transpose(), std::move(new_values));
    }
    auto reshape(usize new_rows, usize new_cols) const {
        if (new_rows * new_cols != chans) {
            throw std::runtime_error("Invalid reshape");
        }
        return VectorT<T>({new_rows, new_cols}, values);
    }
    
    auto flat(Axis axis) const {
        switch (axis) {
            case Row: {
                return reshape(1, size());
            }
            case Col: {
                return reshape(size(), 1);
            }
        }
    }
    
    template <typename W>
    auto at(VectorT<W> const& that) const {
        usize len = that.size();
        vector<T> new_values;
        new_values.reserve(len);
        for (usize i = 0; i < len; ++i) {
            new_values.push_back(at(that[i]));
        }
        return VectorT<T>(that.shape, std::move(new_values));
    }
    template <typename W>
    auto put(VectorT<W> const& index, VectorT<T> newValues) const {
        Shape rhsShape = index.shape.broadcast(newValues.shape);
        vector<T> newVec = values;
        for (usize i = 0; i < rhsShape.rows; ++i) {
            for (usize j = 0; j < rhsShape.cols; ++j) {
                newVec[index.i64_at(i,j)] = newValues.at(i,j);
            }
        }
        return VectorT<T>(shape, std::move(newVec));
    }
    template <typename W>
    auto cat(VectorT<W> const& that) const { 
        using U = std::common_type_t<T, W>;
        vector<U> new_values;
        switch (axis) {
            case Row: {
                Shape new_shape = shape.cat(that.shape, Row);
                new_values.reserve(new_shape.size());

                for (usize i = 0; i < shape.rows; ++i) {
                    for (usize j = 0; j < new_shape.cols; ++j) {
                        new_values.push_back((U)at(i, j));
                    }
                }
                for (usize i = 0; i < that.shape.rows; ++i) {
                    for (usize j = 0; j < new_shape.cols; ++j) {
                        new_values.push_back((U)that.at(i, j));
                    }
                }

                return VectorT<U>(new_shape, std::move(new_values));
            }
            case Col: {
                Shape new_shape = shape.cat(that.shape, Col);
                new_values.reserve(new_shape.size());
                for (usize i = 0; i < new_shape.rows; ++i) {
                    for (usize j = 0; j < shape.cols; ++j) {
                        new_values.push_back((U)at(i, j));
                    }
                    for (usize j = 0; j < that.shape.cols; ++j) {
                        new_values.push_back((U)that.at(i, j));
                    }
                }
                return VectorT<U>(new_shape, std::move(new_values));
            }
        }
    }
    template <typename W>
    auto lace(VectorT<W> const& that) const {
        Shape loop_shape;
        try {
            loop_shape = shape.broadcast(that.shape);
        } catch (std::runtime_error const& e) {
            throw std::runtime_error(std::format("lace: incompatible shapes. {}", e.what()));
        }
        
        using U = std::common_type_t<T, W>;
        vector<U> new_values;
        new_values.reserve(2 * loop_shape.size());
        switch (axis) {
            case Row: {
                for (usize i = 0; i < loop_shape.rows; ++i) {
                    for (usize j = 0; j < loop_shape.cols; ++j) {
                        new_values.push_back((U)at(i, j));
                    }
                    for (usize j = 0; j < loop_shape.rows; ++j) {
                        new_values.push_back((U)that.at(i, j));
                    }
                }
                return VectorT<U>(loop_shape.scale(2, Row), std::move(new_values));
            }
            case Col: {
                for (usize i = 0; i < loop_shape.rows; ++i) {
                    for (usize j = 0; j < loop_shape.cols; ++j) {
                        new_values.push_back((U)at(i, j));
                        new_values.push_back((U)that.at(i, j));
                    }
                }
                return VectorT<U>(loop_shape.scale(2, Col), std::move(new_values));
            }
        }
    }

    template <typename F>
    auto scan(Axis axis, F f) const {
        switch (axis) {
            case Col: {
                vector<T> new_values;
                new_values.reserve(chans);
                for (usize i = 0; i < shape.rows; ++i) {
                    T acc = at(i, 0);
                    new_values.push_back(acc);
                    for (usize j = 1; j < shape.cols; ++j) {
                        acc = f(acc, at(i, j));
                        new_values.push_back(acc);
                    }
                }
                return VectorT<T>(shape, std::move(new_values));
            }
            case Row: {
                VectorT<T> out(shape, 0);
                for (usize j = 0; j < shape.cols; ++j) {
                    T acc = at(0, j);
                    out.at(0, j) = acc;
                    for (usize i = 1; i < shape.rows; ++i) {
                        acc = f(acc, at(i, j));
                        out.at(i, j) = acc;
                    }
                }
                return out;
            }
        }
    }
#endif
    template <typename F>
    auto reduce(usize cols, F f) const {
        vector<T> new_values;
        new_values.reserve(cols);
        usize rows = size() / cols;
        for (usize j = 0; j < cols; ++j) {
            T acc = at(j);
            for (usize i = 1; i < rows; ++i) {
                acc = f(acc, at(i*cols + j));
            }
            new_values.push_back(acc);
        }
        return VectorT<T>(std::move(new_values));
    }
    
    static VectorT iota(usize n, T first, T step) {
        n = asChans(n);
        vector<T> values;
        values.reserve(n);
        for (usize i = 0; i < n; ++i) {
            values.push_back(first + i * step);
        }
        return VectorT(std::move(values));
    }
    
    static VectorT geo(usize n, T first, T grow) {
        vector<T> values;
        n = asChans(n);
        values.reserve(n);
        T cur = first;
        for (usize i = 0; i < n; ++i) {
            values.push_back(cur);
            cur *= grow;
        }
        return VectorT(std::move(values));
    }
    
    static VectorT linspace(usize n, T first, T last) {
        vector<T> values;
        n = asChans(n);
        values.reserve(n);
        T step = (last - first) / (n - 1);
        values.push_back(first);
        for (usize i = 1; i < n-1; ++i) {
            values.push_back(first + i * step);
        }
        values.push_back(last);
        return VectorT(std::move(values));
    }
    
    static VectorT logspace(usize n, T first, T last) {
        vector<T> values;
        n = asChans(n);
        values.reserve(n);
        T grow = std::pow(last / first, 1. / T(n - 1));
        values.push_back(first);
        for (usize i = 1; i < n-1; ++i) {
            values.push_back(first * std::pow(grow, T(i)));
        }
        values.push_back(last);
        return VectorT(std::move(values));
    }
};

struct Constant : Expr {
    variant<VectorT<i64>, VectorT<f64>> value;
    NumType init_type = NumType::any;
    
    Constant(VectorT<i64> const& value, NumType init_type = NumType::any) 
        : Expr(constSignalRate, {}), value(value), init_type(init_type) {}
    Constant(VectorT<f64> const& value, NumType init_type = NumType::any_float) 
        : Expr(constSignalRate, {}), value(value), init_type(init_type) {}
    
    Constant(i64 value, NumType init_type = NumType::any) 
        : Expr(constSignalRate, {}), value(VectorT<i64>(value)), init_type(init_type) {}
    Constant(f64 value, NumType init_type = NumType::any_float) 
        : Expr(constSignalRate, {}), value(VectorT<f64>(value)), init_type(init_type) {}
    
    Constant(vector<i64> inValues, NumType init_type = NumType::any) 
        : Expr(constSignalRate, {}), value(std::move(inValues)), init_type(init_type) {}
    Constant(vector<f64> inValues, NumType init_type = NumType::any_float) 
        : Expr(constSignalRate, {}), value(std::move(inValues)), init_type(init_type) {}
                
    Constant(Constant const& that) = default;
    
    Constant& operator=(VectorT<i64> value) { this->value = value; return *this; }
    Constant& operator=(VectorT<f64> value) { this->value = value; return *this; }
    Constant& operator=(Constant const& that) { value = that.value; return *this; }

    string typeName() const override { return "Constant"; }

    bool equals_(Expr const& that) const override {
        auto& c = static_cast<Constant const&>(that);
        return size() == c.size() && value == c.value && init_type == c.init_type;
    }
    bool is_constant() const override { return true; }
    bool is_scalar_constant() const override { return size() == 1; }
    optional<f64> get_scalar() const noexcept override { 
        return is_scalar_constant() ? optional<f64>(f64_at(0)) : std::nullopt; 
    }
    optional<i64> get_scalar_int() const noexcept override { 
        return is_scalar_constant() ? optional<i64>(i64_at(0)) : std::nullopt; 
    }

    NumType initial_type() const override { return init_type; }
    
    void update_type(ExprIdentitySet& worklist) override {}
    void calcShape() override { chans = std::visit([](auto&& arg) { return arg.size(); }, value); }
    
    bool is_f64() const {
        return std::holds_alternative<VectorT<f64>>(value);
    }
    bool is_i64() const {
        return std::holds_alternative<VectorT<i64>>(value);
    }

    i64 i64_at(usize i) const {
        return std::visit([i](auto&& arg) { return arg.i64_at(i); }, value);
    }
    f64 f64_at(usize i) const {
        return std::visit([i](auto&& arg) { return arg.f64_at(i); }, value);
    }
    
    string str() const override {
        return std::visit([this](auto&& arg) { return arg.str(type); }, value);
    }

    u64 hash() const override {
        return std::visit([](auto&& arg) { return arg.hash(); }, value);
    }
     usize size() const noexcept { 
        return std::visit([](auto&& arg) { return arg.size(); }, value); 
    }

#if 0
    Constant* skip(usize n) const { 
        return std::visit([n](auto&& arg) { return new Constant{arg.skip(n)}; }, value); 
    }
    Constant* take(usize n) const { 
        return std::visit([n](auto&& arg) { return new Constant{arg.take(n)}; }, value); 
    }
    Constant* stride(usize n) const { 
        return std::visit([n](auto&& arg) { return new Constant{arg.stride(n)}; }, value); 
    }
    Constant* stutter(usize n) const { 
        return std::visit([n](auto&& arg) { return new Constant{arg.stutter(n)}; }, value); 
    }
    Constant* cyc(usize n) const { 
        return std::visit([n](auto&& arg) { return new Constant{arg.cyc(n)}; }, value); 
    }
    Constant* reverse(Axis axis = Col) const { 
        return std::visit([](auto&& arg) { return new Constant{arg.reverse(axis)}; }, value); 
    }
    Constant* rotate(isize n) const { 
        return std::visit([n](auto&& arg) { return new Constant{arg.rotate(n)}; }, value); 
    }
    Constant* transpose() const { 
        return std::visit([](auto&& arg) { return new Constant{arg.transpose()}; }, value); 
    }
    Constant* reshape(usize rows, usize cols) const { 
        return std::visit([rows, cols](auto&& arg) { return new Constant{arg.reshape(rows, cols)}; }, value); 
    }
    Constant* flat(Axis axis = Col) const {
        return std::visit([](auto&& arg) { return new Constant{arg.flat(axis)}; }, value); 
    }
    Constant* at(Constant const* indices) const;
    Constant* put(Constant const* indices, Constant const* values) const;   
    
    Constant* permute(Constant const* index) const;

    static Constant* cat(std::vector<Constant*> in) {
        if (in.size() == 0) {
            throw std::runtime_error("cat: empty input");
        }
        if (in.size() == 1) {
            return in[0];
        }
        usize chans;
        bool is_f64 = false;
        for (Constant* k : in) {
            chans += k->chans;
            if (k->is_f64()) { is_f64 = true; }
        }
        if (is_f64) {
            vector<f64> new_values;
            new_values.reserve(chans);
            if (axis == Row) {
                for (Constant* k : in) {
                    auto arg = k->value;
                    std::visit([&new_values](auto&& arg) {
                        for (usize i = 0; i < arg.size(); ++i) {
                            new_values.push_back(arg.f64_at(i));
                        }
                    }, arg);
                }
            } else {
                new_values.resize(chans);
                for (Constant* k : in) {
                    auto arg = k->value;
                    usize cur_col = 0;
                    std::visit([&new_values, &cur_col, shape](auto&& arg) {
                        for (usize row = 0, i = 0; row < arg.shape.rows; ++row) {
                            usize out_row_offset = row * shape.cols + cur_col;
                            for (usize col = 0; col < arg.shape.cols; ++col, ++i) {
                                new_values[out_row_offset + col] = arg.f64_at(i);
                            }
                        }
                    }, arg);
                }
            }
            return new Constant{shape, std::move(new_values)};
        } else {
            vector<i64> new_values;
            new_values.reserve(chans);
            if (axis == Row) {
                for (Constant* k : in) {
                    auto arg = k->value;
                    std::visit([&new_values](auto&& arg) {
                        for (usize i = 0; i < arg.size(); ++i) {
                            new_values.push_back(arg.f64_at(i));
                        }
                    }, arg);
                }
            } else {
                new_values.resize(chans);
                for (Constant* k : in) {
                    auto arg = k->value;
                    usize cur_col = 0;
                    std::visit([&new_values, &cur_col, shape](auto&& arg) {
                        for (usize row = 0, i = 0; row < arg.shape.rows; ++row) {
                            usize out_row_offset = row * shape.cols + cur_col;
                            for (usize col = 0; col < arg.shape.cols; ++col, ++i) {
                                new_values[out_row_offset + col] = arg.f64_at(i);
                            }
                        }
                    }, arg);
                }
            }
            return new Constant{shape, std::move(new_values)};
        }
    }
    
    Constant* cat(Constant const* that) const { 
        return std::visit(overloaded {
            [](VectorT<i64> const& arg_a, VectorT<i64> const& arg_b) { return new Constant{arg_a.cat(arg_b)}; },
            [](VectorT<i64> const& arg_a, VectorT<f64> const& arg_b) { return new Constant{arg_a.cat(arg_b)}; },
            [](VectorT<f64> const& arg_a, VectorT<i64> const& arg_b) { return new Constant{arg_a.cat(arg_b)}; },
            [](VectorT<f64> const& arg_a, VectorT<f64> const& arg_b) { return new Constant{arg_a.cat(arg_b)}; }
        }, value, that->value);
    }
    Constant* lace(Constant const* that) const { 
        return std::visit(overloaded {
            [](VectorT<i64> const& arg_a, VectorT<i64> const& arg_b) { return new Constant{arg_a.lace(arg_b)}; },
            [](VectorT<i64> const& arg_a, VectorT<f64> const& arg_b) { return new Constant{arg_a.lace(arg_b)}; },
            [](VectorT<f64> const& arg_a, VectorT<i64> const& arg_b) { return new Constant{arg_a.lace(arg_b)}; },
            [](VectorT<f64> const& arg_a, VectorT<f64> const& arg_b) { return new Constant{arg_a.lace(arg_b)}; }
        }, value, that->value);
    }
#endif
    
    Constant* ushr(Constant const* that) const;

    Constant* abs() const;
    Constant* clz() const;
    Constant* ctz() const;
    Constant* popcount() const;
    Constant* numbits() const;
    Constant* floor() const;
    Constant* ceil() const;
    Constant* round() const;
    Constant* trunc() const;
    Constant* sqrt() const;
    Constant* cbrt() const;
    Constant* log() const;
    Constant* log2() const;
    Constant* log10() const;
    Constant* log1p() const;
    Constant* exp() const;
    Constant* exp2() const;
    Constant* exp10() const;
    Constant* expm1() const;
    Constant* sin() const;
    Constant* cos() const;
    Constant* tan() const;
    Constant* asin() const;
    Constant* acos() const;
    Constant* atan() const;
    Constant* sinh() const;
    Constant* cosh() const;
    Constant* tanh() const;
    Constant* asinh() const;
    Constant* acosh() const;
    Constant* atanh() const;
    Constant* erf() const;
    Constant* erfc() const;

    Constant* pow(Constant const* that) const;
    Constant* atan2(Constant const* that) const;
    Constant* hypot(Constant const* that) const;
    Constant* copysign(Constant const* that) const;

    Constant* eq(Constant const* that) const;
    Constant* ne(Constant const* that) const;
    Constant* lt(Constant const* that) const;
    Constant* le(Constant const* that) const;
    Constant* gt(Constant const* that) const;
    Constant* ge(Constant const* that) const;

    bool operator==(Constant const& that) const {
        return value == that.value;
    }

    void accept(ExprVisitor& visitor) override;
};

Constant* unary_op(Constant const* a, UnaryOp op);
Constant* binary_op(Constant const* a, Constant const* b, BinaryOp op);
Constant* compare_op(Constant const* a, Constant const* b, CompareOp op);
Constant* reduce(Constant const* a, usize rows, BinaryOp op);
Constant* scan(Constant const* a, usize rows, BinaryOp op);
Constant* to_int(Constant const* a);
Constant* to_float(Constant const* a);

//Constant* matrix_multiply(Constant const* a, Constant const* b);

std::pair<Constant*, Constant*> sincos(Constant const* a);

string scalar_string(Constant const* a);

} // namespace synthdef
