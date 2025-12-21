#pragma once

#include "KArcCacheNode.h"
#include <unordered_map>
#include <mutex>


namespace KamaCache
{

template<typename Key, typename Value>
class KArcCache : public KICachePolicy<Key, Value>
{
public:
    using NodeType = KArcCacheNode<Key, Value>;
    using NodePtr = std::shared_ptr<NodeType>;
    using NodeMap = std::unordered_map<Key, NodePtr>;

    explicit KArcCache(size_t capacity = 10, size_t transformThreshold = 2)
        : capacity_(capacity),
          ghostCapacity_(capacity),
          transformThreshold_(transformThreshold),
        {
            initicializeLists();
        }


    ~KamaCache() override = default;

    bool put(const Key& key, const Value& value) override
    {
        std::lock_guard<std::mutex> lock(mutex_);

        // 先检查主链表中是否存在该节点
        auto it = mainCache_.find(key);
        if(it != mainCache_.end())
        {
            return updateExistingNode(it->second, value);
        }

        // 再检查淘汰链表中是否存在该节点
        auto ghostIt = ghostCache_.find(key);
        if(ghostIt != ghostCache_.end())
        {
            // 如果在淘汰链表中找到，说明需要将其提升到主链表
            ghostCache_.erase(ghostIt);
            return addNewNode(key, value);
        }

        // 新节点，直接添加到主链表
        return addNewNode(key, value);
    }

    bool get(const Key& key, Value& value, bool& shouldTransform) override
    {
        std::lock_guard<std::mutex> lock(mutex_);

        // 先在主链表中查找
        auto it = mainCache_.find(key);
        if(it != mainCache_.end())
        {
            value = it->second->getValue();
            // 增加访问计数
            it->second->incrementAccessCount();
            // 将节点移动到主链表头部
            moveToMainHead(it->second);
            shouldTransform = false;
            return true;
        }

        // 再在淘汰链表中查找
        auto ghostIt = ghostCache_.find(key);
        if(ghostIt != ghostCache_.end())
        {
            value = ghostIt->second->getValue();
            // 增加访问计数
            ghostIt->second->incrementAccessCount();
            shouldTransform = (ghostIt->second->getAccessCount() >= transformThreshold_);
            return true;
        }

        // 未命中
        shouldTransform = false;
        return false;
    }

private:
    void initicializeLists()
    {
        mainHead_ = std::make_shared<NodeType>(Key(), Value());
        mainTail_ = std::make_shared<NodeType>(Key(), Value());
        mainHead_->setNext(mainTail_);
        mainTail_->setPrev(mainHead_);

        ghostHead_ = std::make_shared<NodeType>(Key(), Value());
        ghostTail_ = std::make_shared<NodeType>(Key(), Value());
        ghostHead_->setNext(ghostTail_);
        ghostTail_->setPrev(ghostHead_);
    }

    bool updateExistingNode(const NodePtr& node, const Value& value)
    {
        // 更新节点的值
        node->setValue(value);
        // 增加访问计数
        node->incrementAccessCount();
        // 将节点移动到主链表头部
        moveToMainHead(node);
        return true;
    }

    void moveToMainHead(const NodePtr& node)
    {
        // 从当前位置移除节点
        auto prevNode = node->getPrev();
        auto nextNode = node->getNext();
        if (prevNode) prevNode->setNext(nextNode);
        if (nextNode) nextNode->setPrev(prevNode);

        // 插入到主链表头部
        addToFront(node->getKey(), node->getValue());
    }

    // 将节点移动到主链表头部
    void addToFront(const Key& key, const Value& value)
    {
        auto firstRealNode = mainHead_->getNext();
        mainHead_->setNext(mainCache_[key]);
        mainCache_[key]->setPrev(mainHead_);
        mainCache_[key]->setNext(firstRealNode);
        if (firstRealNode) firstRealNode->setPrev(mainCache_[key]);
    }

    // 添加新节点到主链表
    bool addNewNode(const Key& key, const Value& value)
    {
        if(mainCache_.size() >= capacity_)
        {
            evictNode();
        }

        NodePtr newNode = std::make_shared<NodeType>(key, value);
        // 插入新节点到主链表头部
        addToFront(key, value);
        mainCache_[key] = newNode;
        return true;
    }

    void evictNode()
    {
        // 从主链表尾部移除节点
        auto nodeToEvict = mainTail_->getPrev();
        if (nodeToEvict == mainHead_) return; // 主链表为空

        auto prevNode = nodeToEvict->getPrev();
        mainTail_->setPrev(prevNode);
        if (prevNode) prevNode->setNext(mainTail_);

        mainCache_.erase(nodeToEvict->getKey());

        // 将被淘汰的节点添加到淘汰链表
        addToGhostCache(nodeToEvict);
    }

    void addToGhostCache(const NodePtr& node)
    {
        if(ghostCache_.size() >= ghostCapacity_)
        {
            // 淘汰淘汰链表尾部节点
            auto ghostNodeToEvict = ghostTail_->getPrev();
            if (ghostNodeToEvict != ghostHead_)
            {
                auto prevNode = ghostNodeToEvict->getPrev();
                ghostTail_->setPrev(prevNode);
                if (prevNode) prevNode->setNext(ghostTail_);

                ghostCache_.erase(ghostNodeToEvict->getKey());
            }
        }

        // 重置节点的访问次数
        node->resetAccessCount();

        // 插入到淘汰链表头部
        auto firstGhostNode = ghostHead_->getNext();
        ghostHead_->setNext(node);
        node->setPrev(ghostHead_);
        node->setNext(firstGhostNode);
        if (firstGhostNode) firstGhostNode->setPrev(node);

        ghostCache_[node->getKey()] = node;
    }

private:
    size_t capacity_;
    size_t ghostCapacity_;
    size_t transformThreshold_; // 转换门槛值
    std::mutex mutex_;

    NodeMap mainCache_; // key -> ArcNode
    NodeMap ghostCache_;
    
    // 主链表
    NodePtr mainHead_;
    NodePtr mainTail_;
    // 淘汰链表
    NodePtr ghostHead_;
    NodePtr ghostTail_;
};

}   // namespace KamaCache