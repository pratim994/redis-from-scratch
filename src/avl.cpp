#include "avl.h"
#include <cassert>
#include <algorithm>


static void avl_update(AVLNode* n) {
    n->height = 1 + std::max(avl_height(n->left), avl_height(n->right));
    n->cnt    = 1 + avl_cnt(n->left) + avl_cnt(n->right);
}

// Left rotation:  node becomes left child of its current right child.
static AVLNode* rot_left(AVLNode* node) {
    AVLNode* rchild = node->right;
    AVLNode* inner  = rchild->left;   // may be nullptr

    rchild->left = node;
    node->right  = inner;

    rchild->parent = node->parent;   // caller links rchild into the parent
    node->parent   = rchild;
    if (inner) inner->parent = node;

    avl_update(node);
    avl_update(rchild);
    return rchild;
}

// Right rotation: node becomes right child of its current left child.
static AVLNode* rot_right(AVLNode* node) {
    AVLNode* lchild = node->left;
    AVLNode* inner  = lchild->right;  // may be nullptr

    lchild->right = node;
    node->left    = inner;

    lchild->parent = node->parent;
    node->parent   = lchild;
    if (inner) inner->parent = node;

    avl_update(node);
    avl_update(lchild);
    return lchild;
}

// Fix left-heavy imbalance (left subtree is 2 taller than right).
static AVLNode* avl_fix_left(AVLNode* node) {
    // LR case: rotate left child left first to make it LL
    if (avl_height(node->left->left) < avl_height(node->left->right)) {
        node->left         = rot_left(node->left);
        node->left->parent = node;
    }
    return rot_right(node);
}

// Fix right-heavy imbalance (right subtree is 2 taller than left).
static AVLNode* avl_fix_right(AVLNode* node) {
    // RL case: rotate right child right first to make it RR
    if (avl_height(node->right->right) < avl_height(node->right->left)) {
        node->right         = rot_right(node->right);
        node->right->parent = node;
    }
    return rot_left(node);
}


AVLNode* avl_fix(AVLNode* node) {
    while (true) {
        // Determine the parent's child pointer that currently points at node
        AVLNode** from   = nullptr;
        AVLNode*  parent = node->parent;

        if (parent) {
            from = (parent->left == node) ? &parent->left : &parent->right;
        }

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

        if (!parent) return node;  // reached the root
        node = parent;
    }
}

// Remove a node that has at most one child (the easy case).
static AVLNode* avl_del_easy(AVLNode* node) {
    assert(!node->left || !node->right);

    AVLNode* child  = node->left ? node->left : node->right;
    AVLNode* parent = node->parent;

    if (child) child->parent = parent;

    if (!parent) return child;  // deleted root; child is new root

    AVLNode** from = (parent->left == node) ? &parent->left : &parent->right;
    *from = child;
    return avl_fix(parent);
}

AVLNode* avl_del(AVLNode* node) {
    // Node has two children: swap with in-order successor then delete the successor.
    if (node->left && node->right) {
        AVLNode* victim = node->right;
        while (victim->left) victim = victim->left;

        AVLNode* new_root = avl_del_easy(victim);

        // Transplant victim into node's position
        *victim = *node;
        if (victim->left)  victim->left->parent  = victim;
        if (victim->right) victim->right->parent = victim;

        AVLNode** from = nullptr;
        if (node->parent) {
            from = (node->parent->left == node)
                 ? &node->parent->left
                 : &node->parent->right;
            *from = victim;
        }
        return node->parent ? new_root : victim;
    }

    return avl_del_easy(node);
}

AVLNode* avl_offset(AVLNode* node, int64_t offset) {
    // pos tracks the 0-based rank of 'node' relative to the initial 'node'.
    int64_t pos = 0;
    while (offset != pos) {
        if (pos < offset && pos + (int64_t)avl_cnt(node->right) >= offset) {
            // Target is in the right subtree
            pos  += (int64_t)avl_cnt(node->left) + 1;
            node  = node->right;
        } else if (pos > offset && pos - (int64_t)avl_cnt(node->left) <= offset) {
            // Target is in the left subtree
            pos  -= (int64_t)avl_cnt(node->right) + 1;
            node  = node->left;
        } else {
            // Go up
            AVLNode* parent = node->parent;
            if (!parent) return nullptr;
            if (parent->right == node) {
                pos -= (int64_t)avl_cnt(node->left) + 1;
            } else {
                pos += (int64_t)avl_cnt(node->right) + 1;
            }
            node = parent;
        }
    }
    return node;
}