#include <unordered_map>
#include <memory>
#include <mutex>
#include <climits>  // 这是为了使用 INT_MAX
#include <algorithm>

// 对LFU_max_val.h文件的补全，ai增加注释和改进代码风格

// 前置声明：缓存策略基类（需确保项目中已定义该基类）
template<typename Key, typename Value>
class KICachePolicy {
public:
    virtual ~KICachePolicy() = default;
    virtual void put(Key key, Value value) = 0;
    virtual bool get(Key key, Value& value) = 0;
    virtual Value get(Key key) = 0;
};

template<typename Key, typename Value> 
class KLfuCache;    // 前置声明，以便FreqList类中使用

/**
 * @brief 频率链表类：管理相同访问频率的缓存节点
 * @tparam Key 缓存键类型
 * @tparam Value 缓存值类型
 * 设计要点：
 * 1. 🔥双向链表结构🔥，使用假头/假尾节点简化边界操作
 * 2. 节点使用shared_ptr管理内存，pre指针用weak_ptr打破循环引用
 * 3. 仅负责节点的链表操作，不管理节点生命周期
 */
template<typename Key, typename Value>
class FreqList  // 这是频率链表类
{
private:
    /**
     * @brief 频率链表节点结构
     * 注：pre使用weak_ptr避免与next的shared_ptr形成循环引用
     */
    struct Node // 这是频率链表的双向链表节点   // 这个Node类只在FreqList内部使用，不需要暴露给外部使用 -> 实际上可以单独定义为一个类模板～～
    {
        int freq;                  // 该节点的访问频次
        Key key;                   // 缓存键
        Value value;               // 缓存值
        std::weak_ptr<Node> pre;   // 前驱节点（弱引用，打破循环）
        std::shared_ptr<Node> next;// 后继节点（强引用，管理生命周期）

        /**
         * @brief 默认构造函数（用于创建假头/假尾节点）
         */
        Node() 
            : freq(1), next(nullptr) {}
        
        /**
         * @brief 带键值的构造函数（用于创建业务节点）
         * @param key 缓存键
         * @param value 缓存值
         */
        Node(Key key, Value value) 
            : freq(1), key(key), value(value), next(nullptr) {}
    };

    using NodePtr = std::shared_ptr<Node>; // 节点智能指针类型别名，用share_ptr是因为链表需要管理节点生命周期
    int freq_;                             // 当前链表所有节点的访问频率
    NodePtr head_;                         // 假头节点（简化链表头操作）
    NodePtr tail_;                         // 假尾节点（简化链表尾操作）

public:
    /**
     * @brief 构造函数：初始化频率链表
     * @param n 该链表对应的访问频率值
     */
    explicit FreqList(int n) 
        : freq_(n) 
    {
        head_ = std::make_shared<Node>();  // 创建假头节点  // head_和 tail_必须用 shared_ptr：通过强引用 “锁住” 假头 / 假尾节点，保证它们的生命周期和链表一致，成为链表的稳定边界。
        tail_ = std::make_shared<Node>();  // 创建假尾节点， 1. tail_ 是指向假尾节点的指针，这里假尾节点被强引用，计数由0加1
        head_->next = tail_;               // 初始化链表：头→尾  // 2. head_ 的 next_ 指向 tail_， 这里假尾节点再次被强引用， 计数由1加1为2
        tail_->pre = head_;                // 初始化链表：尾←头 // weak_ptr 的核心用法：weak_ptr 可以接收 shared_ptr 的赋值，本质是 “观察” 这个 shared_ptr 持有的对象，不增加引用计数。假头节点还是1
    }

    /**
     * @brief 判断频率链表是否为空（仅含假头/假尾节点）
     * @return 空返回true，否则返回false
     */
    bool isEmpty() const
    {
        return head_->next == tail_;
    }

