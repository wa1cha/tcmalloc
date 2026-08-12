#include "ThreadCache.h"
#include "CentralCache.h"

using std::min;


void* ThreadCache::FetchFromCentralCache(size_t index,size_t size){

}


void* ThreadCache::Allocate(size_t size){
    assert(size<=MAX_BYTES);
    size_t index=SizeClass::Index(size);
    size_t alignsize=SizeClass::RoundUp(size);
    if(!_freelist[index].Empty()){
        return _freelist[index].Pop();
    }
    else{
        return FetchFromCentralCache(index,alignsize);
    }
}


void ThreadCache::ListTooLong(FreeList& list, size_t size){
    void* start=nullptr;
    void* end=nullptr;
    list.PopRange(start,end,list.MaxSize());
    CentralCache::Getinstant()->ReleaseListToSpans(start,size);

}

void ThreadCache::Deallocate(void* ptr,size_t size){
    assert(ptr);
    assert(size<=MAX_BYTES);

    size_t index = SizeClass::Index(size);
    _freelist[index].Push(ptr);
    
    if(_freelist[index].Size()>_freelist[index].MaxSize()){
        ListTooLong(_freelist[index],size);
    }
}
