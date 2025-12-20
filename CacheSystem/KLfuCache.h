#pragma once

#include "KICachePolicy.h"

#include <memory>   // std::shared_ptr
#include <mutex>
#include <unordered_map>
#include <climits>  // for UINT_MAX/INT_MAX
#include <cfloat>   // for FLT_MAX
#include <algorithm> // for std::min/std::max
#include <vector>

namespace KamaCache
{

template <class Key, class Value> class KLfuCache; // 前向声明缓存类, 以便友元声明使用

template <class Key, class Value>
class LfuNode   // Lfu缓存节点类
{
public:
    LfuNode() : key_(), value_(), freq_(1), prev_(), next_(nullptr) {}   // 默认构造函数
    LfuNode(Key key, Value value) : key_(key), value_(value), freq_(1), prev_(), next_(nullptr) {}   // 构造函数

    // 提供必要的访问器和修改器
    Key getKey() const { return key_; }
    Value getValue() const { return value_; }
    size_t getFreq() const { return freq_; }
    void setValue(const Value& value) { value_ = value; }
    void incrementFreq() { ++freq_; }
    void resetFreq(const size_t freq) { freq_ = freq; }
private:
    Key key_;
    Value value_;
    size_t freq_;
    std::weak_ptr<LfuNode<Key, Value>> prev_;
    std::shared_ptr<LfuNode<Key, Value>> next_;

    // fix: 友元声明，让KLfuCache可以访问prev_/next_
    friend class KLfuCache<Key, Value>;
};

template <class Key, class Value>
class FreqList
{
private:
    using LfuNodeType = LfuNode<Key, Value>;
    using NodePtr = std::shared_ptr<LfuNodeType>;

    NodePtr dummyHead_;
    NodePtr dummyTail_;
    size_t freq_;

public: 
    explicit FreqList(size_t freq) : freq_(freq)
    {
        dummyHead_ = std::make_shared<LfuNodeType>();
        dummyTail_ = std::make_shared<LfuNodeType>();
        dummyHead_->next_ = dummyTail_;
        dummyTail_->prev_ = dummyHead_;
    }

    bool isEmpty() const {
        return dummyHead_->next_ == dummyTail_;
    }

    void insertNodeAtTail(const NodePtr& node) {
        if (!node) return;
        auto tailPrev = dummyTail_->prev_.lock();
        if (!tailPrev) return;

        node->next_ = dummyTail_;
        node->prev_ = tailPrev;
        tailPrev->next_ = node;
        dummyTail_->prev_ = node;
    }

    void removeNode(const NodePtr& node) {
        if (!node) return;
        auto prevNode = node->prev_.lock();
        if (!prevNode || !node->next_) return;

        prevNode->next_ = node->next_;
        node->next_->prev_ = prevNode;

        // fix: 彻底清空节点的prev/next引用，避免悬空
        node->next_ = nullptr;
        node->prev_.reset();
    }

    NodePtr removeLeastFrequentNode() {
        if (isEmpty()) return nullptr;
        NodePtr lfuNode = dummyHead_->next_;
        removeNode(lfuNode);
        return lfuNode;
    }

    size_t getFreq() const {
        return freq_;
    }

    NodePtr getFirstNode() const { return dummyHead_->next_; }  // 获取频率列表中的第一个节点,即频率最低的节点
};


template <class Key, class Value>
class KLfuCache : public KICachePolicy<Key, Value>
{
public:
    using LruNodeType = LfuNode<Key, Value>;
    using NodePtr = std::shared_ptr<LruNodeType>;
    using NodeMap = std::unordered_map<Key, NodePtr>; // key 到节点的映射
    using FreqListPtr = std::shared_ptr<FreqList<Key, Value>>;
    using FreqListMap = std::unordered_map<size_t, FreqListPtr>;    // 频率到频率列表的映射

    KLfuCache(size_t capacity, size_t maxAverageFreq = 1000000)
        : capacity_(capacity)
        , minFreq_(1)  // fix: 初始化为1（新节点默认频率），避免UINT_MAX溢出
        , maxAverageFreq_(maxAverageFreq)
        , totalAccesses_(0)
        , curAverageFreq_(0.0)  // fix: 改为浮点型，提高精度
    {}

