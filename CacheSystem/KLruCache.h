# pragma once    // 防止多重包含

#include "KICachePolicy.h"
#include <memory>   // std::shared_ptr
#include <unordered_map>
#include <mutex>    // std::mutex
#include <vector>
#include <cmath>    // std::ceil


namespace KamaCache
{

template <class Key, class Value> class KLruCache; // 前向声明缓存类, 以便友元声明使用

template <class Key, class Value>
class LruNode   // Lru缓存节点类
{
private:
    Key key_;
    Value value_;
    size_t accessCount_;   // 访问计数, 用于LRU-K算法扩展   // 修正：命名统一为驼峰式 accessCount_
    std::weak_ptr<LruNode<Key, Value>> prev_;   // 前驱节点, 使用弱指针防止循环引用
    std::shared_ptr<LruNode<Key, Value>> next_;

public:
    LruNode(Key key, Value value) : key_(key), value_(value), accessCount_(1), prev_ (), next_(nullptr) {}   // 构造函数，这里指针的初始化：weak_ptr，prev_()、prev_(nullptr)、prev_{} 效果完全一致；

    // 提供必要的访问器和修改器
    Key getKey() const { return key_; }  // const成员函数, 不修改对象状态
    Value getValue() const { return value_; }
    size_t getAccessCount() const { return accessCount_; }
    void setValue(const Value& value) { value_ = value; }
    void incrementAccessCount() { ++accessCount_; }

    friend class KLruCache<Key, Value>; // 友元，让缓存类访问私有成员 ---> 需要提前声明缓存类
};

// 基础版本的 LRU 缓存实现
template <class Key, class Value>
class KLruCache : public KICachePolicy<Key, Value>
{
public:
    using LruNodeType = LruNode<Key, Value>; // 节点类型
    using NodePtr = std::shared_ptr<LruNodeType>; // 节点指针类型, 使用shared_ptr管理内存
    using NodeMap = std::unordered_map<Key, NodePtr>; // key -> Node 映射

    KLruCache(int capacity) : capacity_(capacity)
    {
        initializeList();   // 初始化双向链表
    }
    ~KLruCache() override = default; // 默认析构函数, 使用override确保正确覆盖基类的虚析构函数

    bool get(const Key& key, Value& value) override {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = nodeMap_.find(key);  // 查找key, 返回迭代器
        if (it == nodeMap_.end())   return false;  // 未找到, 返回false
        value = it->second->getValue(); // 获取节点值
        moveToMostRecent(it->second); // 将节点移动到最近使用位置
        return true;
    }

    Value get(const Key& key) override {
        Value value {};
        get(key, value);  // 调用重载的 get 方法，如果key 不存在，value 保持默认值
        return value;
    }

    void put(const Key& key, const Value& value) override {
        if (capacity_ <= 0)  return;

        std::lock_guard<std::mutex> lock(mutex_);
        auto it = nodeMap_.find(key);
        if(it == nodeMap_.end())  return addNewNode(key, value); // 没在哈希表中，说明是新节点，添加新节点。注意这里直接return，避免else嵌套
        // 已存在节点，更新值并移动到最近使用位置
        it->second->setValue(value);    // it->second 是对应的节点指针 NodePtr, 指向的是 LruNode 对象，其value成员是私有的，需要通过setValue方法更新
        moveToMostRecent(it->second);
    }

    bool remove(const Key& key) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = nodeMap_.find(key);
        if(it != nodeMap_.end()) {
            removeNode(it->second); // 删除双向链表中的节点
            nodeMap_.erase(it); // 从哈希表中删除该节点，直接传参iterator
            return true;
        }
        return false;
    }

private:
    int                                 capacity_;   // 缓存容量
    NodePtr                             dummyHead_; // 虚拟头节点
    NodePtr                             dummyTail_; // 虚拟尾节点
    NodeMap                             nodeMap_; // key -> 节点映射 哈希表
    std::mutex                          mutex_; // 互斥锁，保护多线程访问

