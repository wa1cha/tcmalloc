#pragma once
#include "Common.h"

template<class T>
class ObjectPool{
public:
    T*New(){

    }
    void Delete(T* obj){
        
    }
private:
    char* _memory = nullptr; // 指向大块内存的指针
	size_t _remainBytes = 0; // 大块内存在切分过程中剩余字节数

	void* _freeList = nullptr; // 还回来过程中链接的自由链表的头指针
};