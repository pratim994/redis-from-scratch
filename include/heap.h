#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

struct HeapItem {
    uint64_t val;      // expiry timestamp (ms)
    size_t*  ref;      // back-pointer into the owning Entry::heap_idx
};

// Restore the heap invariant after val at position 'pos' changed.
void heap_update(HeapItem* a, size_t pos, size_t len);

// Convenience wrappers that also update the back-pointer.
void heap_delete(std::vector<HeapItem>& heap, size_t pos);
void heap_upsert(std::vector<HeapItem>& heap, size_t pos, HeapItem item);