    /**
     * @brief 将节点添加到频率链表的尾部（最近使用位置）
     * @param node 待添加的节点指针
     * 设计逻辑：新访问的节点插入到尾节点前，保证链表头部是最久未使用节点
     */
    void addNode(NodePtr node) 
    {
        // 空指针保护
        if (!node || !head_ || !tail_) 
            return;

        // 1. 绑定节点的前驱（尾节点的前驱）和后继（尾节点）
        node->pre = tail_->pre;
        node->next = tail_;
        
        // 2. 更新原尾前驱的后继为当前节点（lock()获取weak_ptr的强引用）
        tail_->pre.lock()->next = node;
        
        // 3. 更新尾节点的前驱为当前节点
        tail_->pre = node;
    }

    /**
     * @brief 从频率链表中移除指定节点
     * @param node 待移除的节点指针
     * 注意：仅移除节点，不释放节点内存（由shared_ptr管理）
     */
    void removeNode(NodePtr node)
    {
        // 空指针/无效节点保护
        if (!node || !head_ || !tail_)
            return;
        if (node->pre.expired() || !node->next) // .expired() 检查 weak_ptr 是否指向已销毁对象, 返回值为 true 则表示前驱节点已不存在
            return;

        // 1. 获取前驱节点的强引用，为什么需要获取强引用？因为要操作前驱节点的 next 指针
        auto pre = node->pre.lock();
        
        // 2. 调整链表指针：前驱→后继，后继←前驱
        pre->next = node->next;
        node->next->pre = pre;
        
        // 3. 断开节点与链表的连接（避免悬空引用）
        node->next = nullptr;
        node->pre.reset();  // reset() 用于清空 weak_ptr 的引用，断开与前驱节点的联系
        //node = nullptr; // 可选：将指针置为空，防止悬空指针
    }

    /**
     * @brief 获取频率链表中第一个业务节点（最久未使用节点）
     * @return 第一个业务节点指针（假头的后继）
     */
    NodePtr getFirstNode() const { 
        return head_->next; 
    }
    
    // 友元声明：允许KLfuCache访问私有成员
    friend class KLfuCache<Key, Value>;
};

/**
 * @brief LFU缓存核心实现类
 * @tparam Key 缓存键类型
 * @tparam Value 缓存值类型
 * 核心设计：
 * 1. 基于频率链表的双层映射：key→节点，freq→频率链表
 * 2. 维护最小访问频率，快速定位待淘汰节点
 * 3. 引入平均访问频率阈值，防止频率值无限增长
 * 4. 线程安全：所有对外操作加互斥锁
 */
template <typename Key, typename Value>
class KLfuCache : public KICachePolicy<Key, Value>
{
public:
    using Node = typename FreqList<Key, Value>::Node;    // 节点类型别名    // typename 用于告诉编译器 Node 是一个类型，而不是其他东西（比如静态成员变量）
    using NodePtr = std::shared_ptr<Node>;               // 节点指针别名
    using NodeMap = std::unordered_map<Key, NodePtr>;    // 键→节点映射别名 --> 节点哈希表

    /**
     * @brief 构造函数：初始化LFU缓存
     * @param capacity 缓存最大容量（最多存储的键值对数量）
     * @param maxAverageNum 最大平均访问频率阈值（防止频率无限增长）
     */
    KLfuCache(int capacity, int maxAverageNum = 10)
        : capacity_(capacity), 
          minFreq_(INT8_MAX),          // 初始最小频率设为极大值，为什么？因为首次插入节点时频率为1，需要更新最小频率为1，那为什么不直接设为1？因为如果初始设为1，插入第一个节点时不会更新最小频率，可能导致逻辑错误
          maxAverageNum_(maxAverageNum),// 最大平均访问频率
          curAverageNum_(0),           // 当前平均访问频率
          curTotalNum_(0),             // 总访问次数（所有节点的频率总和）
          mutex_(std::mutex())         // 互斥锁默认构造    // mutex_() 可以省略，编译器会自动调用默认构造函数
    {}

    /**
     * @brief 析构函数：默认析构（智能指针自动释放节点，需手动释放频率链表）
     */
    ~KLfuCache() override = default;

