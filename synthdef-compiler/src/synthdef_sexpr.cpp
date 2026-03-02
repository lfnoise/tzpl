//
//  synthdef_sexpr.cpp
//  synthdef-compiler
//
//  Created by James McCartney on 8/3/25.
//

#include "synthdef_sexpr.hpp"
#include <iostream>

namespace sexpr {

using StringResult = std::expected<std::string, std::string>;
using ItemVecResult = std::expected<ItemVec, std::string>;
using SymbolResult = std::expected<Symbol, std::string>;

class SExprParser {
private:
    std::string const& input;
    size_t pos;
    
    void skip_whitespace() {
        while (pos < input.length() && std::isspace(input[pos])) {
            pos++;
        }
    }
    
    bool is_delimiter(char c) {
        return c == '(' || c == ')' || c == '[' || c == ']' || 
               c == '{' || c == '}' || c == ',' || c == ';' || 
               std::isspace(c);
    }
    
    StringResult parse_string() {
        if (input[pos] != '"') {
            return std::unexpected("Expected '\"' at position " + std::to_string(pos));
        }
        pos++; // skip opening quote
        
        std::string result;
        while (pos < input.length() && input[pos] != '"') {
            if (input[pos] == '\\' && pos + 1 < input.length()) {
                pos++; // skip backslash
                switch (input[pos]) {
                    case 'n': result += '\n'; break;
                    case 't': result += '\t'; break;
                    case 'r': result += '\r'; break;
                    case '\\': result += '\\'; break;
                    case '"': result += '"'; break;
                    default: result += input[pos]; break;
                }
            } else {
                result += input[pos];
            }
            pos++;
        }
        
        if (pos >= input.length()) {
            return std::unexpected("Unterminated string");
        }
        
        pos++; // skip closing quote
        return result;
    }
    
    ItemResult parse_number() {
        size_t start = pos;
        bool is_float = false;
        
        if (input[pos] == '-' || input[pos] == '+') {
            pos++;
        }
        
        bool has_digits = false;
        while (pos < input.length() && std::isdigit(input[pos])) {
            pos++;
            has_digits = true;
        }
        
        if (pos < input.length() && input[pos] == '.') {
            is_float = true;
            pos++;
            while (pos < input.length() && std::isdigit(input[pos])) {
                pos++;
                has_digits = true;
            }
        }
        
        if (!has_digits) {
            throw std::runtime_error("Invalid number format at position " + std::to_string(start));
        }
        
        // Handle scientific notation
        if (pos < input.length() && (input[pos] == 'e' || input[pos] == 'E')) {
            is_float = true;
            pos++;
            if (pos < input.length() && (input[pos] == '+' || input[pos] == '-')) {
                pos++;
            }
            if (pos >= input.length() || !std::isdigit(input[pos])) {
                throw std::runtime_error("Invalid scientific notation at position " + std::to_string(start));
            }
            while (pos < input.length() && std::isdigit(input[pos])) {
                pos++;
            }
        }
        
        std::string num_str = input.substr(start, pos - start);
        if (is_float) {
            return std::stod(num_str);
        } else {
            return std::stol(num_str);
        }
    }
    
    SymbolResult parse_symbol() {
        size_t start = pos;
        
        while (pos < input.length() && 
               std::isprint(input[pos]) && 
               !is_delimiter(input[pos])) {
            pos++;
        }
        
        if (start == pos) {
            return std::unexpected("Empty symbol at position " + std::to_string(pos));
        }
        
        return Symbol(input.substr(start, pos - start));
    }
    
    ItemVecResult parse_list() {
        if (input[pos] != '(') {
            throw std::runtime_error("Expected '(' at position " + std::to_string(pos));
        }
        pos++; // skip opening paren
        
        ItemVec result;
        
        skip_whitespace();
        while (pos < input.length() && input[pos] != ')') {
            ItemResult item = parse_item();
            if (!item) return std::unexpected(item.error());
            // continue working with item
            result.push_back(item.value());
            skip_whitespace();
        }
        
        if (pos >= input.length()) {
            throw std::runtime_error("Unterminated list");
        }
        
        pos++; // skip closing paren
        return result;
    }
    
