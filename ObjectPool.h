#pragma once
#include "Common.h"

template<class T>
class ObjectPool{
public:
    T*New(){
        T*obj=nullptr;
        if(_freeList!=nullptr){
            void* next=NextObj(_freeList);
            obj=(T*)next;
            _freeList=next;
        }
        else{
            if(_remainBytes<sizeof(T)){
                _remainBytes=128*1024;
                _memory=SystemAlloc(_remainBytes>>13);
                if(_memory==nullptr){
                    throw std::bad_alloc();
                }
            }
            obj=(T*)_memory;
            //因为在设计中_freelist中前八个字节需要存放下一个对象的地址，所以T对象需要像上对齐到8字节
            size_t objsize=max(sizeof(T*),sizeof(void*));
            _memory+=objsize;
            _remainBytes-=objsize;
        }
        //定位new，不创造内存
        //在obj这个地址，创造一个T对象
        new(obj)T;
        return obj;
    }
    void Delete(T* obj){
        obj->~T();
        NextObj(obj)=_freeList;
        _freeList=obj;
    }
private:
    char* _memory = nullptr; // 指向大块内存的指针
	size_t _remainBytes = 0; // 大块内存在切分过程中剩余字节数

	void* _freeList = nullptr; // 还回来过程中链接的自由链表的头指针
};