    /**
     * @brief 插入/更新缓存项（线程安全）
     * @param key 缓存键
     * @param value 缓存值
     * 逻辑：
     * 1. 键已存在：更新值并刷新访问频率
     * 2. 键不存在：新增节点，容量满则淘汰最不常访问项
     */
    void put(Key key, Value value) override
    {
        if (capacity_ == 0)  // 容量为0时直接返回
            return;

        std::lock_guard<std::mutex> lock(mutex_); // 加锁保证线程安全
        auto it = nodeMap_.find(key);
        
        if (it != nodeMap_.end()) // 键已存在：更新值+刷新频率
        {
            it->second->value = value;          // 更新节点值
            getInternal(it->second, value);     // 复用getInternal刷新频率
            return;
        }

        putInternal(key, value); // 键不存在：执行新增逻辑
    }

    /**
     * @brief 获取缓存项（线程安全，传出参数版）
     * @param key 缓存键
     * @param value 输出参数：存储获取到的缓存值
     * @return 获取成功返回true，失败返回false
     */
    bool get(Key key, Value& value) override
    {
        std::lock_guard<std::mutex> lock(mutex_); // 加锁保证线程安全
        auto it = nodeMap_.find(key);
        
        if (it != nodeMap_.end()) // 键存在：刷新频率+返回值
        {
            getInternal(it->second, value);
            return true;
        }

        return false; // 键不存在：返回失败
    }

    /**
     * @brief 获取缓存项（线程安全，值返回版）
     * @param key 缓存键
     * @return 成功返回缓存值，失败返回Value类型默认值
     */
    Value get(Key key) override
    {
        Value value {}; // 默认构造Value类型的返回值
        get(key, value);
        return value;
    }

    /**
     * @brief 清空缓存并回收资源
     * 注意：
     * 1. 清空键→节点映射（shared_ptr自动释放节点）
     * 2. 释放所有频率链表的内存（手动new创建，需手动delete）
     */
    void purge()
    {
        std::lock_guard<std::mutex> lock(mutex_); // 加锁保证线程安全
        
        // 第一步：释放所有手动new的FreqList对象（核心！）
        for (auto& pair : freqToFreqList_) {
            if (pair.second != nullptr) { // 空指针保护
                delete pair.second;       // 释放FreqList对象本身的内存
                pair.second = nullptr;    // 避免悬空指针
            }
        }
        
        // 第二步：清空哈希表（仅移除键值对，不释放哈希表自身内存）
        freqToFreqList_.clear(); // 清空哈希表的键值对，哈希表容器本身由析构函数释放
        
        // 第三步：清空节点映射（触发shared_ptr析构，自动释放Node节点）
        nodeMap_.clear(); // shared_ptr的引用计数归0时，Node内存自动释放
        
        // 第四步：重置状态变量（恢复初始状态）
        minFreq_ = INT8_MAX;
        curAverageNum_ = 0;
        curTotalNum_ = 0;
    }

private:
    /**
     * @brief 内部方法：新增缓存项（已加锁）
     * @param key 缓存键
     * @param value 缓存值
     * 逻辑：
     * 1. 容量满则淘汰最不常访问项
     * 2. 创建新节点并添加到频率链表
     * 3. 更新最小频率和访问统计
     */
    void putInternal(Key key, Value value);

    /**
     * @brief 内部方法：刷新缓存项访问频率（已加锁）
     * @param node 待刷新的节点指针
     * @param value 输出参数：存储节点的缓存值
     * 核心逻辑：
     * 1. 节点频率+1，迁移到新频率链表
     * 2. 更新最小频率（若原频率链表为空）
     * 3. 更新访问统计数据
     */
    void getInternal(NodePtr node, Value& value);

    /**
     * @brief 淘汰最不常访问的缓存项（已加锁）
     * 逻辑：
     * 1. 找到最小频率链表的最久未使用节点
     * 2. 移除节点并更新访问统计
     */
    void kickOut();

