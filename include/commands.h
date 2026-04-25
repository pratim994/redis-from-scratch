#pragma once
#include "buffer.h"
#include "hashtable.h"
#include "zset.h"
#include "heap.h"
#include "dlist.h"
#include "thread_pool.h"
#include <string>
#include <vector>

// ─── Value types ─────────────────────────────────────────────────────────────
enum class EntryType : uint32_t { Str = 1, ZSet = 2 };

// ─── Top-level KV entry ──────────────────────────────────────────────────────
struct Entry {
    HNode    node;
    std::string key;
    size_t   heap_idx = static_cast<size_t>(-1);  // index in g_data.heap
    EntryType type    = EntryType::Str;
    std::string str;
    ZSet     zset;
};

// ─── Global server state ─────────────────────────────────────────────────────
struct ServerData {
    HMap        db;
    DList       idle_list;
    std::vector<HeapItem> heap;
    ThreadPool  thread_pool{4};
};

extern ServerData g_data;

// ─── Command dispatch ────────────────────────────────────────────────────────
void do_request(std::vector<std::string>& cmd, Buffer& out);

// ─── TTL helpers (used by server and command handlers) ──────────────────────
uint64_t get_monotonic_msec();
void     entry_set_ttl(Entry* ent, int64_t ttl_ms);
void     entry_del(Entry* ent);
