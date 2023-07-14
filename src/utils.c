#include "blk_fs.h"

void markInvalid(InvalidBlockSet* set, int blockId) {
    set->blocks[blockId / BITS_PER_BYTE] |= (1 << (blockId % BITS_PER_BYTE));
}

void clearInvalid(InvalidBlockSet* set, int blockId) {
    set->blocks[blockId / BITS_PER_BYTE] &= ~(1 << (blockId % BITS_PER_BYTE));
}

void initializeInvalidBlockSet(InvalidBlockSet* set) {
    memset(set->blocks, 0, sizeof(set->blocks));
}

bool isInvalid(InvalidBlockSet* set, int blockId) {
    return (set->blocks[blockId / BITS_PER_BYTE] & (1 << (blockId % BITS_PER_BYTE))) != 0;
}