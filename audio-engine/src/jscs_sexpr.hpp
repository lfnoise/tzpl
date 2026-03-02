//
//  jscs_sexpr.hpp
//  audio engine
//
//  Created by James McCartney on 8/3/25.
//

#ifndef jscs_sexpr_hpp
#define jscs_sexpr_hpp

#include <variant>
#include <string>
#include <vector>
#include <cstdint>
#include <unordered_map>
#include <functional>
#include <expected>

namespace sexpr {

struct Symbol {
    std::string name;
    
    Symbol(std::string name) : name(name) {}
    
    auto operator<=>(Symbol const& that) {
        return name <=> that.name;
    }
};

struct Item;

using ItemVec = std::vector<Item>;

struct Item {
    std::variant<bool, int64_t, double, Symbol, std::string, ItemVec> value;
    
    Item() = default;
    Item(Symbol symbol) : value(std::move(symbol)) {}
    Item(const char* s) : value(std::string(s)) {}
    Item(std::string string) : value(std::move(string)) {}
    Item(bool b) : value(b) {}
    template <std::floating_point T>
    Item(T number) : value(double(number)) {}
    template <std::integral T>
    Item(T number) : value(int64_t(number)) {}
    Item(ItemVec list) : value(std::move(list)) {}

    template<typename T>
    bool is() const {
        return std::holds_alternative<T>(value);
    }
    
    template<typename T>
    const T& get() const {
        return std::get<T>(value);
    }

    std::string to_string() const {
        return std::visit([](const auto& val) -> std::string {
            using T = std::decay_t<decltype(val)>;
            if constexpr (std::is_same_v<T, std::string>) {
                return "\"" + val + "\"";
            } else if constexpr (std::is_same_v<T, Symbol>) {
                return val.name;
            } else if constexpr (std::is_same_v<T, bool>) {
                return val ? "true" : "false";
            } else if constexpr (std::is_same_v<T, double>) {
                return std::to_string(val);
            } else if constexpr (std::is_same_v<T, int64_t>) {
                return std::to_string(val);
            } else if constexpr (std::is_same_v<T, ItemVec>) {
                std::string result = "(";
                for (size_t i = 0; i < val.size(); ++i) {
                    if (i > 0) result += " ";
                    result += val[i].to_string();
                }
                result += ")";
                return result;
            }
        }, value);
    }
    
    std::vector<uint8_t> to_binary() const;
    
};

using ItemResult = std::expected<Item, std::string>;

// Main parse function
ItemResult parse_sexpr(std::string const& input);
ItemResult sexpr_from_binary(std::vector<uint8_t> const& buf);

}

void test_sexpr();

#endif /* jscs_sexpr_hpp */
