#pragma once

#include "ThreadCache.h"
#include "PageCache.h"
#include "Common.h"
#include "CentralCache.h"
#include "ObjectPool.h"


static void* ConcurrentAlloc(size_t size){
    if(size>MAX_BYTES){
        //通过位运算，使得地址与页号的转换
        size_t alisize=SizeClass::RoundUp(size);
        size_t kpage=alisize>>PAGE_SHIFT;
        PageCache::Getinstance()->_pageMtx.lock();
        Span* span=PageCache::Getinstance()->NewSpan(kpage);
        size_t size=span->_objSize;
        PageCache::Getinstance()->_pageMtx.unlock();
        void* ptr=(void*)(span->_pageId<<PAGE_SHIFT);
        return ptr;
    }
    else{
        if(pTLSThreadCache==nullptr){
        static ObjectPool<ThreadCache> tcPool;
        pTLSThreadCache=new ThreadCache;
    }
    return pTLSThreadCache->Allocate(size);
    }

    
}
static void ConcurrentFree(void* ptr){
    Span* span=PageCache::Getinstance()->MapObjectToSpan(ptr);
    size_t size=span->_objSize;
    if(size>MAX_BYTES){
        PageCache::Getinstance()->_pageMtx.lock();
        PageCache::Getinstance()->ReleaseSpanToPageCache(span);
        PageCache::Getinstance()->_pageMtx.unlock();
    }
    else{
        assert(pTLSThreadCache);
        return pTLSThreadCache->Deallocate(ptr,size);
    }

}