    ItemResult parse_item() {
        skip_whitespace();
        
        if (pos >= input.length()) {
            throw std::runtime_error("Unexpected end of input");
        }
        
        char c = input[pos];
        
        if (c == '"') {
            return parse_string().transform([](std::string&& s) { return Item(std::move(s)); });
        } else if (c == '(') {
            return parse_list().transform([](ItemVec&& v) { 
                return Item(std::move(v)); 
            });
        } else if (std::isdigit(c) || c == '-' || c == '+') {
            // Check if it's actually a number or a symbol starting with +/-
            size_t saved_pos = pos;
            try {
                ItemResult item = parse_number();
                if (item) {
                    // Check if we consumed the entire token (not followed by non-delimiter)
                    if (pos >= input.length() || is_delimiter(input[pos])) {
                        return item;
                    }
                }
            } catch (...) {
                // parse_number threw an exception, fall through to symbol parsing
            }
            // else was not a valid number, fall through to symbol parsing

            pos = saved_pos; // Reset position and parse as symbol
            return parse_symbol().transform([](Symbol&& s) { return Item(std::move(s)); });
        } else {
            return parse_symbol().transform([](Symbol&& s) { return Item(std::move(s)); });
        }
    }
    
public:
    explicit SExprParser(std::string const& str) : input(str), pos(0) {}
    
    ItemResult parse() {
        pos = 0;
        ItemResult result = parse_item();
        skip_whitespace();
        if (pos < input.length()) {
            return std::unexpected("Unexpected characters after complete expression at position " + std::to_string(pos));
        }
        return result;
    }
};

ItemResult parse_sexpr(std::string const& input) {
    SExprParser parser(input);
    return parser.parse();
}

struct Slice {
    size_t size;
    uint8_t data[0];
};

enum ItemTag {
    tagBool,
    tagInt,
    tagFloat,
    tagSymbol,
    tagString,
    tagList,
};

union Datum {
    int64_t i;
    double f;
    size_t offset;
};

struct Items16 {
    uint64_t tags;
    Datum d[16];
 
    uint8_t getTag(int i) const {
        return 0x0f & (tags >> (4*i));
    }
};

struct SymbolBuildRef {
    size_t offset;
};

struct StringBuildRef {
    size_t offset;
};

struct ListBuildRef {
    size_t offset;
};

struct SymbolRef {
    uint8_t const* buf;
    size_t offset;
    
    Slice& getSlice() const { return *(Slice*)(buf + offset); }
    const char* getChars() const { return (const char*)&getSlice().data; }
    std::string getString() const { return std::string(getChars()); }
};

struct StringRef {
    uint8_t const* buf;
    size_t offset;
    
    Slice& getSlice() const { return *(Slice*)(buf + offset); }
    const char* getChars() const { return (const char*)&getSlice().data; }
    std::string getString() const { return std::string(getChars()); }
};

struct ListRef {
    uint8_t const* buf;
    size_t offset;
    
    Slice& getSlice() const { return *(Slice*)(buf + offset); }
    Items16 const* getItems() const { return (Items16 const*)&getSlice().data; }
 
    std::optional<uint8_t> getTag(int index) {
        Slice& slice = getSlice();
        if (index < 0 || index >= slice.size) return {};
        Items16 const* items = (Items16 const*)&slice.data;
        int a = index >> 4;
        int b = index & 15;
        return items[a].getTag(b);
    }
 
    std::optional<bool> getBool(int index) {
        Slice& slice = getSlice();
        if (index < 0 || index >= slice.size) return {};
        Items16 const* items = (Items16 const*)&slice.data;
        int a = index >> 4;
        int b = index & 15;
        if (items[a].getTag(b) != tagBool) return {};
        return items[a].d[b].i;
    }

    std::optional<int64_t> getInt(int index) {
        Slice& slice = getSlice();
        if (index < 0 || index >= slice.size) return {};
        Items16 const* items = (Items16 const*)&slice.data;
        int a = index >> 4;
        int b = index & 15;
        if (items[a].getTag(b) != tagInt) return {};
        return items[a].d[b].i;
    }

    std::optional<double> getFloat(int index) {
        Slice& slice = getSlice();
        if (index < 0 || index >= slice.size) return {};
        Items16 const* items = (Items16 const*)&slice.data;
        int a = index >> 4;
        int b = index & 15;
        if (items[a].getTag(b) != tagFloat) return {};
        return items[a].d[b].f;
    }

    std::optional<ListRef> getListRef(int index) {
        Slice& slice = getSlice();
        if (index < 0 || index >= slice.size) return {};
        Items16 const* items = (Items16 const*)&slice.data;
        int a = index >> 4;
        int b = index & 15;
        if (items[a].getTag(b) != tagList) return {};
        return ListRef{buf, items[a].d[b].offset};
    }

    std::optional<SymbolRef> getSymbolRef(int index) {
        Slice& slice = getSlice();
        if (index < 0 || index >= slice.size) return {};
        Items16 const* items = (Items16 const*)&slice.data;
        int a = index >> 4;
        int b = index & 15;
        if (items[a].getTag(b) != tagSymbol) return {};
        return SymbolRef{buf, items[a].d[b].offset};
    }
    std::optional<std::string> getSymbol(int index) {
        auto v = getSymbolRef(index);
        if (!v) return {};
        return v.value().getString();
    }

