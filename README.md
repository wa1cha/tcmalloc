# 高并发内存池（仿 Google tcmalloc）

一个参照 Google **tcmalloc** 设计思想实现的高并发内存池，用于解决多线程环境下频繁 `malloc/free` 带来的三类问题：**锁竞争、内存碎片、频繁系统调用**。

## 解决什么问题

系统 `malloc/free` 在多线程高频调用时存在明显短板：

- **锁竞争**：全局分配器一把大锁，线程越多冲突越严重。
- **系统调用开销**：每次分配/释放都可能触发 `mmap`/`VirtualFree`，成本高昂。
- **内存碎片**：大量小块随机分配/释放后，内存难以复用。

本项目的思路是**用空间换时间**：每个线程持有自己的缓存，绝大多数分配/释放在线程内无锁完成；跨线程的少量分配才去中心/页缓存，并尽量减少锁的粒度。

## 整体架构（三层缓存 + 一个对象池）

```
┌───────────────────────────────────────────────┐
│               用户调用入口                     │
│        ConcurrentAlloc / ConcurrentFree       │
└───────────────────────┬───────────────────────┘
                        │
           ┌────────────┴────────────┐
           │   size > 256KB ?        │
           │   大内存直通 PageCache   │
           ▼                        ▼
  ┌─────────────┐        ┌─────────────────────┐
  │ ThreadCache │ ◄────► │ CentralCache        │
  │ 每线程独享   │  批量   │ SpanList[208] 按桶锁 │
  │ 无锁        │   取/还 │ 切分 span 成小对象   │
  └─────────────┘        └──────────┬──────────┘
           ▲                        │
           └────────────────────────┘
                                    ▼
                    ┌─────────────────────────────┐
                    │ PageCache（页缓存）           │
                    │ SpanList[129] + 页号映射表    │
                    │ 按页(8KB)管理 / 前后页合并     │
                    │ SystemAlloc / SystemFree     │
                    └─────────────────────────────┘
```

### 各层职责

| 层 | 职责 | 锁粒度 |
|----|------|--------|
| **ThreadCache** | 每线程独享 208 个自由链表桶，分配/释放无锁 | 无锁 |
| **CentralCache** | 按大小类维护 Span，向线程缓存批量下发/回收对象 | 每桶一把锁 |
| **PageCache** | 按页(8KB)管理大内存，span 切分与前后合并 | 全局一把锁 |
| **ObjectPool** | 复用 Span / ThreadCache 对象，避免内存池内部 `new` | — |

- 大于 **256KB**（`MAX_BYTES`）的申请直接走 PageCache，不进线程/中心缓存。

## 核心设计

### 1. 大小类（SizeClass）
把申请大小对齐到 208 个"桶"之一，分段对齐控制内部碎片：

| 字节范围 | 对齐单位 | 桶号区间 |
|----------|----------|----------|
| [1, 128] | 8B | [0, 16) |
| [128, 1KB] | 16B | [16, 72) |
| [1KB, 8KB] | 128B | [72, 128) |
| [8KB, 64KB] | 1KB | [128, 184) |
| [64KB, 256KB] | 8KB | [184, 208) |

对齐值 / 区间上限 ≈ 1.5%~6%，把内碎片控制在约 10% 左右。

### 2. Span 与 SpanList
- **Span**：一段连续内存的最小管理单元，记录起始页号、页数、已借出对象数等；可整体作为大块，也可切割成小对象自由链表。
- **SpanList**：带头双向循环链表，每个桶配一把锁。

### 3. 慢启动批量获取
ThreadCache 某个桶首次向 CentralCache 取对象时只取 1 个（`MaxSize` 从 1 开始），用完之后 `MaxSize++` 动态增长，避免一次取太多用不完浪费，也避免取太少频繁访问中心缓存。

### 4. 页管理与前后合并
PageCache 用页号做"字节 ⇄ 页"换算（`addr >> PAGE_SHIFT` / `pageId << PAGE_SHIFT`）。Span 归还时与相邻空闲页合并，缓解外部碎片。

### 5. TLS（线程局部存储）
每个线程通过 `thread_local` 持有自己的 `ThreadCache`，实现无锁访问。

### 6. 锁顺序约定（防死锁）
CentralCache 的桶锁与 PageCache 的全局锁**绝不同时持有**：需要 PageCache 时先释放桶锁再拿页锁，避免加锁顺序不一致导致死锁。

## 目录结构

| 文件 | 职责 |
|------|------|
| `Common.h` | 公共基础：常量、SystemAlloc/Free、FreeList、SizeClass、Span、SpanList |
| `ObjectPool.h` | 通用对象池模板，高效复用 Span/ThreadCache 对象 |
| `ThreadCache.h/.cpp` | 线程缓存：无锁自由链表 + 慢启动批量获取 |
| `CentralCache.h/.cpp` | 中心缓存：Span 切割、对象批量下发与回收（单例） |
| `PageCache.h/.cpp` | 页缓存：Span 管理、前后页合并、页号映射（单例） |
| `ConcurrentAlloc.h` | 对外统一分配/释放接口 |
| `Benchmark.cpp` | 与系统 `malloc/free` 的并发性能对比 |
| `UnitTest.cpp` | 正确性测试：TLS、大小类、多线程、大块分配 |

## 构建与运行

> 本代码为 C++11，可在 macOS / Linux 上用 g++ / clang++ 编译。

```bash
# 基准测试（4 线程 × 10 轮 × 1000 次/轮）
g++ -std=gnu++17 -O2 -pthread ThreadCache.cpp CentralCache.cpp PageCache.cpp Benchmark.cpp -o benchmark
./benchmark

# 单元测试
g++ -std=gnu++17 -O2 -pthread ThreadCache.cpp CentralCache.cpp PageCache.cpp UnitTest.cpp -o unittest
./unittest
```

### Linux 平台注意

- 编译请用 `-std=gnu++17`（**gnu** 模式），否则 glibc 会因特性测试宏把 `mmap` 等 POSIX 声明隐藏掉，报"未定义"。
- 原代码中 `ThreadCache.h` 第 22 行的 `_declspec(thread)` 是 Windows 专属写法，在 Linux 上需要改为 C++11 标准关键字：

  ```cpp
  static thread_local ThreadCache* pTLSThreadCache = nullptr;
  ```

## 性能说明

Benchmark 会分别统计多线程下 `malloc/free` 与本池 `ConcurrentAlloc/ConcurrentFree` 的总耗时。在分配密集、多线程场景下，本池因线程内无锁分配而明显占优；极端小对象、单线程场景收益有限，这是以空间换时间的典型取舍。

## 参考

本项目为仿 **Google tcmalloc** 的教学简化实现，仅供学习内存分配器设计之用。
