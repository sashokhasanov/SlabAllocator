//
//  SlabAllocator.hpp
//  SlabAllocator
//
//  Created by Aleksandr Khasanov.
//  Copyright © 2019 Sashok Corporation LLC. All rights reserved.
//

#ifndef SlabAllocator_hpp
#define SlabAllocator_hpp

#include <cstdlib>

#define DEFAULT_CACHE_ORDER (1)
#define BLOCK_SIZE (4096)

/**
 * Following 2 functions are tended to simulate call of Buddy Allocator with page size of 4096 bytes.
 */

/**
 * Allocs 4096 * 2^order bytes, aligned to the border of 4096 * 2^order bytes.
 */
void* buddy_alloc(int order);

/**
 * Frees memory allocated with buddy_alloc()
 */
void buddy_free(void *slab);

// For testing issues may be implemented as following:
//
//void buddy_free(void *slab)
//{
//    std::free(slab);
//}
//
//Windows:
//void* buddy_alloc(int order)
//{
//    return _aligned_malloc(1ULL << (order + 12U), 1ULL << (order + 12U));
//}
//
//Linux:
//void* buddy_alloc(int order)
//{
//    void* result;
//    posix_memalign(&result, 1ULL << (order + 12U), 1ULL << (order + 12U));
//    return result;
//}


class CacheEntry;

/**
 * This struct represents single SLAB.
 */
struct Slab
{
    // starting adress of objects
    void* objectsPtr = 0;

    // bitmap of free objects
    // each element of bitmap stores index of next free objectstepic
    unsigned* freeObjects = 0;

    // index of first free object
    unsigned freeObjectIndex = 0;

    // number of active objects in this slab
    unsigned objectsInUse = 0;

    // next slab in chain
    Slab* nextSlab = nullptr;

    // previous slab in chain
    Slab* prevSlab = nullptr;

    // owner of current slab
    CacheEntry* owner = nullptr;
};

/**
 * This class represents single entry of cache chain.
 *
 * Each cache entry exists in order to manage allocations of objects of specific size.
 */
class CacheEntry
{
public:

    // init cache entry with allocated objects' size and cache order
    void init(size_t objectSize, int cacheOrder = DEFAULT_CACHE_ORDER);

    // allocates memory for single object of specified size
    void* alloc();

    // frees memory allocated with alloc()
    void free(void* ptr);

    // clears list of free slabs
    void shrink();

    // releases allocated memory
    void release();

public:
    // list of full SLABs
    Slab* slabsFull = nullptr;

    // list of partially full SLABs
    Slab* slabsPartial = nullptr;

    // list of free SLABs
    Slab* slabsFree = nullptr;

    // single object size
    size_t objectSize = 0;

    // count of objects in SLAB
    unsigned objectsInSlab = 0;

    // cache order
    // this affects the size of used SLABs, which equals BLOCK_SIZE * order
    int cacheOrder = 0;

    // next entry in cache chain
    CacheEntry*    next;

private:
    Slab* createSlab();

    void moveSlab(Slab* slb, Slab** listFrom, Slab** listTo);

    void addSlabToList(Slab* slb, Slab** slab_list);
};

/**
 * This class represents SLAB allocator.
 */
class SlabAllocator
{
public:

    // inits allocator
    void init();

    // allocates objectSize bytes
    void* alloc(size_t objectSize);

    // frees memory allocated with alloc()
    void free(void* ptr);

    // releases allocated memory
    void release();

private:

    // returns existing cache entry for specified object size if exists
    // creates new cache entry for specified object size if not exists
    CacheEntry* createCacheForObjectSize(size_t objectSize);

    // find cache entry that specified pointer belongs to
    CacheEntry* findCacheByPtr(void* ptr);

private:

    // cache chain entry
    CacheEntry* allCaches = nullptr;

    // cache entry for storing dynamically created cache entries
    CacheEntry cahceOfCaches;
};

#endif /* SlabAllocator_hpp */
