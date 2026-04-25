#include "commands.h"
#include "common.h"
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <cassert>

// ─── Serialization tags ──────────────────────────────────────────────────────
enum class Tag : uint8_t {
    Nil = 0, Err = 1, Str = 2, Int = 3, Dbl = 4, Arr = 5
};

enum class ErrCode : uint32_t {
    Unknown = 1, TooBig = 2, BadType = 3, BadArg = 4
};

// ─── Output helpers ──────────────────────────────────────────────────────────
static void out_nil(Buffer& out) {
    out.append_u8(static_cast<uint8_t>(Tag::Nil));
}
static void out_str(Buffer& out, std::string_view sv) {
    out.append_u8(static_cast<uint8_t>(Tag::Str));
    out.append_u32(static_cast<uint32_t>(sv.size()));
    out.append(sv);
}
static void out_int(Buffer& out, int64_t val) {
    out.append_u8(static_cast<uint8_t>(Tag::Int));
    out.append_i64(val);
}
static void out_dbl(Buffer& out, double val) {
    out.append_u8(static_cast<uint8_t>(Tag::Dbl));
    out.append_dbl(val);
}
static void out_err(Buffer& out, ErrCode code, std::string_view msg) {
    out.append_u8(static_cast<uint8_t>(Tag::Err));
    out.append_u32(static_cast<uint32_t>(code));
    out.append_u32(static_cast<uint32_t>(msg.size()));
    out.append(msg);
}
static void out_arr(Buffer& out, uint32_t n) {
    out.append_u8(static_cast<uint8_t>(Tag::Arr));
    out.append_u32(n);
}
// Dynamic-length array: fill count after writing elements.
static size_t out_begin_arr(Buffer& out) {
    out.append_u8(static_cast<uint8_t>(Tag::Arr));
    size_t ctx = out.abs_write_pos();
    out.append_u32(0);
    return ctx;
}
static void out_end_arr(Buffer& out, size_t ctx, uint32_t n) {
    std::memcpy(out.data_at(ctx), &n, 4);
}

// ─── Lookup helpers ──────────────────────────────────────────────────────────
struct LookupKey {
    HNode       node;
    std::string key;
};

static bool entry_eq(HNode* node, HNode* key) {
    auto* ent = container_of(node, Entry, node);
    auto* lk  = container_of(key,  LookupKey, node);
    return ent->key == lk->key;
}

static HNode* db_lookup(std::string& k) {
    LookupKey lk;
    lk.key        = std::move(k);
    lk.node.hcode = str_hash(reinterpret_cast<const uint8_t*>(lk.key.data()), lk.key.size());
    HNode* node   = g_data.db.lookup(&lk.node, entry_eq);
    k = std::move(lk.key);   // give it back
    return node;
}

// ─── Entry lifecycle ─────────────────────────────────────────────────────────
ServerData g_data;

uint64_t get_monotonic_msec() {
    struct timespec tv = {};
    clock_gettime(CLOCK_MONOTONIC, &tv);
    return static_cast<uint64_t>(tv.tv_sec) * 1000 + tv.tv_nsec / 1'000'000;
}

void entry_set_ttl(Entry* ent, int64_t ttl_ms) {
    if (ttl_ms < 0 && ent->heap_idx != static_cast<size_t>(-1)) {
        heap_delete(g_data.heap, ent->heap_idx);
        ent->heap_idx = static_cast<size_t>(-1);
    } else if (ttl_ms >= 0) {
        uint64_t expire_at  = get_monotonic_msec() + static_cast<uint64_t>(ttl_ms);
        HeapItem item       = {expire_at, &ent->heap_idx};
        heap_upsert(g_data.heap, ent->heap_idx, item);
    }
}

static void entry_del_sync(Entry* ent) {
    if (ent->type == EntryType::ZSet) zset_clear(ent->zset);
    delete ent;
}

