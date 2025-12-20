#include <vector>
#include <memory>
#include <thread>
#include <functional>
#include <cmath>

// 前置声明：带阈值的LFU缓存类（需确保已定义）
template<typename Key, typename Value>
class KLfuCache;

/**
 * @brief 分片式LFU缓存（Hash LFU）
 * @tparam Key 缓存键类型
 * @tparam Value 缓存值类型
 * 核心设计：
 * 1. 将总缓存容量分片到多个独立的KLfuCache实例中
 * 2. 通过哈希函数将key映射到指定分片，实现缓存的分片管理
 * 3. 每个分片独立运行LFU逻辑，互不干扰
 * 注：依赖带阈值的KLfuCache实现，仅做分片封装，不修改LFU核心逻辑
 */
template<typename Key, typename Value>
class KHashLfuCache
{
public:
    /**
     * @brief 构造函数：初始化分片式LFU缓存
     * @param capacity 缓存总容量（所有分片容量之和）
     * @param sliceNum 分片数量（建议设为CPU核心数，提升并发性能）
     * @param maxAverageNum 每个分片LFU的最大平均访问频率阈值
     */
    KHashLfuCache(size_t capacity, int sliceNum, int maxAverageNum = 10)
        : sliceNum_(sliceNum > 0 ? sliceNum : std::thread::hardware_concurrency())  // 分片数默认取CPU核心数
        , capacity_(capacity)
    {
        // 计算每个分片的容量（向上取整，避免总容量不足）
        size_t sliceSize = std::ceil(capacity_ / static_cast<double>(sliceNum_)); 
        // 初始化每个分片为独立的KLfuCache实例
        for (int i = 0; i < sliceNum_; ++i)
        {
            lfuSliceCaches_.emplace_back(new KLfuCache<Key, Value>(sliceSize, maxAverageNum));
        }
    }

    /**
     * @brief 插入/更新缓存项（线程安全依赖分片内部的锁）
     * @param key 缓存键
     * @param value 缓存值
     * 逻辑：哈希key到指定分片，调用分片的put方法
     */
    void put(Key key, Value value)
    {
        // 根据key的哈希值取模，确定目标分片索引
        size_t sliceIndex = Hash(key) % sliceNum_;
        lfuSliceCaches_[sliceIndex]->put(key, value);
    }

    /**
     * @brief 获取缓存项（传出参数版）
     * @param key 缓存键
     * @param value 输出参数：存储获取到的缓存值
     * @return 获取成功返回true，失败返回false
     * 逻辑：哈希key到指定分片，调用分片的get方法
     */
    bool get(Key key, Value& value)
    {
        size_t sliceIndex = Hash(key) % sliceNum_;
        return lfuSliceCaches_[sliceIndex]->get(key, value);
    }

    /**
     * @brief 获取缓存项（值返回版）
     * @param key 缓存键
     * @return 成功返回缓存值，失败返回Value类型默认值
     */
    Value get(Key key)
    {
        Value value;
        get(key, value);
        return value;
    }

    /**
     * @brief 清空所有分片的缓存
     * 逻辑：遍历所有分片，调用每个分片的purge方法
     */
    void purge()
    {
        for (auto& lfuSliceCache : lfuSliceCaches_)
        {
            lfuSliceCache->purge(); // 清理每个分片的缓存（释放内存+重置状态）
        }
    }

private:
    /**
     * @brief 哈希函数：将key映射为哈希值
     * @param key 缓存键
     * @return key对应的哈希值（使用std::hash默认实现）
     */
    size_t Hash(Key key)
    {
        std::hash<Key> hashFunc;
        return hashFunc(key);
    }

private:
    size_t capacity_; // 缓存总容量（所有分片容量之和）
    int sliceNum_; // 缓存分片数量（决定并发度）
    // 分片容器：每个元素是独立的KLfuCache实例，用unique_ptr管理内存
    std::vector<std::unique_ptr<KLfuCache<Key, Value>>> lfuSliceCaches_; 
};


