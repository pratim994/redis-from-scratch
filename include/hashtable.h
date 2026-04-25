#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>

// Intrusive singly-linked hash node.  Embed in your struct.
struct HNode {
    HNode*   next  = nullptr;
    uint64_t hcode = 0;   // full hash, not masked
};

using HEqFunc = std::function<bool(HNode*, HNode*)>;

// Progressive-rehashing hash map (two tables, migrates k entries per operation).
struct HMap {
    // Lookup. Returns nullptr if not found.
    HNode* lookup(HNode* key, const HEqFunc& eq);
    // Insert (caller must set key->hcode before calling).
    void   insert(HNode* node);
    // Delete. Returns the deleted node or nullptr.
    HNode* remove(HNode* key, const HEqFunc& eq);
    // Visit every node.  Return false from f to stop early.
    void   foreach(std::function<bool(HNode*)> f);
    // Number of entries across both tables.
    [[nodiscard]] size_t size() const;
    // Free all storage (nodes themselves are caller-owned).
    void clear();

    // HTab is public so free helper functions in the .cpp can accept it by ref.
    struct HTab {
        HNode**  tab  = nullptr;
        uint64_t mask = 0;   // capacity - 1; capacity is always a power of two
        size_t   sz   = 0;

        void   init(size_t n);
        void   insert(HNode* node);
        HNode** lookup(HNode* key, const HEqFunc& eq);
        void   free_storage();
    };

private:

    void help_rehash();
    void trigger_rehash();

    HTab   newer_;
    HTab   older_;
    size_t migrate_pos_ = 0;

    static constexpr size_t kRehashWork     = 128;
    static constexpr size_t kMaxLoadFactor  = 8;
};
