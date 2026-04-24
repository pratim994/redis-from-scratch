#include <gtest/gtest.h>
#include "hashtable.h"
#include "common.h"
#include <string>
#include <unordered_map>
#include <vector>
#include <cstdlib>

struct KVNode {
    HNode       hnode;
    std::string key;
    int         value = 0;
};

static bool kv_eq(HNode* a, HNode* b) {
    auto* ka = container_of(a, KVNode, hnode);
    auto* kb = container_of(b, KVNode, hnode);
    return ka->key == kb->key;
}

static KVNode* make_kv(const std::string& k, int v) {
    auto* n = new KVNode();
    n->key  = k;
    n->value = v;
    n->hnode.hcode = str_hash(reinterpret_cast<const uint8_t*>(k.data()), k.size());
    return n;
}

//Tests

TEST(HashTable, BasicInsertAndLookup) {
    HMap map;
    auto* n = make_kv("hello", 42);
    map.insert(&n->hnode);

    KVNode key;
    key.key = "hello";
    key.hnode.hcode = str_hash(reinterpret_cast<const uint8_t*>("hello"), 5);
    HNode* found = map.lookup(&key.hnode, kv_eq);

    ASSERT_NE(found, nullptr);
    EXPECT_EQ(container_of(found, KVNode, hnode)->value, 42);

    map.clear();
    delete n;
}

TEST(HashTable, MissingKeyReturnsNull) {
    HMap map;
    KVNode key;
    key.key = "missing";
    key.hnode.hcode = str_hash(reinterpret_cast<const uint8_t*>("missing"), 7);
    EXPECT_EQ(map.lookup(&key.hnode, kv_eq), nullptr);
}

TEST(HashTable, DeleteKey) {
    HMap map;
    auto* n = make_kv("foo", 99);
    map.insert(&n->hnode);

    KVNode key;
    key.key = "foo";
    key.hnode.hcode = n->hnode.hcode;
    HNode* removed = map.remove(&key.hnode, kv_eq);

    ASSERT_NE(removed, nullptr);
    EXPECT_EQ(map.size(), 0u);
    EXPECT_EQ(map.lookup(&key.hnode, kv_eq), nullptr);

    delete container_of(removed, KVNode, hnode);
}

TEST(HashTable, SizeTracking) {
    HMap map;
    std::vector<KVNode*> nodes;
    for (int i = 0; i < 20; ++i) {
        auto* n = make_kv("key" + std::to_string(i), i);
        nodes.push_back(n);
        map.insert(&n->hnode);
    }
    EXPECT_EQ(map.size(), 20u);

    // Remove half
    for (int i = 0; i < 10; ++i) {
        map.remove(&nodes[i]->hnode, kv_eq);
        delete nodes[i];
    }
    EXPECT_EQ(map.size(), 10u);

    map.clear();
    for (int i = 10; i < 20; ++i) delete nodes[i];
}

TEST(HashTable, TriggerRehash) {
    // Insert enough entries to force rehashing (load factor > 8)
    HMap map;
    const int N = 200;
    std::vector<KVNode*> nodes;
    for (int i = 0; i < N; ++i) {
        auto* n = make_kv("rehash_key_" + std::to_string(i), i);
        nodes.push_back(n);
        map.insert(&n->hnode);
    }
    EXPECT_EQ(map.size(), static_cast<size_t>(N));

    // All nodes should still be findable
    for (int i = 0; i < N; ++i) {
        HNode* found = map.lookup(&nodes[i]->hnode, kv_eq);
        ASSERT_NE(found, nullptr);
        EXPECT_EQ(container_of(found, KVNode, hnode)->value, i);
    }

    map.clear();
    for (auto* n : nodes) delete n;
}

TEST(HashTable, Foreach) {
    HMap map;
    std::unordered_map<std::string, int> oracle = {{"a",1},{"b",2},{"c",3}};
    std::vector<KVNode*> nodes;
    for (auto& [k, v] : oracle) {
        auto* n = make_kv(k, v);
        nodes.push_back(n);
        map.insert(&n->hnode);
    }

    std::unordered_map<std::string, int> seen;
    map.foreach([&](HNode* node) {
        auto* kv = container_of(node, KVNode, hnode);
        seen[kv->key] = kv->value;
        return true;
    });
    EXPECT_EQ(seen, oracle);

    map.clear();
    for (auto* n : nodes) delete n;
}