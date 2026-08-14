// 测试1：基础正确性
// 覆盖：分配/释放往返、写读校验、多对象共存不重叠、乱序释放、反复分配释放稳定性
#include "ConcurrentAlloc.h"
#include <cstdio>

static int g_fail = 0;

// 对一块内存写特征数据并读回校验；返回是否一致
static bool FillAndVerify(void* ptr, size_t size, unsigned seed)
{
	unsigned char* p = (unsigned char*)ptr;
	for (size_t i = 0; i < size; ++i)
		p[i] = (unsigned char)((i * 131u + seed * 17u) & 0xFF);
	for (size_t i = 0; i < size; ++i)
		if (p[i] != (unsigned char)((i * 131u + seed * 17u) & 0xFF))
			return false;
	return true;
}

static bool TestSingleSize(size_t size)
{
	void* ptr = ConcurrentAlloc(size);
	if (ptr == nullptr)
	{
		printf("  [FAIL] size=%zu 返回NULL\n", size);
		return false;
	}
	bool ok = FillAndVerify(ptr, size, (unsigned)size);
	if (!ok)
		printf("  [FAIL] size=%zu 写读不一致\n", size);
	ConcurrentFree(ptr);
	return ok;
}

// 同时持有 1000 个 64B 对象，验证互不重叠、数据各自完整，然后乱序释放
static bool TestManyObjects()
{
	const size_t N = 1000;
	const size_t SIZE = 64;
	void* arr[N];

	for (size_t i = 0; i < N; ++i)
	{
		arr[i] = ConcurrentAlloc(SIZE);
		if (arr[i] == nullptr)
		{
			printf("  [FAIL] 多对象分配返回NULL\n");
			return false;
		}
		unsigned char* p = (unsigned char*)arr[i];
		for (size_t j = 0; j < SIZE; ++j)
			p[j] = (unsigned char)(i * 3u + j);
	}
	for (size_t i = 0; i < N; ++i)
	{
		unsigned char* p = (unsigned char*)arr[i];
		for (size_t j = 0; j < SIZE; ++j)
			if (p[j] != (unsigned char)(i * 3u + j))
			{
				printf("  [FAIL] 对象%zu 数据被破坏（疑似地址重叠）\n", i);
				return false;
			}
	}
	// 乱序释放（倒序），验证释放顺序不影响正确性
	for (size_t i = N; i > 0; --i)
		ConcurrentFree(arr[i - 1]);
	return true;
}

// 混合大小连续分配/释放
static bool TestMixedLoop()
{
	const size_t sizes[] = {
		1, 2, 3, 7, 8, 9, 15, 16, 17, 31, 32, 33, 63, 64, 65, 100,
		127, 128, 129, 255, 256, 257, 511, 512, 513, 1023, 1024, 1025,
		2048, 4096, 8191, 8192, 8193, 16384, 65535, 65536, 65537,
		131072, 262144
	};
	for (size_t s : sizes)
	{
		void* p = ConcurrentAlloc(s);
		if (p == nullptr || !FillAndVerify(p, s, (unsigned)s))
		{
			printf("  [FAIL] 混合大小 size=%zu\n", s);
			return false;
		}
		ConcurrentFree(p);
	}
	return true;
}

// 反复分配释放，验证长时间稳定
static bool TestStability()
{
	for (int round = 0; round < 2000; ++round)
	{
		size_t s = 1 + round % 512;
		void* p = ConcurrentAlloc(s);
		if (p == nullptr || !FillAndVerify(p, s, (unsigned)round))
		{
			printf("  [FAIL] 稳定性 round=%d size=%zu\n", round, s);
			return false;
		}
		ConcurrentFree(p);
	}
	return true;
}

int main()
{
	const size_t sizes[] = {
		1, 2, 3, 7, 8, 9, 15, 16, 17, 31, 32, 33, 63, 64, 65, 100,
		127, 128, 129, 255, 256, 257, 511, 512, 513, 1023, 1024, 1025,
		2048, 4096, 8191, 8192, 8193, 16384, 65535, 65536, 65537,
		131072, 262144
	};

	// 1. 每个大小单独分配/释放 + 写读校验
	for (size_t s : sizes)
		if (!TestSingleSize(s))
			++g_fail;

	// 2. 多对象共存 + 乱序释放
	if (!TestManyObjects())
		++g_fail;

	// 3. 混合大小
	if (!TestMixedLoop())
		++g_fail;

	// 4. 反复分配释放
	if (!TestStability())
		++g_fail;

	printf("======== 基础正确性测试结果 ========\n");
	if (g_fail == 0)
		printf("全部通过\n");
	else
		printf("失败 %d 项\n", g_fail);
	return g_fail ? 1 : 0;
}
