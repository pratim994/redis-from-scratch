#include "avl.h"
#include <cassert>
#include <algorithm>

// ─── helpers ────────────────────────────────────────────────────────────────

static void avl_update(AVLNode* n) {
    n->height = 1 + std::max(avl_height(n->left), avl_height(n->right));
    n->cnt    = 1 + avl_cnt(n->left) + avl_cnt(n->right);
}

static AVLNode* rot_left(AVLNode* node) {
    AVLNode* rchild = node->right;
    AVLNode* inner  = rchild->left;

    rchild->left = node;
    node->right  = inner;

    rchild->parent = node->parent;
    node->parent   = rchild;
    if (inner) inner->parent = node;

    avl_update(node);
    avl_update(rchild);
    return rchild;
}

static AVLNode* rot_right(AVLNode* node) {
    AVLNode* lchild = node->left;
    AVLNode* inner  = lchild->right;

    lchild->right = node;
    node->left    = inner;

    lchild->parent = node->parent;
    node->parent   = lchild;
    if (inner) inner->parent = node;

    avl_update(node);
    avl_update(lchild);
    return lchild;
}

static AVLNode* avl_fix_left(AVLNode* node) {
    if (avl_height(node->left->left) < avl_height(node->left->right)) {
        node->left         = rot_left(node->left);
        node->left->parent = node;
    }
    return rot_right(node);
}

static AVLNode* avl_fix_right(AVLNode* node) {
    if (avl_height(node->right->right) < avl_height(node->right->left)) {
        node->right         = rot_right(node->right);
        node->right->parent = node;
    }
    return rot_left(node);
}

// ─── public API ─────────────────────────────────────────────────────────────

AVLNode* avl_fix(AVLNode* node) {
    while (true) {
        AVLNode** from   = nullptr;
        AVLNode*  parent = node->parent;
        if (parent)
            from = (parent->left == node) ? &parent->left : &parent->right;

        avl_update(node);

        uint32_t lh = avl_height(node->left);
        uint32_t rh = avl_height(node->right);

        if (lh == rh + 2) {
            AVLNode* fixed = avl_fix_left(node);
            fixed->parent  = parent;
            if (from) *from = fixed;
            node = fixed;
        } else if (lh + 2 == rh) {
            AVLNode* fixed = avl_fix_right(node);
            fixed->parent  = parent;
            if (from) *from = fixed;
            node = fixed;
        }

        if (!parent) return node;
        node = parent;
    }
}

static AVLNode* avl_del_easy(AVLNode* node) {
    assert(!node->left || !node->right);
    AVLNode* child  = node->left ? node->left : node->right;
    AVLNode* parent = node->parent;
    if (child) child->parent = parent;
    if (!parent) return child;
    AVLNode** from = (parent->left == node) ? &parent->left : &parent->right;
    *from = child;
    return avl_fix(parent);
}

AVLNode* avl_del(AVLNode* node) {
    if (node->left && node->right) {
        AVLNode* victim = node->right;
        while (victim->left) victim = victim->left;
        AVLNode* new_root = avl_del_easy(victim);
        *victim = *node;
        if (victim->left)  victim->left->parent  = victim;
        if (victim->right) victim->right->parent = victim;
        if (node->parent) {
            AVLNode** from = (node->parent->left == node)
                           ? &node->parent->left : &node->parent->right;
            *from = victim;
        }
        return node->parent ? new_root : victim;
    }
    return avl_del_easy(node);
}

// ─── avl_offset (corrected rank arithmetic) ──────────────────────────────────
//
// Strategy: compute the absolute rank of `node` by walking to the root,
// then seek to (rank + offset) from the root in a single downward pass.
// This is O(log n) and sidesteps the tricky incremental pos-tracking.

static int64_t avl_rank(AVLNode* node) {
    // Rank = number of nodes that come before this one in sorted order.
    int64_t rank = static_cast<int64_t>(avl_cnt(node->left));
    while (node->parent) {
        if (node->parent->right == node) {
            // All of parent's left subtree + parent itself come before us.
            rank += static_cast<int64_t>(avl_cnt(node->parent->left)) + 1;
        }
        node = node->parent;
    }
    return rank;
}

static AVLNode* avl_seek(AVLNode* root, int64_t target_rank) {
    if (!root) return nullptr;
    AVLNode* node = root;
    while (node) {
        int64_t left_cnt = static_cast<int64_t>(avl_cnt(node->left));
        if (target_rank < left_cnt) {
            node = node->left;
        } else if (target_rank == left_cnt) {
            return node;
        } else {
            target_rank -= left_cnt + 1;
            node = node->right;
        }
    }
    return nullptr;
}

AVLNode* avl_offset(AVLNode* node, int64_t offset) {
    if (!node) return nullptr;
    int64_t rank   = avl_rank(node) + offset;
    // Walk to root to get total count.
    AVLNode* root  = node;
    while (root->parent) root = root->parent;
    int64_t total  = static_cast<int64_t>(avl_cnt(root));
    if (rank < 0 || rank >= total) return nullptr;
    return avl_seek(root, rank);
}
