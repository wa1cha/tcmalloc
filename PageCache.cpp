#include"PageCache.h"

PageCache PageCache::_sInst;

//PageCache三大功能
//获取一个span，由地址查到span，将回收的span合并释放
Span* PageCache::NewSpan(size_t k){
    assert(k>0);
    //如果k太大直接返回给用户，不用挂载在pagecache中
    if(k>NPAGE-1){
        void* ptr=SystemAlloc(k);
        Span* span=_spanpool.New();

        span->_pageId=(PAGE_ID)ptr >> PAGE_SHIFT;
        span->_n=k;

        _idSpanMap[span->_pageId]=span;
        return span;

    }
    if(!_spanLists[k].Empty()){
        Span* span=_spanLists[k].PopFront();
        for(int i=0;i<span->_n;i++){
            _idSpanMap[span->_pageId+i]=span;
        }
        return span;
    }
    for (size_t i = k+1; i < NPAGE; ++i)
	{
		if (!_spanLists[i].Empty())
		{
			Span* nSpan = _spanLists[i].PopFront();
			//Span* kSpan = new Span;
			Span* kSpan = _spanpool.New();

			// 在nSpan的头部切一个k页下来
			// k页span返回
			// nSpan再挂到对应映射的位置
			kSpan->_pageId = nSpan->_pageId;
			kSpan->_n = k;

			nSpan->_pageId += k;
			nSpan->_n -= k;

			_spanLists[nSpan->_n].PushFront(nSpan);
			// 存储nSpan的首位页号跟nSpan映射，方便page cache回收内存时
			// 进行的合并查找
			_idSpanMap[nSpan->_pageId] = nSpan;
			_idSpanMap[nSpan->_pageId + nSpan->_n - 1] = nSpan;


			// 建立id和span的映射，方便central cache回收小块内存时，查找对应的span
			for (PAGE_ID i = 0; i < kSpan->_n; ++i)
			{
				_idSpanMap[kSpan->_pageId + i] = kSpan;
			}

			return kSpan;
		}
	}
    
    //所有大page中都找不到k页的
    //内存申请一个最大的页的span，之后递归复用在查一遍
    Span* bigSpan=_spanpool.New();
    void* ptr=SystemAlloc(NPAGE-1);


    bigSpan->_pageId=(PAGE_ID)ptr>>PAGE_SHIFT;
    bigSpan->_n=NPAGE-1;

    _spanLists[bigSpan->_n].PushFront(bigSpan);
    return NewSpan(k);

}

Span* PageCache::MapObjectToSpan(void* obj){
    PAGE_ID id=((PAGE_ID)obj>>PAGE_SHIFT);
    std::unique_lock<std::mutex> lock(_pageMtx);
    //由idspanmap查找span，并返回
    auto ret=_idSpanMap.find(id);
    if(ret!=_idSpanMap.end()){
        return ret->second;
    }
    else{
        //如果没拿到数据，直接报错返回
        assert(false);
        return nullptr;
    }
    
}

void PageCache::ReleaseSpanToPageCache(Span* span){
    if(span->_n>NPAGE-1){
        void* ptr=(void*)(span->_pageId<<PAGE_SHIFT);
        SystemFree(ptr,span->_n);
        _spanpool.Delete(span);
        return;

    }
    while(1)
    {
        PAGE_ID PreId=span->_pageId-1;
        auto ret=_idSpanMap.find(PreId);
        //不一定能不能找到span
        if(ret==_idSpanMap.end()){
            break;
        }
        Span* prevSpan = ret->second;
        //是否被使用
        if(prevSpan->_isUse){
            break;
        }
        //最大只能合并NPAGE-1大小的页
        if(prevSpan->_n+span->_n>NPAGE-1){
            break;
        }
        span->_pageId=prevSpan->_pageId;
        span->_n+=prevSpan->_n;

        _spanLists[prevSpan->_n].Erase(prevSpan);
        _spanpool.Delete(prevSpan);
    }
    while(1){
        PAGE_ID nextSpan_id=span->_pageId+span->_n;
        auto ret=_idSpanMap.find(nextSpan_id);
        if(ret==_idSpanMap.end()){
            break;
        }
        Span* nextspan=ret->second;
        if(nextspan->_isUse==true){
            break;
        }
        if(nextspan->_n+span->_n>NPAGE-1){
            break;
        }
        span->_n+=nextspan->_n;
        _spanLists[nextspan->_n].Erase(nextspan);
        _spanpool.Delete(nextspan);
    }
    //把返回来的span重新挂到spanlist（pagecache）中
    _spanLists[span->_n].PushFront(span);
    span->_isUse=false;
    _idSpanMap[span->_pageId] = span;
	_idSpanMap[span->_pageId+span->_n-1] = span;
}