    // 初始化双向链表
    void initializeList() {
        // 创建头尾虚拟指针节点
        dummyHead_ = std::make_shared<LruNodeType>(Key(), Value()); // key 和 value 使用默认构造函数初始化; 使用shared_ptr管理内存，
        dummyTail_ = std::make_shared<LruNodeType>(Key(), Value()); // 相比（-1， -1）更通用，适用于任意类型的Key和Value
        dummyHead_->next_ = dummyTail_;
        dummyTail_->prev_ = dummyHead_; // prev_ 是 weak_ptr, 接收 shared_ptr 并弱引用，防止循环引用 --> 仅对 dummyHead_ 的数据形成 “监管”，dummyHead_ 的引用计数完全不变。
    }

    // 将该节点移动到最新的位置
    void moveToMostRecent(const NodePtr& node) {
        removeNode(node);
        insertNode(node);
    }

    // 从链表中移除节点
    void removeNode(const NodePtr& node) {
        auto prevNode = node->prev_.lock(); // 获取前驱节点的 shared_ptr
        if(!prevNode || !node->next_) return; // 安全检查

        prevNode->next_ = node->next_;
        node->next_->prev_ = prevNode;

        node->next_ = nullptr;  // 前驱和后继指针断开，帮助内存回收
    }

    // 在链表尾部插入节点
    void insertNode(const NodePtr& node) {
        auto tailPrev = dummyTail_->prev_.lock();
        if (!tailPrev) return; // 新增：空指针安全检查

        node->next_ = dummyTail_;
        node->prev_ = dummyTail_->prev_;
        tailPrev->next_ = node;
        dummyTail_->prev_ = node;
    }

    // 添加新节点
    void addNewNode(const Key& key, const Value& value) {
        if (nodeMap_.size() >= capacity_) {
            evictLeastRecent(); // 驱逐最近最少访问
        }
        NodePtr newNode = std::make_shared<LruNodeType>(key, value);
        insertNode(newNode);
        nodeMap_[key] = newNode; // 注意操作新节点需要在链表和哈希表中都进行
    }

    // 驱逐最近最少访问的节点
    void evictLeastRecent() {
        // if(dummyHead_->next_ == dummyTail_) return; // 赘余了，addNewNode 中仅当 nodeMap_.size()>=capacity_ 才调用，此时链表非空
        NodePtr lruNode = dummyHead_->next_; // 最近最少使用的节点在头节点后面
        if(!lruNode || lruNode == dummyTail_) return; // 安全检查
        removeNode(lruNode);
        nodeMap_.erase(lruNode->getKey()); // 从哈希表中删除, 调用getKey()获取节点的key
    }

};

// LRU优化：Lru-k版本。 通过继承的方式进行再优化
template <class Key, class Value>
class KLruKCache : public KLruCache<Key, Value>
{
public:
    KLruKCache(int capacity, int historyCapacity, size_t k)
        : KLruCache<Key, Value>(capacity)   // 调用基类构造函数，初始化主缓存
        , accessCountCache_(std::make_unique<KLruCache<Key, size_t>>(historyCapacity))  // 初始化访问计数缓存，调用基类构造函数
        , k_(k) // 初始化访问阈值
    {}

    ~KLruKCache() override = default;    // 默认析构函数

    bool get(const Key& key, Value& value) override {
        bool inMainCache = KLruCache<Key, Value>::get(key, value); // 显式调用基类的 get 方法, 这里如果未命中，value 保持默认值吗？

        size_t historyCount = 0;
        bool hasHistory = accessCountCache_->get(key, historyCount);
        if (!hasHistory) {
            historyCount = 0; // 未找到时初始化为0
        }
        historyCount++;
        accessCountCache_->put(key, historyCount); // 更新访问计数缓存

        if (inMainCache) return true;
        // 不在主缓存中，检查历史值映射

        if(historyCount >= k_) {
            auto it = historyValueMap_.find(key);
            if (it != historyValueMap_.end()) {
                value = it->second; // 获取历史值

                // 从历史映射中移除该记录
                historyValueMap_.erase(it);
                accessCountCache_->remove(key); // 从访问计数缓存中移除该记录

                // 将该项插入主缓存 !!!
                KLruCache<Key, Value>::put(key, value);
                return true;
            }
        }

        return false;
    }

