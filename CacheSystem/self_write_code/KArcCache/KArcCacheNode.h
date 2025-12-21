#pragma once

#include <memory>

namespace KamaCache
{

template<typename Key, typename Value>
class KArcCacheNode
{
private:
    Key key_;
    Value value_;
    size_t accessCount_;
    std::weak_ptr<KArcCacheNode> prev_;
    std::shared_ptr<KArcCacheNode> next_;

public:
    KArcCacheNode(const Key& key, const Value& value)
        : key_(key), value_(value), accessCount_(1), prev_(), next_(nullptr) {}

    const Key& getKey() const { return key_; }
    const Value& getValue() const { return value_; }
    void setValue(const Value& value) { value_ = value; }

    size_t getAccessCount() const { return accessCount_; }
    void incrementAccessCount() { ++accessCount_; }
    void resetAccessCount() { accessCount_ = 1; }

    std::shared_ptr<KArcCacheNode> getNext() const { return next_; }
    void setNext(const std::shared_ptr<KArcCacheNode>& next) { next_ = next; }

    std::shared_ptr<KArcCacheNode> getPrev() const { return prev_.lock(); }
    void setPrev(const std::shared_ptr<KArcCacheNode>& prev) { prev_ = prev; }

    template<typename K, typename V> friend class ArcLruPart;
    template<typename K, typename V> friend class ArcLfuPart;   
};

}   // namespace KamaCache