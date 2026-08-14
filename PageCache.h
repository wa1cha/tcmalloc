#pragma once

#include"Common.h"
#include"ObjectPool.h"

//单例模式
class PageCache{
public:
    //单例入口
    static PageCache* Getinstance(){
        return &_sInst;
    }
    //已知obj来找到对应的span
    Span* MapObjectToSpan(void* obj);
    //将span归还至pagecache
    void ReleaseSpanToPageCache(Span* span);
    //拿k页的span
    Span* NewSpan(size_t k);
    //单例模式仅能保证整套程序仅有一份pagecache（一套页系统），所以需要用锁来保证
    std::mutex _pageMtx;

private:
    SpanList _spanLists[NPAGE];
    ObjectPool<Span> _spanpool;
    std::map<PAGE_ID,Span*> _idSpanMap;
    //把构造函数写在私有下，保证只有单一线程访问，饿汉模式
    PageCache(){}
    //禁止把 PageCache 对象拷贝出第二份，谁拷贝谁编译报错
    PageCache(const PageCache&)=delete;
    static PageCache _sInst;
};