    /**
     * @brief 从对应频率链表中移除节点（已加锁）
     * @param node 待移除的节点指针
     */
    void removeFromFreqList(NodePtr node);

    /**
     * @brief 将节点添加到对应频率链表（已加锁）
     * @param node 待添加的节点指针
     * 逻辑：频率链表不存在则创建，再添加节点
     */
    void addToFreqList(NodePtr node);

    /**
     * @brief 更新访问统计：增加总访问次数并计算平均频率
     * 逻辑：
     * 1. 总访问次数+1
     * 2. 重新计算平均频率
     * 3. 检查是否超过阈值，超过则触发频率重置
     */
    void addFreqNum();

    /**
     * @brief 更新访问统计：减少总访问次数并计算平均频率
     * @param num 减少的频率值（淘汰节点的频率）
     */
    void decreaseFreqNum(int num);

    /**
     * @brief 处理平均访问频率超过阈值的情况（已加锁）
     * 核心逻辑：所有节点频率按比例降低，防止频率值无限增长
     */
    void handleOverMaxAverageNum();

    /**
     * @brief 更新当前最小访问频率（已加锁）
     * 逻辑：遍历所有非空频率链表，找到最小频率值
     */
    void updateMinFreq();

private:
    int                                            capacity_;          // 缓存最大容量
    int                                            minFreq_;           // 当前最小访问频率（用于快速淘汰）
    int                                            maxAverageNum_;     // 最大允许的平均访问频率阈值
    int                                            curAverageNum_;     // 当前平均访问频率（总次数/节点数）
    int                                            curTotalNum_;       // 所有节点的访问频率总和
    std::mutex                                     mutex_;             // 互斥锁：保护所有缓存操作的线程安全
    NodeMap                                        nodeMap_;           // 键→节点映射（快速查找节点）---》节点哈希表
    std::unordered_map<int, FreqList<Key, Value>* > freqToFreqList_;    // 频率→频率链表映射（管理同频率节点）--》 频数双向链表哈希表
};



/**
 * @brief 内部方法实现：刷新缓存项访问频率
 */
template<typename Key, typename Value>
void KLfuCache<Key, Value>::getInternal(NodePtr node, Value& value)
{
    // 1. 输出节点值
    value = node->value;
    
    // 2. 从原频率链表中移除节点
    removeFromFreqList(node); 
    
    // 3. 节点频率+1
    node->freq++;
    
    // 4. 添加到新频率链表
    addToFreqList(node);
    
    // 5. 更新最小频率：若原频率是最小频率且链表为空，则最小频率+1
    if (node->freq - 1 == minFreq_ && freqToFreqList_[node->freq - 1]->isEmpty())
        minFreq_++;

    // 6. 更新访问统计
    addFreqNum();
}

/**
 * @brief 内部方法实现：新增缓存项
 */
template<typename Key, typename Value>
void KLfuCache<Key, Value>::putInternal(Key key, Value value)
{   
    // 1. 缓存容量满：淘汰最不常访问项
    if (nodeMap_.size() == capacity_)
    {
        kickOut();
    }
    
    // 2. 创建新节点（初始频率为1）
    NodePtr node = std::make_shared<Node>(key, value);
    
    // 3. 添加到键→节点映射
    nodeMap_[key] = node;
    
    // 4. 添加到频率链表（频率1）
    addToFreqList(node);
    
    // 5. 更新访问统计
    addFreqNum();
    
    // 6. 重置最小频率（新节点频率为1，必然是最小）
    minFreq_ = std::min(minFreq_, 1);
}

/**
 * @brief 内部方法实现：淘汰最不常访问的缓存项
 */
template<typename Key, typename Value>
void KLfuCache<Key, Value>::kickOut()
{
    // 1. 找到最小频率链表的最久未使用节点（链表头部）
    NodePtr node = freqToFreqList_[minFreq_]->getFirstNode();
    
    // 2. 从频率链表中移除节点
    removeFromFreqList(node);
    
    // 3. 从键→节点映射中移除
    nodeMap_.erase(node->key);
    
    // 4. 更新访问统计（减去被淘汰节点的频率）
    decreaseFreqNum(node->freq);
}

