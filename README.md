# cache_learning · C++ 缓存淘汰算法学习

这个仓库记录我学习 C++ 缓存替换策略的过程，围绕 LRU、LFU、ARC 及其变体，保存参考工程、自写练习、代码注释和命中率实验。阅读时建议先运行参考版本，再对照个人实现理解数据结构、淘汰过程和并发访问。

## 目录与完成情况

| 路径 | 内容与状态 |
| --- | --- |
| [`CacheSystem/KamaCache-main 17.02.57/`](<CacheSystem/KamaCache-main 17.02.57/>) | KamaCache 参考工程；包含 CMake、算法实现、命中率演示和结果截图，可作为运行入口 |
| [`CacheSystem/self_write_code/`](CacheSystem/self_write_code/) | 对照学习后编写、注释的实现；仍有编译与接口问题，属于进行中的练习 |
| [`code_learning/`](code_learning/) | LRU、LFU、ARC 的独立算法学习版本，未组成统一构建工程 |
| [`CacheSystem/README2.md`](CacheSystem/README2.md) | 个人学习记录与已有问题说明 |

参考项目与自写代码保留在不同目录，方便对照。参考工程沿用了原项目说明和许可证，个人练习的完成度以实际源码和构建结果为准。

## 主要内容

| 策略 | 学习重点 |
| --- | --- |
| LRU | 哈希索引与双向链表，按最近访问顺序淘汰 |
| LRU-K | 记录访问历史，达到访问次数阈值后进入主缓存 |
| LFU | 访问频率分组，以及同频率下的节点淘汰 |
| LFU-Aging | 频率老化，减弱历史高频数据的长期影响 |
| ARC 相关实现 | 对照参考工程研究近期性、频率与自适应调整；具体行为以代码实现为准 |
| 分片缓存 | 通过多个缓存分片研究锁竞争与容量分配 |

统一策略接口位于 `KICachePolicy.h`：`put(key, value)` 写入数据，`get(key, value)` 通过返回值表示是否命中。

## 快速运行参考工程

环境要求：支持 C++17 的编译器；使用 CMake 时需 CMake 3.10 或更高版本。在仓库根目录执行：

```bash
cmake -S "CacheSystem/KamaCache-main 17.02.57" -B build/reference
cmake --build build/reference -j
./build/reference/main
```

也可以直接编译现有演示程序：

```bash
mkdir -p build
c++ -std=c++17 -O2 -pthread \
  "CacheSystem/KamaCache-main 17.02.57/testAllCachePolicy.cpp" \
  -o build/cache_benchmark
./build/cache_benchmark
```

程序会输出三类场景下 LRU、LFU、ARC、LRU-K 和 LFU-Aging 的命中率：热点数据访问、循环扫描、工作负载剧烈变化。

参考目录内原有的 `build/` 是历史构建产物，可能包含其他机器的绝对路径；以上命令使用新的输出目录重新构建。

## 实验结果如何理解

2026-10-18 使用 Apple Clang 按上面的直接编译命令完成构建，现有程序的三个场景均正常运行。实验截图保存在 [参考工程的 images 目录](<CacheSystem/KamaCache-main 17.02.57/images/>)。

当前演示使用随机数生成访问请求，各策略会依次消耗随机数流，因此并非对完全相同的访问序列进行严格对照。输出用于观察策略在不同访问模式下的表现，不能据此得出固定的性能排名；现有程序也未提供完整的并发正确性或吞吐量评测。

## 自写版本的已知问题

`self_write_code` 尚未通过完整编译。当前可见的问题包括依赖类型前缺少 `typename`、使用 `std::thread` 时缺少相应头文件，以及 ARC 部分的类名、析构函数和接口签名不一致。该目录适合继续排查与完善；快速体验应使用上面的参考工程。

后续可先统一接口并修复编译，再补充小容量淘汰、重复更新、未命中、频率老化和并发访问等针对性验证。

## 参考与学习记录

- [KamaCache 原项目](https://github.com/youngyangyang04/KamaCache)
- [保留的参考项目说明](<CacheSystem/KamaCache-main 17.02.57/README.md>)
- [参考目录中的许可证](<CacheSystem/KamaCache-main 17.02.57/LICENSE>)
- [个人学习博客](https://kidle9527.github.io/)
