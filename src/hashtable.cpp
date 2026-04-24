#include "hashtable.h"
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <stdexcept>


void HMap::HTab::init(size_t n) {
    assert(n > 0 && (n & (n - 1)) == 0);   // must be a power of two
    tab  = static_cast<HNode**>(std::calloc(n, sizeof(HNode*)));
    if (!tab) throw std::bad_alloc();
    mask = n - 1;
    sz   = 0;
}

void HMap::HTab::insert(HNode* node) {
    size_t pos  = node->hcode & mask;
    node->next  = tab[pos];
    tab[pos]    = node;
    ++sz;
}

HNode** HMap::HTab::lookup(HNode* key, const HEqFunc& eq) {
    if (!tab) return nullptr;
    size_t  pos  = key->hcode & mask;
    HNode** from = &tab[pos];
    for (HNode* cur; (cur = *from) != nullptr; from = &cur->next) {
        if (cur->hcode == key->hcode && eq(cur, key)) return from;
    }
    return nullptr;
}

void HMap::HTab::free_storage() {
    std::free(tab);
    tab  = nullptr;
    mask = 0;
    sz   = 0;
}


static HNode* h_detach(HMap::HTab& htab, HNode** from) {
    HNode* node = *from;
    *from       = node->next;
    --htab.sz;
    return node;
}

void HMap::help_rehash() {
    size_t nwork = 0;
    while (nwork < kRehashWork && older_.sz > 0) {
        HNode** from = &older_.tab[migrate_pos_];
        if (!*from) { ++migrate_pos_; continue; }
        newer_.insert(h_detach(older_, from));
        ++nwork;
    }
    if (older_.sz == 0 && older_.tab) {
        older_.free_storage();
    }
}

void HMap::trigger_rehash() {
    assert(!older_.tab);
    older_       = newer_;
    newer_.init((older_.mask + 1) * 2);
    migrate_pos_ = 0;
}

HNode* HMap::lookup(HNode* key, const HEqFunc& eq) {
    help_rehash();
    HNode** from = newer_.lookup(key, eq);
    if (!from)  from = older_.lookup(key, eq);
    return from ? *from : nullptr;
}

void HMap::insert(HNode* node) {
    if (!newer_.tab) newer_.init(4);
    newer_.insert(node);

    if (!older_.tab) {
        size_t threshold = (newer_.mask + 1) * kMaxLoadFactor;
        if (newer_.sz >= threshold) trigger_rehash();
    }
    help_rehash();
}

HNode* HMap::remove(HNode* key, const HEqFunc& eq) {
    help_rehash();
    if (HNode** from = newer_.lookup(key, eq)) return h_detach(newer_, from);
    if (HNode** from = older_.lookup(key, eq)) return h_detach(older_, from);
    return nullptr;
}

void HMap::foreach(std::function<bool(HNode*)> f) {
    auto visit = [&](HTab& ht) -> bool {
        if (!ht.tab) return true;
        for (size_t i = 0; i <= ht.mask; ++i) {
            for (HNode* n = ht.tab[i]; n; n = n->next) {
                if (!f(n)) return false;
            }
        }
        return true;
    };
    if (visit(newer_)) visit(older_);
}

size_t HMap::size() const { return newer_.sz + older_.sz; }

void HMap::clear() {
    newer_.free_storage();
    older_.free_storage();
}