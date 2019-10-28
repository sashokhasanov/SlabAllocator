//
//  SlabAllocator.cpp
//  SlabAllocator
//
//  Created by Created by Aleksandr Khasanov.
//  Copyright © 2019 Sashok Corporation LLC. All rights reserved.
//

#include "SlabAllocator.hpp"

////////////////////////////////////////////////////////////////////////////////
// class CacheEntry

 //----------------------------------------------------------------------------//
void CacheEntry::init(size_t objectSize, int cacheOrder /*= CACHE_CACHE_ORDER*/)
{
    this->objectSize = objectSize;
    this->cacheOrder = cacheOrder;

    long memoryAvailable = (1 << cacheOrder) * BLOCK_SIZE - sizeof(Slab);

    this->objectsInSlab = memoryAvailable / (sizeof(unsigned) + objectSize);
}

//----------------------------------------------------------------------------//
void CacheEntry::shrink()
{
    Slab* slab = nullptr;
    while (slabsFree != nullptr)
    {
        slab = slabsFree;
        slabsFree = slab->nextSlab;

        buddy_free(slab);
    }
}

//----------------------------------------------------------------------------//
void* CacheEntry::alloc()
{
    Slab* slab = slabsPartial;

    if (slab == nullptr)
    {
        slab = slabsFree;
    }

    if (slab == nullptr)
    {
        Slab* newSlab = createSlab();
        addSlabToList(newSlab, &slabsFree);

        slab = newSlab;
    }

    void* retObject = (void*)((char*)slab->objectsPtr + slab->freeObjectIndex * objectSize);

    slab->freeObjectIndex = slab->freeObjects[slab->freeObjectIndex];
    slab->objectsInUse++;

    if (slab == slabsFree)
    {
        moveSlab(slab, &slabsFree, &slabsPartial);
    }
    else if (slab->objectsInUse == objectsInSlab)
    {
        moveSlab(slab, &slabsPartial, &slabsFull);
    }

    return retObject;
}

//----------------------------------------------------------------------------//
void CacheEntry::free(void* ptr)
{
    if (ptr == nullptr)
    {
        return;
    }

    int slabSize = BLOCK_SIZE * (1 << cacheOrder);

    Slab* slab = (Slab*)((size_t)ptr & (~(slabSize - 1)));
    slab->objectsInUse--;

    int i = ((char*)ptr - (char*)slab->objectsPtr) / objectSize;
    if (ptr != (void*)((char*)slab->objectsPtr + i * objectSize))
    {
        return;
    }
    slab->freeObjects[i] = slab->freeObjectIndex;
    slab->freeObjectIndex = i;


    if (slab->objectsInUse == objectsInSlab)
    {
        moveSlab(slab, &slabsFull, &slabsPartial);
    }
    else if (slab->objectsInUse == 0)
    {
        moveSlab(slab, &slabsPartial, &slabsFree);
    }
}

//----------------------------------------------------------------------------//
Slab* CacheEntry::createSlab()
{
    void* ptr = buddy_alloc(cacheOrder);

    Slab* slab = (Slab*)ptr;

    slab->freeObjects = (unsigned*)((char*)ptr + sizeof(Slab));
    slab->freeObjectIndex = 0;
    slab->nextSlab = nullptr;
    slab->prevSlab = nullptr;

    slab->owner = this;

    slab->objectsPtr = (void*)((char*)ptr + sizeof(Slab) + sizeof(unsigned) * objectsInSlab);

    for (unsigned i = 0; i < objectsInSlab; i++)
    {
        slab->freeObjects[i] = i + 1;
    }

    return slab;
}

//----------------------------------------------------------------------------//
void CacheEntry::moveSlab(Slab* slab, Slab** listFrom, Slab** listTo)
{
    if (slab == *listFrom)
    {
        *listFrom = slab->nextSlab;
    }
    else
    {
        Slab* next = slab->nextSlab;
        Slab* prev = slab->prevSlab;

        if (next != nullptr)
        {
            next->prevSlab = prev;
        }

        if (prev != nullptr)
        {
            prev->nextSlab = next;
        }
    }

    slab->nextSlab = nullptr;
    slab->prevSlab = nullptr;

    addSlabToList(slab, listTo);
}

//----------------------------------------------------------------------------//
void CacheEntry::addSlabToList(Slab* slab, Slab** slabList)
{
    slab->nextSlab = *slabList;
    slab->prevSlab = nullptr;

    if (*slabList != nullptr)
    {
        (*slabList)->prevSlab = slab;
    }

    *slabList = slab;
}

//----------------------------------------------------------------------------//
void CacheEntry::release()
{
    while (slabsFree != nullptr)
    {
        Slab* slab = slabsFree;
        slabsFree = slab->nextSlab;

        buddy_free(slab);
    }

    while (slabsFull != nullptr)
    {
        Slab* slab = slabsFull;
        slabsFull = slab->nextSlab;

        buddy_free(slab);
    }

    while (slabsPartial != nullptr)
    {
        Slab* slab = slabsPartial;
        slabsPartial = slab->nextSlab;

        buddy_free(slab);
    }
}

////////////////////////////////////////////////////////////////////////////////
// class SlabAllocator

//----------------------------------------------------------------------------//
void SlabAllocator::init()
{
    cahceOfCaches.init(sizeof(CacheEntry));
}

//----------------------------------------------------------------------------//
void* SlabAllocator::alloc(size_t objectSize)
{
    CacheEntry* cache = createCacheForObjectSize(objectSize);
    if (cache == nullptr)
    {
        return nullptr;
    }

    return cache->alloc();
}

//----------------------------------------------------------------------------//
void SlabAllocator::free(void* ptr)
{
    CacheEntry* cache = findCacheByPtr(ptr);
    cache->free(ptr);
}

//----------------------------------------------------------------------------//
void SlabAllocator::release()
{
    CacheEntry* cache = nullptr;
    while (allCaches != nullptr)
    {
        cache = allCaches;
        allCaches = cache->next;

        cache->release();
    }

    cahceOfCaches.release();
}

//----------------------------------------------------------------------------//
CacheEntry* SlabAllocator::createCacheForObjectSize(size_t objectSize)
{
    CacheEntry* cache = allCaches;
    while (cache != nullptr)
    {
        if (cache->objectSize == objectSize)
        {
            return cache;
        }

        cache = cache->next;
    }

    cache = (CacheEntry*)cahceOfCaches.alloc();
    cache->init(objectSize);

    cache->next = allCaches;

    allCaches = cache;

    return cache;
}

//----------------------------------------------------------------------------//
CacheEntry* SlabAllocator::findCacheByPtr(void* ptr)
{
    CacheEntry* cache = allCaches;
    while (cache != nullptr)
    {
        int slabSize = BLOCK_SIZE * (1 << cache->cacheOrder);

        Slab* slab = cache->slabsFull;
        while (slab != nullptr)
        {
            if ((void*)ptr > (void*)slab && (void*)ptr < (void*)((char*)slab + slabSize))
            {
                return cache;
            }

            slab = slab->nextSlab;
        }

        slab = cache->slabsPartial;
        while (slab != nullptr)
        {
            if ((void*)ptr > (void*)slab && (void*)ptr < (void*)((char*)slab + slabSize))
            {
                return cache;
            }

            slab = slab->nextSlab;
        }

        cache = cache->next;
    }

    return nullptr;
}