    Value get(const Key& key) override {
        Value value {};
        get(key, value);
        return value;
    }

    void put(const Key& key, const Value& value) override {
        // 1. 检查主缓存是否存在（显式调用基类，对齐参考代码）
        bool inMainCache = KLruCache<Key, Value>::get(key, Value {}); 
        if (inMainCache) {    
            // 已存在，更新主缓存值
            KLruCache<Key, Value>::put(key, value); 
            return;
        }

        // 2. 读取并更新访问计数（显式判空，对齐参考代码）
        size_t historyCount = 0;
        bool hasHistory = accessCountCache_->get(key, historyCount);
        if (!hasHistory) {
            historyCount = 0; // 未找到时初始化为0
        }
        historyCount++;
        accessCountCache_->put(key, historyCount); 

        // 3. 更新历史值映射（补充：处理计数缓存淘汰，避免map膨胀）
        historyValueMap_[key] = value; 

        // 4. 达到阈值，移入主缓存
        if(historyCount >= k_) {
            accessCountCache_->remove(key); 
            historyValueMap_.erase(key); 
            KLruCache<Key, Value>::put(key, value); 
        }
    }

private:
    size_t k_; // 访问阈值
    /*相比基础Lru，Lru-k通过引入访问计数缓存和历史值映射，实现更精细的缓存管理策略*/
    std::unique_ptr<KLruCache<Key, size_t>> accessCountCache_; // 记录 访问计数 的LRU缓存,即访问数据历史记录
    std::unordered_map<Key, Value> historyValueMap_; // 记录历史值的映射
};

// lru优化：对lru进行分片，提高高并发使用的性能 注意：这其实是一个LruCache的容器类
template <class Key, class Value>
class KhashLruCaches {
public:
    KhashLruCaches(int capacity, int sliceNum)
        : capacity_(capacity)
        , sliceNum_(sliceNum > 0 ? sliceNum : std::thread::hardware_concurrency())  // 默认分片数为CPU核心数
    {
        size_t slicecapacity = std::ceil(static_cast<double>(capacity_ ) / sliceNum_); // 计算每个分片的容量，向上取整 // 先将 capacity_ 转 double 再除法，保证精度
        for(int i = 0; i < sliceNum_; ++i) {
            lruSliceCaches_.emplace_back(std::make_unique<KLruCache<Key, Value>>(slicecapacity)); // 初始化每个分片
        }
    }

    void put(const Key& key, const Value& value) {
        size_t sliceNum_index = Hash(key) % sliceNum_; // 计算分片索引
        lruSliceCaches_[sliceNum_index]->put(key, value); // 调用对应分片的put方法
    }

    bool get(const Key& key, Value& value) {
        size_t sliceNum_index = Hash(key) % sliceNum_;
        return lruSliceCaches_[sliceNum_index]->get(key, value);
    }

    Value get(const Key& key) {
        Value value {};
        // size_t sliceNum_index = Hash(key) % sliceNum_;
        // lruSliceCaches_[sliceNum_index]->get(key, value);   // 这里是调用对应分片的get方法
        get(key, value); // 复用上面的get方法
        return value;
    }

private:
    size_t Hash(const Key& key) {
        std::hash<Key> hashFunc;
        return hashFunc(key);
    }

    size_t capacity_;  // 总容量
    int sliceNum_;  // 分片数量
    // 分片数组：每个元素是一个独立的KLruCache智能指针，确保每个分片独立管理生命周期
    std::vector<std::unique_ptr<KLruCache<Key, Value>>> lruSliceCaches_;
};

} // namespace KamaCache