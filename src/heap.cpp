#include "heap.h"
#include <cassert>

static void heap_swap(HeapItem* a, size_t i, size_t j) {
    std::swap(a[i], a[j]);
    *a[i].ref = i;
    *a[j].ref = j;
}

void heap_update(HeapItem* a, size_t pos, size_t len) {
    // Sift up
    while (pos > 0) {
        size_t parent = (pos - 1) / 2;
        if (a[parent].val <= a[pos].val) break;
        heap_swap(a, parent, pos);
        pos = parent;
    }
    // Sift down
    while (true) {
        size_t smallest = pos;
        size_t l = 2 * pos + 1;
        size_t r = 2 * pos + 2;
        if (l < len && a[l].val < a[smallest].val) smallest = l;
        if (r < len && a[r].val < a[smallest].val) smallest = r;
        if (smallest == pos) break;
        heap_swap(a, pos, smallest);
        pos = smallest;
    }
}

void heap_delete(std::vector<HeapItem>& heap, size_t pos) {
    heap[pos] = heap.back();
    heap.pop_back();
    if (pos < heap.size()) {
        heap_update(heap.data(), pos, heap.size());
    }
}

void heap_upsert(std::vector<HeapItem>& heap, size_t pos, HeapItem item) {
    if (pos < heap.size()) {
        heap[pos] = item;
    } else {
        pos = heap.size();
        heap.push_back(item);
    }
    heap_update(heap.data(), pos, heap.size());
}