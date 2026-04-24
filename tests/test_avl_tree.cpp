#include <gtest/gtest.h>
#include "avl.h"
#include "common.h"
#include <cstdlib>
#include <vector>
#include <algorithm>
#include <set>

// Minimal container to test AVL tree
struct IntNode {
    AVLNode avl;
    int     val;
};

static IntNode* make_node(int v) {
    auto* n = new IntNode();
    avl_init(&n->avl);
    n->val = v;
    return n;
}

static AVLNode* tree_insert(AVLNode* root, IntNode* n) {
    AVLNode** from   = &root;
    AVLNode*  parent = nullptr;
    while (*from) {
        parent = *from;
        int pv  = container_of(parent, IntNode, avl)->val;
        from = (n->val < pv) ? &parent->left : &parent->right;
    }
    *from         = &n->avl;
    n->avl.parent = parent;
    return avl_fix(&n->avl);
}

static bool is_balanced(AVLNode* n) {
    if (!n) return true;
    int diff = static_cast<int>(avl_height(n->left)) - static_cast<int>(avl_height(n->right));
    if (diff < -1 || diff > 1) return false;
    if (avl_height(n) != 1 + std::max(avl_height(n->left), avl_height(n->right))) return false;
    if (avl_cnt(n) != 1 + avl_cnt(n->left) + avl_cnt(n->right)) return false;
    return is_balanced(n->left) && is_balanced(n->right);
}

static void collect_inorder(AVLNode* n, std::vector<int>& out) {
    if (!n) return;
    collect_inorder(n->left, out);
    out.push_back(container_of(n, IntNode, avl)->val);
    collect_inorder(n->right, out);
}

static void free_tree(AVLNode* n) {
    if (!n) return;
    free_tree(n->left);
    free_tree(n->right);
    delete container_of(n, IntNode, avl);
}

//Tests

TEST(AVL, InsertAndBalance) {
    AVLNode* root = nullptr;
    std::vector<int> vals = {5, 3, 7, 1, 4, 6, 8, 2};
    for (int v : vals) root = tree_insert(root, make_node(v));

    EXPECT_TRUE(is_balanced(root));
    EXPECT_EQ(avl_cnt(root), static_cast<uint32_t>(vals.size()));

    std::vector<int> inorder;
    collect_inorder(root, inorder);
    std::sort(vals.begin(), vals.end());
    EXPECT_EQ(inorder, vals);

    free_tree(root);
}

TEST(AVL, InsertAscendingTriggersRotations) {
    // Inserting in sorted order without balancing would produce a degenerate tree.
    AVLNode* root = nullptr;
    for (int i = 1; i <= 16; ++i) root = tree_insert(root, make_node(i));

    EXPECT_TRUE(is_balanced(root));
    EXPECT_EQ(avl_cnt(root), 16u);

    std::vector<int> inorder;
    collect_inorder(root, inorder);
    for (int i = 0; i < 16; ++i) EXPECT_EQ(inorder[i], i + 1);

    free_tree(root);
}

TEST(AVL, DeleteLeaf) {
    AVLNode* root = nullptr;
    std::vector<IntNode*> nodes;
    for (int v : {5, 3, 7}) {
        auto* n = make_node(v);
        nodes.push_back(n);
        root = tree_insert(root, n);
    }

    // Delete leaf '3'
    root = avl_del(&nodes[1]->avl);
    delete nodes[1];

    EXPECT_TRUE(is_balanced(root));
    EXPECT_EQ(avl_cnt(root), 2u);

    std::vector<int> inorder;
    collect_inorder(root, inorder);
    EXPECT_EQ(inorder, (std::vector<int>{5, 7}));

    free_tree(root);
}

TEST(AVL, DeleteRoot) {
    AVLNode* root = nullptr;
    std::vector<IntNode*> nodes;
    for (int v : {5, 3, 7, 1, 4, 6, 8}) {
        auto* n = make_node(v);
        nodes.push_back(n);
        root = tree_insert(root, n);
    }

    root = avl_del(root);   // delete the root
    EXPECT_TRUE(is_balanced(root));
    EXPECT_EQ(avl_cnt(root), 6u);

    free_tree(root);
}

TEST(AVL, OffsetQuery) {
    AVLNode* root = nullptr;
    for (int v : {10, 5, 15, 3, 7, 12, 20}) root = tree_insert(root, make_node(v));

    // Find node with value 5 (second smallest)
    auto* min_node = root;
    while (min_node->left) min_node = min_node->left;  // 3

    AVLNode* next = avl_offset(min_node, 1);   // should be 5
    ASSERT_NE(next, nullptr);
    EXPECT_EQ(container_of(next, IntNode, avl)->val, 5);

    AVLNode* out_of_bounds = avl_offset(min_node, 100);
    EXPECT_EQ(out_of_bounds, nullptr);

    free_tree(root);
}

TEST(AVL, StressInsertDelete) {
    const int N = 500;
    std::set<int> oracle;
    AVLNode* root = nullptr;
    std::vector<IntNode*> all_nodes;

    std::srand(42);
    for (int i = 0; i < N; ++i) {
        int v = std::rand() % 10000;
        if (oracle.insert(v).second) {
            auto* n = make_node(v);
            all_nodes.push_back(n);
            root = tree_insert(root, n);
        }
    }

    EXPECT_TRUE(is_balanced(root));
    EXPECT_EQ(avl_cnt(root), static_cast<uint32_t>(oracle.size()));

    // Delete half the nodes
    size_t half = all_nodes.size() / 2;
    for (size_t i = 0; i < half; ++i) {
        root = avl_del(&all_nodes[i]->avl);
        delete all_nodes[i];
    }

    EXPECT_TRUE(is_balanced(root));
    free_tree(root);
}