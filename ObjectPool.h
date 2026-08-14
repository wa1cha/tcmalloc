#pragma once
#include "Common.h"

template <class T>
class ObjectPool
{
public:
    T *New()
    {
        T *obj = nullptr;
        if (_freeList != nullptr)
        {
            obj = (T *)_freeList;
            _freeList = NextObj(_freeList);
        }
        else
        {
            if (_remainBytes < sizeof(T))
            {
                _remainBytes = 128 * 1024;
                _memory = (char *)SystemAlloc(_remainBytes >> 13);
                if (_memory == nullptr)
                {
                    throw std::bad_alloc();
                }
            }
            obj = (T*)_memory;
			size_t objSize = sizeof(T) < sizeof(void*) ? sizeof(void*) : sizeof(T);
			_memory += objSize;
			_remainBytes -= objSize;
        }
        // 定位new，不创造内存
        // 在obj这个地址，创造一个T对象
        new (obj) T;
        return obj;
    }
    void Delete(T *obj)
    {
        obj->~T();
        NextObj(obj) = _freeList;
        _freeList = obj;
    }

private:
    char *_memory = nullptr; // 指向大块内存的指针
    size_t _remainBytes = 0; // 大块内存在切分过程中剩余字节数

    void *_freeList = nullptr; // 还回来过程中链接的自由链表的头指针
};