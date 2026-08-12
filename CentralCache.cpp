#include "CentralCache.h"
#include "PageCache.h"

CentralCache CentralCache::_sInst;

Span* CentralCache::GetOneSpan(SpanList& list,size_t byte_size){
    
}


void CentralCache::ReleaseListToSpans(void* start,size_t byte_size){
    size_t index=SizeClass::Index(byte_size);
    _spanLists[index]._mtx.lock();
    while(start){
        void*next=NextObj(start);
        Span* span=PageCache::Getinstance()->MapObjectToSpan(start);
        NextObj(start)=span->_freeList;
        span->_freeList=start;
        span->_useCount--;
        if(span->_useCount==0){
            _spanLists[index].Erase(span);
            span->_freeList=nullptr;
            span->_next=nullptr;
            span->_prev==nullptr;
            _spanLists[index]._mtx.unlock();
            PageCache::Getinstance()->_pageMtx.lock();
            PageCache::Getinstance()->ReleaseSpanToPageCache(span);
            PageCache::Getinstance()->_pageMtx.unlock();
			_spanLists[index]._mtx.lock();


        }
        start=next;

    }
    
}


size_t CentralCache::FetchRangeObj(void*& start, void*& end, size_t batchNum, size_t size){
    size_t index=SizeClass::Index(size);
    _spanLists[index]._mtx.lock();
    //取一个span用于切内存给ThreadCache
    Span* span=GetOneSpan(_spanLists[index],size);
    assert(span);
    assert(span->_freeList);

    //循环取值，并移动span中_freelist中的值
    start=span->_freeList;
    end=start;
    size_t i=0;
    size_t actNum=1;
    while(i<batchNum-1&&NextObj(end)!=nullptr){
        end=NextObj(end);
        i++;
        actNum++;
    }
    //修改span中的属性
    span->_freeList=NextObj(end);
    NextObj(end)=nullptr;
    span->_useCount-=actNum;

    _spanLists[index]._mtx.unlock();
    return actNum;
}