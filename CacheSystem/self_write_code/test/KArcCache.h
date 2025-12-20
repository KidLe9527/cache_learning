#pragma once

#include "../KICachePolicy.h"
#include "KArcLruPart.h"
#include "KArcLfuPart.h"
#include <memory>


namespace KamaCache
{

template<typename Key, typename Value>
class KArcCache : public KICachePolicy<Key, Value>
{
public:
    KArcCache(size_t capacity = 10, size_t transformThreshold = 2)
        : capacity_(capacity),
          transformThreshold_(transformThreshold),
          lruPart_(std::make_unique<KArcLruPart<Key, Value>>(capacity)),
          lfuPart_(std::make_unique<KArcLfuPart<Key, Value>>(capacity)) {}

    ~KArcCache() override = default;

    virtual bool get(const Key& key, Value& value) override
    {
        if (lruPart_->get(key, value))
        {
            return true;
        }
        if (lfuPart_->get(key, value))
        {
            return true;
        }
        return false;
    }

    virtual Value get(const Key& key) override
    {
        Value value;
        if (get(key, value))
        {
            return value;
        }
        throw std::runtime_error("Key not found");
    }

    virtual void put(const Key& key, const Value& value) override
    {
        if (lruPart_->contains(key) || lfuPart_->contains(key))
        {
            lruPart_->put(key, value);
            lfuPart_->put(key, value);
            return;
        }

        if (lruPart_->size() < capacity_)
        {
            lruPart_->put(key, value);
        }
        else
        {
            Key evictedKey = lruPart_->evict();
            lfuPart_->put(evictedKey, lruPart_->get(evictedKey));
            lruPart_->put(key, value);
        }
    }

private:
    size_t capacity_;   
    size_t transformThreshold_; // 从LRU转换到LFU的访问阈值
    std::unique_ptr<KArcLruPart<Key, Value>> lruPart_;
    std::unique_ptr<KArcLfuPart<Key, Value>> lfuPart_;
};

}