    ~KLfuCache() override {
        purge();
    }

    // 获取缓存项
    bool get(const Key& key, Value& value) override {
        std::lock_guard<std::mutex> lock(mutex_);
        if(nodeMap_.count(key) == 0) {
            return false; // 缓存未命中
        }
        NodePtr node = nodeMap_[key];
        value = node->getValue();

        // 更新节点的频率（抽离为私有方法，避免重复代码）
        updateNodeFrequency(node);

        // fix: 缓存命中后返回true
        return true;
    }

    Value get(const Key& key) override {
        Value value {};
        get(key, value);
        return value;
    }

    void put(const Key& key, const Value& value) override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (capacity_ == 0) return;

        if (nodeMap_.count(key) > 0) {
            // 更新已有节点的值
            NodePtr node = nodeMap_[key];
            node->setValue(value);
            // fix: 直接调用私有方法更新频率，避免重复加锁
            updateNodeFrequency(node);
            return;
        }

        // 插入新节点
        if (nodeMap_.size() >= capacity_) {
            // 缓存已满，移除最少频率节点
            FreqListPtr minFreqList = freqListMap_[minFreq_];
            NodePtr lfuNode = minFreqList->removeLeastFrequentNode();
            if (lfuNode) {
                // fix: 移除节点时同步更新访问统计
                totalAccesses_ -= lfuNode->getFreq();
                nodeMap_.erase(lfuNode->getKey());
            }
            if (minFreqList->isEmpty()) {
                freqListMap_.erase(minFreq_);
                // minFreq_ 会在后续插入新节点时更新
            }
        }

        // 创建并插入新节点
        NodePtr newNode = std::make_shared<LruNodeType>(key, value);
        nodeMap_[key] = newNode;
        minFreq_ = 1; // 新节点的频率为1，更新 minFreq_
        if (freqListMap_.count(minFreq_) == 0) {
            freqListMap_[minFreq_] = std::make_shared<FreqList<Key, Value>>(minFreq_);
        }
        freqListMap_[minFreq_]->insertNodeAtTail(newNode);

        // 更新访问统计
        totalAccesses_++;
        curAverageFreq_ = (nodeMap_.empty()) ? 0.0 : static_cast<double>(totalAccesses_) / nodeMap_.size();

        // 频率限制逻辑仍保留（新节点频率为1，不会触发，但保留逻辑完整性）
        if (curAverageFreq_ > maxAverageFreq_) {
            limitFrequencyGrowth();
        }
    }

    // 清空缓存,回收资源
    void purge() {
        std::lock_guard<std::mutex> lock(mutex_); // fix: 加锁保护
        nodeMap_.clear();
        freqListMap_.clear();
        minFreq_ = 1; // fix: 重置为初始值
        totalAccesses_ = 0;
        curAverageFreq_ = 0.0;
    }

private:
    // fix: 抽离频率更新逻辑，避免重复代码
    void updateNodeFrequency(const NodePtr& node) {
        if (!node) return;

        // 更新节点的频率
        size_t oldFreq = node->getFreq();
        size_t newFreq = oldFreq + 1;
        node->incrementFreq();

        // 从旧频率列表中移除节点
        FreqListPtr oldFreqList = freqListMap_[oldFreq];
        oldFreqList->removeNode(node);
        if (oldFreqList->isEmpty()) {
            freqListMap_.erase(oldFreq);
            if (minFreq_ == oldFreq) {
                minFreq_ = newFreq;
            }
        }

        // 添加节点到新频率列表
        if (freqListMap_.count(newFreq) == 0) {
            freqListMap_[newFreq] = std::make_shared<FreqList<Key, Value>>(newFreq);
        }
        freqListMap_[newFreq]->insertNodeAtTail(node);

        // 更新访问统计
        totalAccesses_++;
        curAverageFreq_ = (nodeMap_.empty()) ? 0.0 : static_cast<double>(totalAccesses_) / nodeMap_.size();
        
        // 检查是否需要限制频率增长
        if (curAverageFreq_ > maxAverageFreq_) {
            limitFrequencyGrowth();
        }
    }

