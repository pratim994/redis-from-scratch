#pragma once
#include "avl.h"
#include "hashtable.h"
#include <cstddef>
#include <cstdint>
#include <string_view>

// A single member of the sorted set.
struct ZNode {
    AVLNode  tree;         // in the score-ordered AVL tree
    HNode    hmap;         // in the name→node hash map
    double   score  = 0;
    size_t   len    = 0;   // length of name[]
    char     name[]; // NOLINT: C99 flexible array — intentional for inline storage
};

struct ZSet {
    AVLNode* root = nullptr;
    HMap     hmap;
};

// Returns true if the member was newly added, false if score was updated.
bool zset_insert(ZSet& zset, std::string_view name, double score);

// Returns nullptr if not found.
ZNode* zset_lookup(ZSet& zset, std::string_view name);

// Delete a member (caller must have obtained it from zset_lookup).
void zset_delete(ZSet& zset, ZNode* node);

// Find the first node with (score, name) >= (score, name). Returns nullptr if none.
ZNode* zset_seekge(ZSet& zset, double score, std::string_view name);

// Return the node 'offset' positions away in sorted order.
ZNode* znode_offset(ZNode* node, int64_t offset);

// Free all nodes and clear the set.
void zset_clear(ZSet& zset);