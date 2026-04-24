#include "zset.h"
#include "common.h"
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <algorithm>


static ZNode* znode_new(std::string_view name, double score) {
    ZNode* node = static_cast<ZNode*>(std::malloc(sizeof(ZNode) + name.size()));
    if (!node) throw std::bad_alloc();
    avl_init(&node->tree);
    node->hmap.next  = nullptr;
    node->hmap.hcode = str_hash(reinterpret_cast<const uint8_t*>(name.data()), name.size());
    node->score      = score;
    node->len        = name.size();
    std::memcpy(node->name, name.data(), name.size());
    return node;
}

static void znode_free(ZNode* node) { std::free(node); }


// Returns true if AVLNode 'lhs' is strictly less than (score, name).
static bool zless(const AVLNode* lhs, double score, std::string_view name) {
    const ZNode* zl = container_of(lhs, ZNode, tree);
    if (zl->score != score) return zl->score < score;
    std::string_view lname{zl->name, zl->len};
    return lname < name;
}

static bool zless(const AVLNode* lhs, const AVLNode* rhs) {
    const ZNode* zr = container_of(rhs, ZNode, tree);
    return zless(lhs, zr->score, {zr->name, zr->len});
}


static void tree_insert(ZSet& zset, ZNode* node) {
    AVLNode*  parent = nullptr;
    AVLNode** from   = &zset.root;
    while (*from) {
        parent = *from;
        from   = zless(&node->tree, parent) ? &parent->left : &parent->right;
    }
    *from            = &node->tree;
    node->tree.parent = parent;
    zset.root        = avl_fix(&node->tree);
}

static void zset_update(ZSet& zset, ZNode* node, double score) {
    if (node->score == score) return;
    zset.root = avl_del(&node->tree);
    avl_init(&node->tree);
    node->score = score;
    tree_insert(zset, node);
}


// Lookup key stored on the stack (no heap allocation needed).
struct ZKey {
    HNode       hnode;
    std::string_view name;
};

static bool hcmp(HNode* node, HNode* key) {
    const ZNode* zn = container_of(node, ZNode, hmap);
    const ZKey*  zk = container_of(key,  ZKey,  hnode);
    std::string_view zname{zn->name, zn->len};
    return zname == zk->name;
}


bool zset_insert(ZSet& zset, std::string_view name, double score) {
    ZNode* node = zset_lookup(zset, name);
    if (node) {
        zset_update(zset, node, score);
        return false;
    }
    node = znode_new(name, score);
    zset.hmap.insert(&node->hmap);
    tree_insert(zset, node);
    return true;
}

ZNode* zset_lookup(ZSet& zset, std::string_view name) {
    ZKey key;
    key.hnode.hcode = str_hash(reinterpret_cast<const uint8_t*>(name.data()), name.size());
    key.name        = name;
    HNode* found    = zset.hmap.lookup(&key.hnode, hcmp);
    return found ? container_of(found, ZNode, hmap) : nullptr;
}

void zset_delete(ZSet& zset, ZNode* node) {
    ZKey key;
    key.hnode.hcode = node->hmap.hcode;
    key.name        = {node->name, node->len};
    HNode* found    = zset.hmap.remove(&key.hnode, hcmp);
    assert(found);
    zset.root = avl_del(&node->tree);
    znode_free(node);
}

ZNode* zset_seekge(ZSet& zset, double score, std::string_view name) {
    AVLNode* found = nullptr;
    for (AVLNode* node = zset.root; node; ) {
        if (zless(node, score, name)) {
            node = node->right;
        } else {
            found = node;
            node  = node->left;
        }
    }
    return found ? container_of(found, ZNode, tree) : nullptr;
}

ZNode* znode_offset(ZNode* node, int64_t offset) {
    AVLNode* tnode = node ? avl_offset(&node->tree, offset) : nullptr;
    return tnode ? container_of(tnode, ZNode, tree) : nullptr;
}

static void tree_dispose(AVLNode* node) {
    if (!node) return;
    tree_dispose(node->left);
    tree_dispose(node->right);
    znode_free(container_of(node, ZNode, tree));
}

void zset_clear(ZSet& zset) {
    zset.hmap.clear();
    tree_dispose(zset.root);
    zset.root = nullptr;
}