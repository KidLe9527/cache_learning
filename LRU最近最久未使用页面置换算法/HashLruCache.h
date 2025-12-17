// LRU优化：分片LRU（Sharded LRU），核心解决单LRU在高并发下的锁竞争问题
// 原理：将总缓存容量拆分为多个独立LRU分片，每个分片有独立锁，并发访问时仅竞争对应分片的锁
template<typename Key, typename Value>
class KHashLruCaches    // 分片LRU无需继承基类，因为它是“LRU集群管理器”，而非单个LRU实现
{
public:
    // 构造函数：初始化分片LRU
    // capacity：缓存总容量；sliceNum：分片数量（默认取CPU核心数，适配并发能力）
    KHashLruCaches(size_t capacity, int sliceNum)
        : capacity_(capacity)
        // 分片数兜底：若传入≤0，则取系统支持的并发线程数（CPU核心数），最大化多核利用
        , sliceNum_(sliceNum > 0 ? sliceNum : std::thread::hardware_concurrency())  
    {
        // 计算每个分片的容量：向上取整（避免总容量不足）
        // 例如：总容量10，分片数3 → 每个分片4（4+3+3=10，或4+4+2，向上取整确保总容量≥设定值）
        // static_cast<double>：将capacity转为浮点型，确保除法是浮点运算（否则整数除法会丢失精度）
        size_t sliceSize = std::ceil(capacity / static_cast<double>(sliceNum_)); 
        // 初始化每个分片：每个分片是一个独立的KLruCache实例，用智能指针管理
        for (int i = 0; i < sliceNum_; ++i)
        {
            lruSliceCaches_.emplace_back(std::make_unique<KLruCache<Key, Value>>(sliceSize)); 
        }
    }

    // 写入缓存：根据key的哈希值路由到指定分片，执行put
    void put(Key key, Value value)
    {
        // 1. 计算key的哈希值 → 取模分片数 → 得到唯一分片索引（确保同一个key永远路由到同一个分片）
        size_t sliceIndex = Hash(key) % sliceNum_;  
        // 2. 调用对应分片的put方法（每个分片独立锁，降低并发竞争）
        lruSliceCaches_[sliceIndex]->put(key, value);
    }

    // 读取缓存（输出参数版）：路由到分片，返回是否命中，命中则通过value输出值
    bool get(Key key, Value& value)
    {
        size_t sliceIndex = Hash(key) % sliceNum_;
        return lruSliceCaches_[sliceIndex]->get(key, value);
    }

    // 读取缓存（返回值版）：封装输出参数版，未命中返回默认初始化的value
    Value get(Key key)
    {
        Value value {}; // 默认构造Value（对POD类型等价于清零，对复杂类型调用默认构造函数）
        // 注意：memset仅适用于POD类型（如int/char/结构体无自定义构造），复杂类型（如std::string）调用memset会崩溃
        // memset(&value, 0, sizeof(value)); // 错误！仅POD类型可用，注释掉并修正逻辑
        get(key, value); // 调用输出参数版get，未命中则value保持默认构造值
        return value;
    }

private:
    // 哈希函数：将key转换为size_t类型的哈希值（复用标准库hash函数，保证一致性）
    size_t Hash(const Key& key)
    {
        std::hash<Key> hashFunc;    // 标准库哈希函数对象（对基础类型/string等已重载，自定义类型需手动实现）
        return hashFunc(key);       // 生成key的唯一哈希值（相同key的哈希值永远相同）
    }

private:
    size_t                                              capacity_;  // 缓存总容量（逻辑容量，实际总容量≥该值）
    int                                                 sliceNum_;  // 分片数量（最终为CPU核心数或用户指定值）
    // 分片数组：每个元素是一个独立的KLruCache智能指针，确保每个分片独立管理生命周期
    std::vector<std::unique_ptr<KLruCache<Key, Value>>> lruSliceCaches_; 
};

/*
分片LRU（Sharded LRU）核心设计说明：
==================== 优点 ====================
1. 极致并发性能：
   - 每个分片独立维护锁，并发访问不同分片的key时无锁竞争，高并发下吞吐量提升显著；
   - 分片数默认等于CPU核心数，最大化利用多核处理器优势。
2. 可扩展性强：
   - 分片数可根据业务并发量动态调整（如从4分片扩到8分片），无需修改核心逻辑；
   - 每个分片独立管理容量，可针对不同分片设置差异化策略（如热点分片扩容）。
3. 故障隔离：
   - 单个分片的异常（如锁死、数据损坏）不会影响其他分片，降低整体故障风险。

==================== 缺点 ====================
1. 缓存命中率下降风险：
   - 容量碎片化：总容量拆分后，单个分片满了会淘汰数据，即使其他分片有空闲容量；
   - 路由不均：若hash函数不均匀，部分分片满载、部分空闲，命中率进一步下降（需保证hash函数均匀性）。
2. 内存开销增加：
   - 每个分片需维护独立的LRU数据结构（哈希表+双向链表），额外内存开销约为单LRU的sliceNum_倍（但碎片化内存通常可接受）；
   - 向上取整的分片容量会导致实际总容量略大于设定值，少量冗余内存占用。
3. 实现复杂度提升：
   - 需管理多个LRU实例的生命周期（智能指针），路由逻辑、分片扩容/缩容需额外处理；
   - 自定义Key类型需实现可靠的hash函数，否则会导致路由不均。

==================== 关键注意事项 ====================
1. 哈希路由的确定性：
   - 同一个key的hash值固定，路由到的分片索引固定，不会随机变化（除非修改分片数）；
   - 自定义Key类型必须重载std::hash，否则会使用默认hash（可能导致路由不均）。
2. 分片数选择：
   - 建议等于CPU核心数（std::thread::hardware_concurrency()），平衡并发性能和命中率；
   - 分片数过多（如超过32）会导致命中率暴跌，需谨慎调整。
3. 数据类型兼容性：
   - get方法中禁用memset，改用默认构造（Value{}），适配复杂类型（如std::string、自定义类）；
   - 自定义Value类型需支持默认构造，否则get方法会编译失败。
*/