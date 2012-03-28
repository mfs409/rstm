#include <stdint.h>
#include "common/utils.hpp"
#include "stm/config.h"
#include "stm/WriteSet.hpp"
#include "sandboxing.hpp"
using namespace stm;

/***  Writeset constructor.  Note that the version must start at 1. */
WriteSet::WriteSet(const size_t initial_capacity)
    : index(NULL), shift(8 * sizeof(uint32_t)), ilength(0),
      version(1), list(NULL), capacity(initial_capacity), lsize(0)
{
    // Find a good index length for the initial capacity of the list.
    while (ilength < 3 * initial_capacity)
        doubleIndexLength();

    index = new index_t[ilength];
    list  = typed_malloc<WriteSetEntry>(capacity);
}

/***  Writeset destructor */
WriteSet::~WriteSet() {
    delete[] index;
    free(list);
}

/***  Rebuild the writeset */
void
WriteSet::rebuild() {
    assert(version != 0 && "ERROR: the version should *never* be 0");

    // extend the index
    delete[] index;
    index = new index_t[doubleIndexLength()];

    for (size_t i = 0; i < lsize; ++i) {
        const WriteSetEntry& l = list[i];
        size_t h = hash(l.addr);

        // search for the next available slot
        while (index[h].version == version)
            h = (h + 1) % ilength;

        index[h].address = l.addr;
        index[h].version = version;
        index[h].index   = i;
    }
}

/***  Resize the writeset */
void
WriteSet::resize() {
    stm::sandbox::InLib raii;
    WriteSetEntry* temp  = list;
    capacity     *= 2;
    list          = typed_malloc<WriteSetEntry>(capacity);
    memcpy(list, temp, sizeof(WriteSetEntry) * lsize);
    free(temp);
}

/***  Another writeset reset function that we don't want inlined */
void
WriteSet::reset_internal() {
    memset(index, 0, sizeof(index_t) * ilength);
    version = 1;
}

/**
 * Deal with the actual rollback of log entries, which depends on the
 * STM_ABORT_ON_THROW configuration as well as on the type of write logging
 * we're doing.
 */
#if defined(STM_ABORT_ON_THROW)
void
WriteSet::rollback(void** exception, size_t len) {
    // early exit if there's no exception
    if (!len)
        return;

    // for each entry, call rollback with the exception range, which will
    // actually writeback if the entry is in the address range.
    void** upper = (void**)((uint8_t*)exception + len);
    for (iterator i = begin(), e = end(); i != e; ++i)
        i->rollback(exception, upper);
}
#else
// rollback was inlined
#endif


void
WriteSet::writeback() {
    assert(this && "");
    for (iterator i = begin(), e = end(); i != e; ++i)
        i->writeback();
}

void
WriteSet::reset() {
    lsize    = 0;
    version += 1;

    // check overflow
    if (version != 0)
        return;
    reset_internal();
}

/**
 * This doubles the size of the index. This *does not* do anything as
 * far as actually doing memory allocation. Callers should delete[] the
 * index table, increment the table size, and then reallocate it.
 */
size_t
WriteSet::doubleIndexLength()
{
    assert(shift != 0 &&
           "ERROR: the writeset doesn't support an index this large");
    shift   -= 1;
    ilength  = 1 << (8 * sizeof(uint32_t) - shift);
    return ilength;
}
