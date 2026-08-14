#include "ThreadCache.h"
#include "CentralCache.h"

using std::min;


void* ThreadCache::FetchFromCentralCache(size_t index,size_t size){
    // 慢开始：批量个数 = min(自己当前的MaxSize, 该size类建议的批量)                                                                                     
      size_t batchNum = min(_freelist[index].MaxSize(), SizeClass::NumMoveSize(size));                                                                     
      if (_freelist[index].MaxSize() == batchNum)                                                                                                          
      {                                                                                                                                                    
          _freelist[index].MaxSize() += 1;   // 这次要的都被用光了，下次多要一个                                                                           
      }                                                                                                                                                    
                                                                  
      void* start = nullptr;                                                                                                                               
      void* end = nullptr;                                        
      size_t actualNum = CentralCache::Getinstant()->FetchRangeObj(start, end, batchNum, size);                                                            
      assert(actualNum > 0);                                                                                                                               
                                                                                                                                                           
      if (actualNum == 1)                                                                                                                                  
      {                                                                                                                                                    
          return start;                       // 就一个，直接返回给用户                                                                                    
      }                                                                                                                                                    
      else                                                                                                                                                 
      {                                                                                                                                                    
          _freelist[index].PushRange(NextObj(start), end, actualNum - 1);  // 多余的挂回线程缓存
          return start;                                                                                                                                    
      }               
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
    
    if(_freelist[index].Size()>=_freelist[index].MaxSize()){
        ListTooLong(_freelist[index],size);
    }
}