void entry_del(Entry* ent) {
    entry_set_ttl(ent, -1);
    size_t set_sz = (ent->type == EntryType::ZSet) ? ent->zset.hmap.size() : 0;
    constexpr size_t kLargeThreshold = 1000;
    if (set_sz > kLargeThreshold) {
        g_data.thread_pool.enqueue([ent] { entry_del_sync(ent); });
    } else {
        entry_del_sync(ent);
    }
}

// ─── Parsing helpers ─────────────────────────────────────────────────────────
static bool str2int(const std::string& s, int64_t& out) {
    char* end = nullptr;
    out = std::strtoll(s.c_str(), &end, 10);
    return end == s.c_str() + s.size();
}
static bool str2dbl(const std::string& s, double& out) {
    char* end = nullptr;
    out = std::strtod(s.c_str(), &end);
    return end == s.c_str() + s.size() && !std::isnan(out);
}

// ─── Command implementations ─────────────────────────────────────────────────

static void do_get(std::vector<std::string>& cmd, Buffer& out) {
    HNode* node = db_lookup(cmd[1]);
    if (!node) return out_nil(out);
    auto* ent = container_of(node, Entry, node);
    if (ent->type != EntryType::Str)
        return out_err(out, ErrCode::BadType, "not a string value");
    return out_str(out, ent->str);
}

static void do_set(std::vector<std::string>& cmd, Buffer& out) {
    HNode* node = db_lookup(cmd[1]);
    if (node) {
        auto* ent = container_of(node, Entry, node);
        if (ent->type != EntryType::Str)
            return out_err(out, ErrCode::BadType, "a non-string value exists");
        ent->str = std::move(cmd[2]);
    } else {
        auto* ent     = new Entry();
        ent->type     = EntryType::Str;
        ent->key      = std::move(cmd[1]);
        ent->node.hcode = str_hash(reinterpret_cast<const uint8_t*>(ent->key.data()), ent->key.size());
        ent->str      = std::move(cmd[2]);
        g_data.db.insert(&ent->node);
    }
    return out_nil(out);
}

static void do_del(std::vector<std::string>& cmd, Buffer& out) {
    LookupKey lk;
    lk.key        = std::move(cmd[1]);
    lk.node.hcode = str_hash(reinterpret_cast<const uint8_t*>(lk.key.data()), lk.key.size());
    HNode* node   = g_data.db.remove(&lk.node, entry_eq);
    if (node) entry_del(container_of(node, Entry, node));
    return out_int(out, node ? 1 : 0);
}

static void do_expire(std::vector<std::string>& cmd, Buffer& out) {
    int64_t ttl_ms = 0;
    if (!str2int(cmd[2], ttl_ms))
        return out_err(out, ErrCode::BadArg, "expect int64");
    HNode* node = db_lookup(cmd[1]);
    if (node) entry_set_ttl(container_of(node, Entry, node), ttl_ms);
    return out_int(out, node ? 1 : 0);
}

static void do_ttl(std::vector<std::string>& cmd, Buffer& out) {
    HNode* node = db_lookup(cmd[1]);
    if (!node) return out_int(out, -2);
    auto* ent = container_of(node, Entry, node);
    if (ent->heap_idx == static_cast<size_t>(-1)) return out_int(out, -1);
    uint64_t expire_at = g_data.heap[ent->heap_idx].val;
    uint64_t now_ms    = get_monotonic_msec();
    return out_int(out, expire_at > now_ms ? static_cast<int64_t>(expire_at - now_ms) : 0);
}

static void do_keys(std::vector<std::string>&, Buffer& out) {
    out_arr(out, static_cast<uint32_t>(g_data.db.size()));
    g_data.db.foreach([&out](HNode* node) {
        auto* ent = container_of(node, Entry, node);
        out_str(out, ent->key);
        return true;
    });
}

// ─── ZSet commands ───────────────────────────────────────────────────────────

static ZSet* expect_zset(std::string& name) {
    static const ZSet empty_zset;
    HNode* node = db_lookup(name);
    if (!node) return const_cast<ZSet*>(&empty_zset);
    auto* ent = container_of(node, Entry, node);
    return ent->type == EntryType::ZSet ? &ent->zset : nullptr;
}