    // fix: 抽离频率限制逻辑，便于维护
    void limitFrequencyGrowth() {
        if (nodeMap_.empty()) return;

        size_t reduceStep = maxAverageFreq_ / 2;
        size_t newMinFreq = UINT_MAX;

        for (auto& pair : nodeMap_) {
            NodePtr node = pair.second;
            size_t oldFreq = node->getFreq();
            
            // 先从当前频率列表中移除节点
            FreqListMap::iterator oldFreqIt = freqListMap_.find(oldFreq);
            if (oldFreqIt != freqListMap_.end()) {
                oldFreqIt->second->removeNode(node);
                // fix: 移除后检查列表是否为空，为空则删除
                if (oldFreqIt->second->isEmpty()) {
                    freqListMap_.erase(oldFreqIt);
                }
            }

            // 降低节点频率（最小为1）
            size_t newFreq = (oldFreq > reduceStep) ? (oldFreq - reduceStep) : 1;
            node->resetFreq(newFreq);

            // 添加节点到新的频率列表
            if (freqListMap_.count(newFreq) == 0) {
                freqListMap_[newFreq] = std::make_shared<FreqList<Key, Value>>(newFreq);
            }
            freqListMap_[newFreq]->insertNodeAtTail(node);

            // 更新最小频率
            newMinFreq = std::min(newMinFreq, newFreq);
        }

        // fix: 全局更新minFreq_
        minFreq_ = newMinFreq;
        // 重新计算平均频率
        totalAccesses_ = 0;
        for (const auto& pair : nodeMap_) {
            totalAccesses_ += pair.second->getFreq();
        }
        curAverageFreq_ = (nodeMap_.empty()) ? 0.0 : static_cast<double>(totalAccesses_) / nodeMap_.size();
    }

private:
    size_t capacity_;
    size_t minFreq_; // 当前最小频率, 用于快速定位需要淘汰的节点
    size_t maxAverageFreq_; // 最大平均频率, 用于限制频率增长
    size_t totalAccesses_; // 总访问次数, 用于计算平均频率
    double curAverageFreq_; // 当前平均频率（fix: 改为浮点型）

    std::mutex mutex_; // 互斥锁, 保护多线程访问
    NodeMap nodeMap_; // key 到节点的映射
    FreqListMap freqListMap_; // 频率到频率列表的映射
};

// hash分片LFU缓存类
template <class Key, class Value>
class KHashLfuCaches
{
public:
    KHashLfuCaches(size_t capacity, size_t sliceNum)
        : capacity_(capacity)
        , sliceNum_(sliceNum > 0 ? sliceNum : std::thread::hardware_concurrency())  // 默认分片数为CPU核心数
    {
        size_t sliceCapacity = std::ceil(static_cast<double>(capacity_) / sliceNum_); // 计算每个分片的容量，向上取整
        for (size_t i = 0; i < sliceNum_; ++i) {
            lfuSliceCaches_.emplace_back(std::make_unique<KLfuCache<Key, Value>>(sliceCapacity)); // 初始化每个分片
        }
    }

    void put(const Key& key, const Value& value) {
        size_t sliceIndex = Hash(key) % sliceNum_; // 计算分片索引
        lfuSliceCaches_[sliceIndex]->put(key, value); // 调用对应分片的put方法
    }

    bool get(const Key& key, Value& value) {
        size_t sliceIndex = Hash(key) % sliceNum_;
        return lfuSliceCaches_[sliceIndex]->get(key, value);
    }

    Value get(const Key& key) {
        Value value {};
        size_t sliceIndex = Hash(key) % sliceNum_;
        lfuSliceCaches_[sliceIndex]->get(key, value);   // 这里是调用对应分片的get方法
        return value;
    }

    ~KHashLfuCaches() = default;

    // 清空所有分片缓存
    void purge() {
        for (auto& sliceCache : lfuSliceCaches_) {
            sliceCache->purge();
        }
    }

private:
    // 简单的哈希函数，可以根据需要替换为更复杂的哈希算法
    size_t Hash(const Key& key) {
        return std::hash<Key>()(key);
    }

    size_t capacity_; // 总容量
    size_t sliceNum_; // 分片数量
    std::vector<std::unique_ptr<KLfuCache<Key, Value>>> lfuSliceCaches_; // 分片缓存容器
};

}  // namespace KamaCache