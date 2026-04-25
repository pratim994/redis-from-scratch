#pragma once
#include <cstdint>
#include <cstddef>

// AVL tree node — embed into your struct, use container_of to recover the parent.
struct AVLNode {
    AVLNode* left   = nullptr;
    AVLNode* right  = nullptr;
    AVLNode* parent = nullptr;
    uint32_t height = 1;
    uint32_t cnt    = 1;  // subtree size (for rank queries)
};

inline void avl_init(AVLNode* node) {
    node->left = node->right = node->parent = nullptr;
    node->height = 1;
    node->cnt    = 1;
}

inline uint32_t avl_height(const AVLNode* node) { return node ? node->height : 0; }
inline uint32_t avl_cnt   (const AVLNode* node) { return node ? node->cnt    : 0; }

// Fix the tree after insertion/update starting from 'node'.  Returns new root.
AVLNode* avl_fix(AVLNode* node);

// Delete 'node' from the tree.  Returns new root.
AVLNode* avl_del(AVLNode* node);

// Return the node that is 'offset' positions away from 'node' in sorted order.
// Positive offset → successor, negative → predecessor.  Returns nullptr if OOB.
AVLNode* avl_offset(AVLNode* node, int64_t offset);
