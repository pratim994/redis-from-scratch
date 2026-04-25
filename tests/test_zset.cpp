#include <gtest/gtest.h>
#include "zset.h"
#include <vector>
#include <string>
#include <cmath>

// ─── Tests ───────────────────────────────────────────────────────────────────

TEST(ZSet, InsertAndLookup) {
    ZSet zset;
    EXPECT_TRUE(zset_insert(zset, "alice", 1.0));
    EXPECT_TRUE(zset_insert(zset, "bob",   2.0));

    ZNode* n = zset_lookup(zset, "alice");
    ASSERT_NE(n, nullptr);
    EXPECT_DOUBLE_EQ(n->score, 1.0);
    EXPECT_EQ(std::string(n->name, n->len), "alice");

    zset_clear(zset);
}

TEST(ZSet, DuplicateInsertUpdatesScore) {
    ZSet zset;
    EXPECT_TRUE(zset_insert(zset, "alice", 1.0));
    EXPECT_FALSE(zset_insert(zset, "alice", 5.0));   // update, not new

    ZNode* n = zset_lookup(zset, "alice");
    ASSERT_NE(n, nullptr);
    EXPECT_DOUBLE_EQ(n->score, 5.0);

    zset_clear(zset);
}

TEST(ZSet, LookupMissing) {
    ZSet zset;
    EXPECT_EQ(zset_lookup(zset, "nobody"), nullptr);
}

TEST(ZSet, Delete) {
    ZSet zset;
    zset_insert(zset, "a", 1.0);
    zset_insert(zset, "b", 2.0);

    ZNode* n = zset_lookup(zset, "a");
    ASSERT_NE(n, nullptr);
    zset_delete(zset, n);

    EXPECT_EQ(zset_lookup(zset, "a"), nullptr);
    EXPECT_NE(zset_lookup(zset, "b"), nullptr);

    zset_clear(zset);
}

TEST(ZSet, OrderByScore) {
    ZSet zset;
    // Insert in non-sorted order
    zset_insert(zset, "c", 3.0);
    zset_insert(zset, "a", 1.0);
    zset_insert(zset, "b", 2.0);

    // Seek to score=1.0, name="" → should land at "a"
    ZNode* n = zset_seekge(zset, 1.0, "");
    ASSERT_NE(n, nullptr);
    EXPECT_DOUBLE_EQ(n->score, 1.0);
    EXPECT_EQ(std::string(n->name, n->len), "a");

    // Walk forward
    n = znode_offset(n, 1);
    ASSERT_NE(n, nullptr);
    EXPECT_EQ(std::string(n->name, n->len), "b");

    n = znode_offset(n, 1);
    ASSERT_NE(n, nullptr);
    EXPECT_EQ(std::string(n->name, n->len), "c");

    n = znode_offset(n, 1);
    EXPECT_EQ(n, nullptr);   // past the end

    zset_clear(zset);
}

TEST(ZSet, TieBreakByName) {
    ZSet zset;
    zset_insert(zset, "z", 1.0);
    zset_insert(zset, "a", 1.0);
    zset_insert(zset, "m", 1.0);

    // Same score — order should be lexicographic: a, m, z
    ZNode* n = zset_seekge(zset, 1.0, "");
    ASSERT_NE(n, nullptr);
    EXPECT_EQ(std::string(n->name, n->len), "a");

    n = znode_offset(n, 1);
    EXPECT_EQ(std::string(n->name, n->len), "m");

    n = znode_offset(n, 1);
    EXPECT_EQ(std::string(n->name, n->len), "z");

    zset_clear(zset);
}

TEST(ZSet, SeekGePastEnd) {
    ZSet zset;
    zset_insert(zset, "a", 1.0);
    ZNode* n = zset_seekge(zset, 999.0, "");
    EXPECT_EQ(n, nullptr);
    zset_clear(zset);
}

TEST(ZSet, OffsetNegative) {
    ZSet zset;
    for (int i = 1; i <= 5; ++i)
        zset_insert(zset, std::to_string(i), static_cast<double>(i));

    // Seek to score=5
    ZNode* n = zset_seekge(zset, 5.0, "");
    ASSERT_NE(n, nullptr);
    EXPECT_DOUBLE_EQ(n->score, 5.0);

    // Walk backward
    n = znode_offset(n, -1);
    ASSERT_NE(n, nullptr);
    EXPECT_DOUBLE_EQ(n->score, 4.0);

    zset_clear(zset);
}

TEST(ZSet, StressInsert) {
    ZSet zset;
    const int N = 300;
    for (int i = 0; i < N; ++i)
        zset_insert(zset, "member_" + std::to_string(i), static_cast<double>(i));

    EXPECT_EQ(zset.hmap.size(), static_cast<size_t>(N));

    // All should be findable
    for (int i = 0; i < N; ++i) {
        ZNode* n = zset_lookup(zset, "member_" + std::to_string(i));
        ASSERT_NE(n, nullptr);
        EXPECT_DOUBLE_EQ(n->score, static_cast<double>(i));
    }

    zset_clear(zset);
}
