//
//  symbol.cpp
//  static-lang-3
//
//  Lock-free global symbol table using hash-of-binary-trees.
//  Thread-safe via atomic CAS. Uses system allocator (std::string).
//

#include "symbol.hpp"
#include <atomic>
#include <vector>
#include <cassert>

namespace ts {

class SymbolTable {
private:
    struct Node {
        Symbol symbol;
        std::atomic<Node*> left;
        std::atomic<Node*> right;

        Node(std::string s)
            : symbol(std::move(s))
            , left(nullptr)
            , right(nullptr) {}
    };

    static constexpr size_t TABLE_SIZE = 16384;
    std::vector<std::atomic<Node*>> buckets;

    size_t bucket_index(size_t hash) const {
        return hash % TABLE_SIZE;
    }

    SymbolPtr find_or_insert(Node* current, std::string_view s, size_t hash_value) {
        while (current != nullptr) {
            if (hash_value == current->symbol.hash_ && s == current->symbol.s_) {
                return &current->symbol;
            }

            std::atomic<Node*>& next = (hash_value < current->symbol.hash_) ?
                current->left : current->right;

            Node* next_node = next.load(std::memory_order_acquire);
            if (next_node == nullptr) {
                // Found insertion point
                Node* new_node = new Node(std::string(s));
                if (next.compare_exchange_strong(next_node, new_node,
                    std::memory_order_release,
                    std::memory_order_acquire)) {
                    return &new_node->symbol;
                }
                // CAS failed, another thread inserted. Clean up and continue
                delete new_node;
                next_node = next.load(std::memory_order_acquire);
            }
            current = next_node;
        }
        return nullptr; // Should never reach here
    }

    // Helper function to delete a tree with reduced recursion
    static void delete_tree(Node* root) {
        while (root != nullptr) {
            // Delete left subtree recursively
            Node* left = root->left.load(std::memory_order_relaxed);
            if (left != nullptr) {
                delete_tree(left);
            }

            // Move to right child and delete current node
            Node* right = root->right.load(std::memory_order_relaxed);
            delete root;
            root = right;
        }
    }

public:
    SymbolTable() : buckets(TABLE_SIZE) {
        for (auto& bucket : buckets) {
            bucket.store(nullptr, std::memory_order_relaxed);
        }
    }

    ~SymbolTable() {
        for (auto& bucket : buckets) {
            delete_tree(bucket.load(std::memory_order_relaxed));
        }
    }

    SymbolPtr intern(std::string_view sv) {
        assert(!sv.empty());
        size_t hash_value = std::hash<std::string_view>{}(sv);
        size_t idx = bucket_index(hash_value);

        while (true) {
            Node* current = buckets[idx].load(std::memory_order_acquire);

            if (current == nullptr) {
                // Empty bucket, try to insert as root
                Node* new_node = new Node(std::string(sv));
                if (buckets[idx].compare_exchange_strong(current, new_node,
                    std::memory_order_release,
                    std::memory_order_acquire)) {
                    return &new_node->symbol;
                }
                delete new_node;
                current = buckets[idx].load(std::memory_order_acquire);
            }

            // Either bucket was non-empty or CAS failed
            SymbolPtr result = find_or_insert(current, sv, hash_value);
            if (result != nullptr) {
                return result;
            }
        }
    }

    // Delete copy constructor and assignment operator
    SymbolTable(const SymbolTable&) = delete;
    SymbolTable& operator=(const SymbolTable&) = delete;
};

static SymbolTable gSymbolTable;

SymbolPtr intern(std::string_view name) {
    return gSymbolTable.intern(name);
}

} // namespace ts