/**
 * @brief 内部方法实现：从频率链表中移除节点
 */
template<typename Key, typename Value>
void KLfuCache<Key, Value>::removeFromFreqList(NodePtr node)
{
    // 空指针保护
    if (!node) 
        return;
    
    // 获取节点当前频率，移除节点
    auto freq = node->freq;
    freqToFreqList_[freq]->removeNode(node);
}

/**
 * @brief 内部方法实现：将节点添加到对应频率链表
 */
template<typename Key, typename Value>
void KLfuCache<Key, Value>::addToFreqList(NodePtr node)
{
    // 空指针保护
    if (!node) 
        return;

    // 获取节点频率
    auto freq = node->freq;
    
    // 频率链表不存在则创建
    if (freqToFreqList_.find(node->freq) == freqToFreqList_.end())
    {
        freqToFreqList_[node->freq] = new FreqList<Key, Value>(node->freq);
    }

    // 添加节点到频率链表尾部
    freqToFreqList_[freq]->addNode(node);
}

/**
 * @brief 内部方法实现：增加访问统计
 */
template<typename Key, typename Value>
void KLfuCache<Key, Value>::addFreqNum()
{
    // 总访问次数+1
    curTotalNum_++;
    
    // 计算当前平均频率（避免除零）
    if (nodeMap_.empty())
        curAverageNum_ = 0;
    else
        curAverageNum_ = curTotalNum_ / nodeMap_.size();

    // 平均频率超过阈值：触发频率重置
    if (curAverageNum_ > maxAverageNum_)
    {
       handleOverMaxAverageNum();
    }
}

/**
 * @brief 内部方法实现：减少访问统计
 */
template<typename Key, typename Value>
void KLfuCache<Key, Value>::decreaseFreqNum(int num)
{
    // 总访问次数减去淘汰节点的频率
    curTotalNum_ -= num;
    
    // 重新计算平均频率（避免除零）
    if (nodeMap_.empty())
        curAverageNum_ = 0;
    else
        curAverageNum_ = curTotalNum_ / nodeMap_.size();
}

/**
 * @brief 内部方法实现：处理平均频率超过阈值的情况 🔥
 */
template<typename Key, typename Value>
void KLfuCache<Key, Value>::handleOverMaxAverageNum()
{
    // 空缓存直接返回
    if (nodeMap_.empty())
        return;

    // 遍历所有节点：频率按比例降低（防止频率无限增长）
    for (auto it = nodeMap_.begin(); it != nodeMap_.end(); ++it)
    {
        // 空节点保护
        if (!it->second)
            continue;

        NodePtr node = it->second;

        // 1. 从当前频率链表移除
        removeFromFreqList(node);

        // 2. 频率降低（至少保留1）
        node->freq -= maxAverageNum_ / 2;
        if (node->freq < 1) node->freq = 1;

        // 3. 添加到新频率链表
        addToFreqList(node);
    }

    // 4. 重新计算最小频率
    updateMinFreq();
}

/**
 * @brief 内部方法实现：更新最小访问频率
 */
template<typename Key, typename Value>
void KLfuCache<Key, Value>::updateMinFreq() 
{
    // 初始化为极大值
    minFreq_ = INT8_MAX;    // 为什么？因为要找到最小频率，初始设为极大值，遍历时不断取更小值
    
    // 遍历所有频率链表，找到非空链表的最小频率
    for (const auto& pair : freqToFreqList_) 
    {
        if (pair.second && !pair.second->isEmpty()) 
        {
            minFreq_ = std::min(minFreq_, pair.first);
        }
    }
    
    // 空缓存时重置为1  // 为什么？因为插入第一个节点时频率为1，需要更新最小频率为1
    if (minFreq_ == INT8_MAX) 
        minFreq_ = 1;
}