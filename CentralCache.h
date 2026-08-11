#pragma once

#include"Common.h"

class CentralCache{
public:
    static CentralCache* Getinstant(){
        return &_sInst;

    }
    Span* GetOneSpan(SpanList& list,size_t byte_size);
    //把对象传到span中
    void ReleaseListToSpans(void* start,size_t byte_size);
    // 从中心缓存获取一定数量的对象给thread cache
    void* FetchRangeObj(void*& start,void*& end,size_t batchnum,size_t size);

private:
    CentralCache()
    {}
    CentralCache(const CentralCache&)=delete;
    static CentralCache _sInst ;
};