    std::optional<StringRef> getStringRef(int index) {
        Slice& slice = getSlice();
        if (index < 0 || index >= slice.size) return {};
        Items16 const* items = (Items16 const*)&slice.data;
        int a = index >> 4;
        int b = index & 15;
        if (items[a].getTag(b) != tagString) return {};
        return StringRef{buf, items[a].d[b].offset};
    }
    std::optional<std::string> getString(int index) {
        auto v = getStringRef(index);
        if (!v) return {};
        return v.value().getString();
    }
};

struct ListBuilder;

struct Builder {
    std::unordered_map<std::string, size_t> symbols;
    std::unordered_map<std::string, size_t> strings;
    std::unordered_map<size_t, std::vector<size_t>> lists;
    
    std::vector<uint8_t> buf;
    
    uint8_t* alloc(size_t n) {
        uint8_t* p = &*buf.end();
        n = (n + 7) & ~size_t(7);
        buf.resize(buf.size() + n);
        return p;
    }
        
    SymbolBuildRef newSymbol(std::string name) {
        auto p = symbols.find(name);
        if (p != symbols.end()) {
            return {p->second};
        }
        size_t offset = buf.size();
        Slice* slice = (Slice*)alloc(8 + name.size()+1);
        slice->size = name.size();
        strncpy((char*)slice->data, name.c_str(), name.size());
        symbols[name] = offset;
        return {offset};
    }
    StringBuildRef newString(std::string str) {
        auto p = strings.find(str);
        if (p != strings.end()) {
            return {p->second};
        }
        size_t offset = buf.size();
        Slice* slice = (Slice*)alloc(8 + str.size());
        slice->size = str.size();
        strncpy((char*)slice->data, str.c_str(), str.size());
        return {offset};
    }
    ListBuildRef newList(std::function<void(ListBuilder&)>);
    

};

struct ListBuilder {
    Builder& builder;
 
    Slice* slice;
    Items16* last;
    int chunkSize = 0;
    
    size_t lastItemOffset;
    
    ListBuilder(Builder& builder) 
        : builder(builder) 
    {
        slice = (Slice*)builder.alloc(16);
        slice->size = 0;
        last = (Items16*)&slice->data;
    }
    
    void addItem(uint8_t tag, Datum d) {
        if (chunkSize == 16) {
            last = (Items16*)builder.alloc(8);
            chunkSize = 0;
        }
        last->tags |= tag << (chunkSize*4);
        Datum* dat = (Datum*)builder.alloc(8);
        *dat = d;
        ++chunkSize;
        ++slice->size;
    }
        
    void add(bool b) { addItem(tagBool, {.i = b}); }
    
    template <std::integral T>
    void add(T i) { addItem(tagInt, {.i = int64_t(i)}); }
    
    template <std::floating_point T>
    void add(T f) { addItem(tagFloat, {.f = double(f)}); }
    
    void add(SymbolBuildRef r) { addItem(tagSymbol, {.offset = r.offset}); }
    
    void add(StringBuildRef r) { addItem(tagString, {.offset = r.offset}); }
    
    void add(ListBuildRef r) { addItem(tagList, {.offset = r.offset}); }
};

std::vector<uint8_t> Item::to_binary() const {
    Builder builder;
    
    return builder.buf;
}


}


void test_sexpr() {
    using namespace sexpr;
        // time how long it takes to parse a simple expression

    auto start = std::chrono::high_resolution_clock::now();
    
    auto item = parse_sexpr(R"#(
(sched 100
    (newNode sinosc 101)
    (setInput (101 0) (300))
    (setInput (101 1) (0))
    (setInput (101 1) (0.05) .2 fadeEaseInCubic)
    (connect (101 0) (0 0))))#");
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    std::cout << "Parse time: " << duration.count() << "µs" << std::endl;

    if (!item) {
        std::cerr << "Parse error: " << item.error() << std::endl;
        return;
    }
    std::cout << item.value().to_string() << std::endl;
    
    // Check if it's a list
    if (item.value().is<std::vector<Item>>()) {
        auto& list = item.value().get<std::vector<Item>>();
        std::cout << "List has " << list.size() << " elements" << std::endl;
        
        // First element should be the '' symbol
        if (list[0].is<Symbol>()) {
            auto sym = list[0].get<Symbol>();
            std::cout << "First element is symbol: " << sym.name << std::endl;
        }
    }
    
    {
        Item a = "123";
        Item b = 123;
        Item c = 3.14;
        Item d = {{a, b, c}};
        Item e = ItemVec{"123", 123, 3.14};
    }
}