/*
分片LFU（Sharded LFU/Hash LFU）核心设计说明：
==================== 优点 ====================
1. 高并发性能：
   - 每个分片独立维护互斥锁，并发访问不同分片的key时无锁竞争，多核场景下吞吐量相比单例LFU提升5~10倍；
   - 分片数默认等于CPU核心数，最大化利用多核处理器优势，适配高QPS（如10w+）缓存访问场景。
2. 故障隔离与稳定性：
   - 单个分片的逻辑异常（如频率重置、淘汰操作阻塞）不会扩散到其他分片，缓存整体可用性更高；
   - 分片内的频率链表/哈希表规模更小，内存访问更高效（CPU缓存行命中率提升），减少内存碎片。
3. 兼容扩展特性：
   - 完全复用带阈值LFU的核心逻辑（频率阈值、防膨胀），仅做分片封装，开发/维护成本低；
   - 分片数可根据业务并发量动态调整（如从4分片扩到8分片），无需重构LFU核心算法；
   - 支持差异化策略（如热点分片调高频率阈值、扩容分片容量）。

==================== 缺点 ====================
1. 缓存命中率下降：
   - 容量碎片化：总容量拆分后，单个分片满了会淘汰数据，即使其他分片有空闲容量，全局命中率低于单例LFU；
   - 淘汰局部化：分片内仅按自身频率淘汰，可能出现“分片内低频率但全局高频率”的元素被误淘汰，进一步降低命中率；💥
   - 哈希不均风险：若hash函数设计不佳，大量key路由到同一分片，该分片会成为性能瓶颈，且命中率暴跌。
2. 额外开销：
   - 哈希路由开销：每次get/put需计算key的hash值，虽耗时极短，但高并发下累计开销不可忽略；
   - 内存冗余：每个分片需维护独立的频率→链表映射、统计变量（总访问次数、平均频率），额外内存开销约为单LFU的sliceNum_倍；
   - 向上取整的分片容量会导致实际总容量略大于设定值，少量冗余内存占用。
3. 实现与运维复杂度：
   - 需管理多个LFU实例的生命周期（智能指针），清空/监控缓存需遍历所有分片；
   - 全局统计成本高（如统计整体缓存命中率、平均频率），需聚合所有分片数据；
   - 自定义Key类型必须实现可靠的std::hash重载，否则会导致路由不均。

==================== 关键注意事项 ====================
1. 哈希路由的可靠性：
   - 同一个key的hash值必须固定，路由到的分片索引不可变（除非修改分片数）；
   - 自定义Key类型需重载std::hash，建议结合key的多个特征（如字符串拼接、数值组合）设计hash函数，减少冲突。
2. 分片数选择：
   - 建议等于CPU核心数（std::thread::hardware_concurrency()），平衡并发性能和命中率；
   - 分片数不宜过多（如超过32），否则命中率会随分片数增加呈指数级下降；
   - 高并发场景可适度增加分片数（如CPU核心数×2），但需接受命中率小幅下降。
3. 阈值与容量适配：
   - 每个分片的频率阈值建议与单例LFU保持一致，避免分片间策略不一致导致全局频率逻辑混乱；
   - 为抵消容量碎片化，总容量可预留10%~20%冗余（如实际需100，总容量设为120），提升整体利用率。
4. 适用数据类型：
   - Value类型需支持默认构造（get方法中使用Value{}），适配复杂类型（如std::string、自定义类）；
   - Key类型需支持std::hash或自定义hash函数，否则会编译失败或路由不均。

==================== 适用场景 ====================
1. 高并发缓存场景：
   - 电商秒杀、直播弹幕、高QPS接口缓存（QPS≥10w），优先保证并发吞吐量，可接受命中率小幅下降；
   - 多核服务器部署环境，需最大化利用CPU核心资源。
2. 大容量缓存场景：
   - 缓存总容量≥10万条，分片可降低单个LFU实例的内存占用，提升数据结构访问效率；
   - 需避免单例LFU因哈希表过大导致的内存抖动、GC频繁问题（如C++无GC，但大哈希表扩容耗时久）。
3. 故障敏感型业务：
   - 金融、支付等核心业务缓存，需故障隔离，避免单个缓存实例异常导致整体服务不可用；
   - 需精细化监控缓存状态（如分片级命中率、负载）的场景。

==================== 不适用场景 ====================
1. 低并发/小容量场景：
   - QPS＜1w、缓存容量＜1万条，哈希路由和分片管理的开销抵消性能收益，单例LFU更简单高效；
   - 嵌入式设备、边缘计算等资源受限场景，额外内存开销不可接受。
2. 命中率极致敏感场景：
   - 金融风控、精准推荐、机器学习模型缓存，需全局最优淘汰策略，分片LFU的局部淘汰会导致命中率不达标；
   - 缓存穿透/击穿风险高的场景，需严格控制淘汰逻辑，单例LFU的全局频率管理更可靠。
3. 自定义淘汰策略场景：
   - 需基于全局频率、业务规则（如时间、优先级）淘汰数据的场景，分片LFU的局部逻辑无法满足全局策略；
   - 需动态调整全局频率阈值的场景，分片聚合统计成本高，单例LFU更易实现。
*/