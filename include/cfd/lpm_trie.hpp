// SPDX-License-Identifier: Apache-2.0
// Longest-prefix-match trie keyed by IpAddr (v4/v6). Holds opaque values of
// type T. Nodes are owned via std::unique_ptr — destroying the trie frees the
// whole tree in O(n) with no manual delete.
#pragma once
#include "cfd/cidr.hpp"
#include <memory>
#include <optional>

namespace cfd::net {

template <class T>
class LpmTrie {
public:
    LpmTrie() = default;
    LpmTrie(const LpmTrie&)            = delete;
    LpmTrie& operator=(const LpmTrie&) = delete;
    LpmTrie(LpmTrie&&) noexcept        = default;
    LpmTrie& operator=(LpmTrie&&) noexcept = default;

    void insert(const Cidr& c, T value) {
        Node** cur = root_for(c.addr.family);
        for (unsigned bit = 0; bit < c.prefix_len; ++bit) {
            if (!*cur) *cur = std::make_unique<Node>().release();  // see ~Node
            const bool b = bit_of(c.addr, bit);
            cur = b ? &(*cur)->right : &(*cur)->left;
        }
        if (!*cur) *cur = std::make_unique<Node>().release();
        (*cur)->value = std::move(value);
        (*cur)->has_value = true;
    }

    std::optional<T> lookup(const IpAddr& a) const {
        const Node* cur = (a.family == Family::V4) ? root_v4_ : root_v6_;
        std::optional<T> best;
        const unsigned max_bits = (a.family == Family::V4) ? 32u : 128u;
        for (unsigned bit = 0; cur && bit <= max_bits; ++bit) {
            if (cur->has_value) best = cur->value;
            if (bit == max_bits) break;
            cur = bit_of(a, bit) ? cur->right : cur->left;
        }
        return best;
    }

    ~LpmTrie() {
        destroy(root_v4_);
        destroy(root_v6_);
    }

private:
    struct Node {
        Node*  left{nullptr};
        Node*  right{nullptr};
        T      value{};
        bool   has_value{false};
    };

    static bool bit_of(const IpAddr& a, unsigned bit) noexcept {
        return (a.bytes[bit / 8] >> (7u - bit % 8u)) & 1u;
    }

    Node** root_for(Family f) noexcept {
        return (f == Family::V4) ? &root_v4_ : &root_v6_;
    }

    static void destroy(Node*& n) noexcept {
        if (!n) return;
        destroy(n->left);
        destroy(n->right);
        delete n;
        n = nullptr;
    }

    Node* root_v4_{nullptr};
    Node* root_v6_{nullptr};
};

}  // namespace cfd::net