static void do_zadd(std::vector<std::string>& cmd, Buffer& out) {
    double score = 0;
    if (!str2dbl(cmd[2], score))
        return out_err(out, ErrCode::BadArg, "expect float");

    HNode* hnode = db_lookup(cmd[1]);
    Entry* ent   = nullptr;
    if (!hnode) {
        ent          = new Entry();
        ent->type    = EntryType::ZSet;
        ent->key     = std::move(cmd[1]);
        ent->node.hcode = str_hash(reinterpret_cast<const uint8_t*>(ent->key.data()), ent->key.size());
        g_data.db.insert(&ent->node);
    } else {
        ent = container_of(hnode, Entry, node);
        if (ent->type != EntryType::ZSet)
            return out_err(out, ErrCode::BadType, "expect zset");
    }
    bool added = zset_insert(ent->zset, cmd[3], score);
    return out_int(out, static_cast<int64_t>(added));
}

static void do_zrem(std::vector<std::string>& cmd, Buffer& out) {
    ZSet* zset = expect_zset(cmd[1]);
    if (!zset) return out_err(out, ErrCode::BadType, "expect zset");
    ZNode* znode = zset_lookup(*zset, cmd[2]);
    if (znode) zset_delete(*zset, znode);
    return out_int(out, znode ? 1 : 0);
}

static void do_zscore(std::vector<std::string>& cmd, Buffer& out) {
    ZSet* zset = expect_zset(cmd[1]);
    if (!zset) return out_err(out, ErrCode::BadType, "expect zset");
    ZNode* znode = zset_lookup(*zset, cmd[2]);
    return znode ? out_dbl(out, znode->score) : out_nil(out);
}

static void do_zquery(std::vector<std::string>& cmd, Buffer& out) {
    double score = 0;
    if (!str2dbl(cmd[2], score))
        return out_err(out, ErrCode::BadArg, "expect float");
    int64_t offset = 0, limit = 0;
    if (!str2int(cmd[4], offset) || !str2int(cmd[5], limit))
        return out_err(out, ErrCode::BadArg, "expect int");

    ZSet* zset = expect_zset(cmd[1]);
    if (!zset) return out_err(out, ErrCode::BadType, "expect zset");
    if (limit <= 0) return out_arr(out, 0);

    ZNode* znode = zset_seekge(*zset, score, cmd[3]);
    znode        = znode_offset(znode, offset);

    size_t  ctx = out_begin_arr(out);
    int64_t n   = 0;
    while (znode && n < limit) {
        out_str(out, {znode->name, znode->len});
        out_dbl(out, znode->score);
        znode = znode_offset(znode, +1);
        n += 2;
    }
    out_end_arr(out, ctx, static_cast<uint32_t>(n));
}

// ─── Dispatch table ──────────────────────────────────────────────────────────
void do_request(std::vector<std::string>& cmd, Buffer& out) {
    if (cmd.empty()) return out_err(out, ErrCode::Unknown, "empty command");
    const auto& name = cmd[0];
    const size_t n   = cmd.size();

    if      (n == 2 && name == "get")     return do_get(cmd, out);
    else if (n == 3 && name == "set")     return do_set(cmd, out);
    else if (n == 2 && name == "del")     return do_del(cmd, out);
    else if (n == 3 && name == "pexpire") return do_expire(cmd, out);
    else if (n == 2 && name == "pttl")    return do_ttl(cmd, out);
    else if (n == 1 && name == "keys")    return do_keys(cmd, out);
    else if (n == 4 && name == "zadd")    return do_zadd(cmd, out);
    else if (n == 3 && name == "zrem")    return do_zrem(cmd, out);
    else if (n == 3 && name == "zscore")  return do_zscore(cmd, out);
    else if (n == 6 && name == "zquery")  return do_zquery(cmd, out);
    else return out_err(out, ErrCode::Unknown, "unknown command");
}
