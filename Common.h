#pragma once

#include<iostream>
#include<vector>
#include<unordered_map>
#include<string.h>
#include<algorithm>
#include<map>

#include<thread>
#include<mutex>

#include<time.h>
#include<assert.h>
#include<aio.h>
#include<atomic>

using std::cout;
using std::cin;

#ifdef WIN_32
    #include<windows.h>
#else
    #include<sys/mman.h>
    #include<unistd.h>
#endif


static const size_t NFREELIST=208;
static const size_t MAX_BYTES=256*1024;
static const size_t NPAGE=129;
static const size_t PAGE_SHIFT=13;

#ifdef WIN_64
    typedef unsigned long long PAGE_ID;
#else
    typedef size_t PAGE_ID;

#endif

// 直接去堆上按页申请空间
inline static void* SystemAlloc(size_t kpage)
{
#ifdef _WIN32
	void* ptr = VirtualAlloc(0, kpage << 13, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
#else
	// linux下brk mmap等
	void* ptr = mmap(
        nullptr,
		kpage<<13,
		PROT_READ|PROT_WRITE,
		MAP_ANONYMOUS|MAP_PRIVATE,
		-1,
		0
    );

    if (ptr == MAP_FAILED)
    {
        return nullptr;
    }

    return ptr;
#endif

	if (ptr == nullptr)
		throw std::bad_alloc();

	return ptr;
}

inline static void SystemFree(void* ptr, size_t kpage)
{
#ifdef _WIN32
    VirtualFree(ptr, 0, MEM_RELEASE);
#else
    munmap(ptr, kpage << PAGE_SHIFT);
#endif
}

static void*& NextObj(void* obj){
	return *(void**)obj;
}

class FreeList{
public:
void Push(void* obj){
	//头插
	assert(obj);
	NextObj(obj)=_freelist;
	_freelist=obj;
	_size++;
}
void PushRange(void* start,void* end,size_t n){
	NextObj(end)=_freelist;
	_freelist=start;
	_size+=n;
}
void PopRange(void*& start,void*& end,size_t n){
	//弹出一段list中的一段地址
	//⚠️这里也同样拿到了start和end
	assert(n<=_size);
	_freelist=start;
	end=start;
	for(int i=0;i<n-1;i++){
		end=NextObj(end);

	}
	_freelist=NextObj(end);
	NextObj(end)=nullptr;
	_size-=n;

}
void* Pop(){
	//头删
	assert(_freelist);
	void* obj=_freelist;
	_freelist=NextObj(obj);
	_size--;
	return obj;
}
bool Empty(){
	return _freelist==nullptr;
}
size_t& MaxSize(){
	return _MaxSize;
}
size_t& Size(){
	return _size;
}
private:
	void* _freelist=nullptr;
	size_t _MaxSize=1;
	size_t _size=0;
};




class SizeClass
{
public:
    static inline size_t _RoundUp(size_t bytes, size_t alignNum)
	{
		return ((bytes + alignNum - 1) & ~(alignNum - 1));
	}

	static inline size_t RoundUp(size_t size)
	{
		if (size <= 128)
		{
			return _RoundUp(size, 8);
		}
		else if (size <= 1024)
		{
			return _RoundUp(size, 16);
		}
		else if (size <= 8*1024)
		{
			return _RoundUp(size, 128);
		}
		else if (size <= 64*1024)
		{
			return _RoundUp(size, 1024);
		}
		else if (size <= 256 * 1024)
		{
			return _RoundUp(size, 8*1024);
		}
		else
		{
			return _RoundUp(size, 1<<PAGE_SHIFT);
		}
	}
    static inline size_t _Index(size_t bytes, size_t align_shift)
	{
		return ((bytes + (1 << align_shift) - 1) >> align_shift) - 1;
	}

	// 计算映射的哪一个自由链表桶
	static inline size_t Index(size_t bytes)
	{
		assert(bytes <= MAX_BYTES);

		// 每个区间有多少个链
		static int group_array[4] = { 16, 56, 56, 56 };
		if (bytes <= 128){
			return _Index(bytes, 3);
		}
		else if (bytes <= 1024){
			return _Index(bytes - 128, 4) + group_array[0];
		}
		else if (bytes <= 8 * 1024){
			return _Index(bytes - 1024, 7) + group_array[1] + group_array[0];
		}
		else if (bytes <= 64 * 1024){
			return _Index(bytes - 8 * 1024, 10) + group_array[2] + group_array[1] + group_array[0];
		}
		else if (bytes <= 256 * 1024){
			return _Index(bytes - 64 * 1024, 13) + group_array[3] + group_array[2] + group_array[1] + group_array[0];
		}
		else{
			assert(false);
		}

		return -1;
	}

	// 一次thread cache从中心缓存获取多少个
	static size_t NumMoveSize(size_t size)
	{
		assert(size > 0);

		// [2, 512]，一次批量移动多少个对象的(慢启动)上限值
		// 小对象一次批量上限高
		// 小对象一次批量上限低
		int num = MAX_BYTES / size;
		if (num < 2)
			num = 2;

		if (num > 512)
			num = 512;

		return num;
	}

	// 计算一次向系统获取几个页
	// 单个对象 8byte
	// ...
	// 单个对象 256KB
	static size_t NumMovePage(size_t size)
	{
		size_t num = NumMoveSize(size);
		size_t npage = num*size;

		npage >>= PAGE_SHIFT;
		if (npage == 0)
			npage = 1;

		return npage;
	}

};

struct Span
{
	PAGE_ID _pageId = 0; // 大块内存起始页的页号
	size_t  _n = 0;      // 页的数量

	Span* _next = nullptr;	// 双向链表的结构
	Span* _prev = nullptr;

	size_t _objSize = 0;  // 切好的小对象的大小
	size_t _useCount = 0; // 切好小块内存，被分配给thread cache的计数
	void* _freeList = nullptr;  // 切好的小块内存的自由链表

	bool _isUse = false;          // 是否在被使用
};
class SpanList
{
public:
    SpanList(){
        _head= new Span;
        _head->_next=_head;
        _head->_prev=_head;
    }
private:
    Span* _head;
public:
    std::mutex _mtx;
};




