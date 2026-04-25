#pragma once

// Intrusive circular doubly-linked list.
// The sentinel node's next/prev point to the first/last real items.
struct DList {
    DList* prev = nullptr;
    DList* next = nullptr;
};

inline void dlist_init(DList* node) {
    node->prev = node->next = node;
}

inline bool dlist_empty(const DList* node) {
    return node->next == node;
}

inline void dlist_detach(DList* node) {
    DList* prev = node->prev;
    DList* next = node->next;
    prev->next  = next;
    next->prev  = prev;
    dlist_init(node);   // make it a self-loop (safe to detach again)
}

// Insert 'rookie' immediately before 'target'
inline void dlist_insert_before(DList* target, DList* rookie) {
    DList* prev   = target->prev;
    prev->next    = rookie;
    rookie->prev  = prev;
    rookie->next  = target;
    target->prev  = rookie;
}
