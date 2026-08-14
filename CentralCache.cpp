#include "CentralCache.h"
#include "PageCache.h"

CentralCache CentralCache::_sInst;

Span* CentralCache::GetOneSpan(SpanList& list,size_t byte_size){
    // 先看桶里已有的span，谁还有空闲对象就用谁                                                                                                          
      Span* it = list.Begin();                                                                                                                             
      while (it != list.End())                                                                                                                             
      {                                                                                                                                                    
          if (it->_freeList != nullptr)                                                                                                                    
              return it;                                                                                                                                   
          it = it->_next;                                                                                                                                  
      }                                                                                                                                                    
                                                                                                                                                           
      // 桶里没有空闲span了 -> 先解锁桶，别挡着其他线程归还对象，然后找PageCache要                                                                         
      list._mtx.unlock();                                         
                                                                                                                                                           
      PageCache::Getinstance()->_pageMtx.lock();                                                                                                           
      Span* span = PageCache::Getinstance()->NewSpan(SizeClass::NumMovePage(byte_size));                                                                        
      span->_isUse = true;                                                                                                                                 
      span->_objSize = byte_size;                                                                                                                               
      PageCache::Getinstance()->_pageMtx.unlock();                                                                                                         
                                                                                                                                                           
      // 把这块大内存切成 size 大小的对象，用 NextObj 串成链表                                                                                             
      char* start = (char*)(span->_pageId << PAGE_SHIFT);                                                                                                  
      size_t bytes = span->_n << PAGE_SHIFT;                                                                                                               
      char* end = start + bytes;                                                                                                                           
                                                                                                                                                           
      span->_freeList = start;   // 先切一块当头部                                                                                                         
      start += byte_size;                                                                                                                                       
      void* tail = span->_freeList;                                                                                                                        
      while (start < end)
      {
          NextObj(tail) = start;
          tail = NextObj(tail);
          start += byte_size;
      }
      NextObj(tail) = nullptr;   // 链表收尾

      // 切好后再加锁挂进桶
      list._mtx.lock();
      list.PushFront(span);
      return span;
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
            span->_prev=nullptr;
            _spanLists[index]._mtx.unlock();
            PageCache::Getinstance()->_pageMtx.lock();
            PageCache::Getinstance()->ReleaseSpanToPageCache(span);
            PageCache::Getinstance()->_pageMtx.unlock();
			_spanLists[index]._mtx.lock();


        }
        start=next;

    }
    _spanLists[index]._mtx.unlock();
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
    span->_useCount+=actNum;

    _spanLists[index]._mtx.unlock();
    return actNum;
}