// 前向声明
template<typename Key, typename Value> class KLruCache;

template<typename Key, typename Value>
class LruNode   // LRU缓存节点，包含键值对和指向前后节点的指针，双向链表结构
{
private:
    Key key_;
    Value value_;
    size_t accessCount_;  // 访问次数，有啥用？ 记录该节点被访问的次数，辅助实现 LRU-K 算法
    std::weak_ptr<LruNode<Key, Value>> prev_;  // 改为weak_ptr打破循环引用
    std::shared_ptr<LruNode<Key, Value>> next_;

public:
    LruNode(Key key, Value value)   // 为什么不初始化指针成员？因为shared_ptr和weak_ptr的默认构造函数已经将它们初始化为nullptr
        : key_(key)
        , value_(value)
        , accessCount_(1) 
    {}

    // 提供必要的访问器
    Key getKey() const { return key_; }
    Value getValue() const { return value_; }
    void setValue(const Value& value) { value_ = value; }
    size_t getAccessCount() const { return accessCount_; }
    void incrementAccessCount() { ++accessCount_; } // 增加访问次数

    friend class KLruCache<Key, Value>; // 使KLruCache成为友元类，允许其访问私有成员
};


template<typename Key, typename Value>
class KLruCache : public KICachePolicy<Key, Value>  // 继承自缓存策略接口，实现LRU缓存， KLruCache 代表 Key-Value LRU Cache
{
public:
    using LruNodeType = LruNode<Key, Value>;    // 节点类型
    using NodePtr = std::shared_ptr<LruNodeType>;   // 节点指针类型，为什么用shared_ptr？因为节点在链表中可能被多个指针引用
    using NodeMap = std::unordered_map<Key, NodePtr>;   // key -> Node 映射

    KLruCache(int capacity)
        : capacity_(capacity)
    {
        initializeList();   // 初始化双向链表
    }

    ~KLruCache() override = default;    // 默认析构函数， 因为使用了智能指针，不需要手动释放内存。override 关键字确保正确覆盖基类的虚析构函数

    // 添加缓存
    void put(Key key, Value value) override
    {
        if (capacity_ <= 0)
            return;
    
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = nodeMap_.find(key);   // find 方法返回一个迭代器，指向找到的元素或容器末尾
        if (it != nodeMap_.end())
        {
            // 如果在当前容器中,则更新value,并调用get方法，代表该数据刚被访问
            updateExistingNode(it->second, value);  // it->second 是对应的节点指针 NodePtr, value 是新的值
            return ;
        }

        addNewNode(key, value); // 如果不在当前容器中,则添加新节点
    }

    bool get(Key key, Value& value) override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = nodeMap_.find(key);
        if (it != nodeMap_.end())
        {
            moveToMostRecent(it->second);
            value = it->second->getValue();
            return true;
        }
        return false;
    }

    Value get(Key key) override
    {
        Value value{};  // 默认构造Value类型的变量
        // memset(&value, 0, sizeof(value));   // memset 是按字节设置内存的，对于复杂类型（如 string）使用 memset 可能会破坏对象的内部结构
        get(key, value);    // 调用重载的 get 方法，如果key 不存在，value 保持默认值
        return value;
    }

    // 删除指定元素
    void remove(Key key) 
    {   
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = nodeMap_.find(key);
        if (it != nodeMap_.end())
        {
            removeNode(it->second); // 删除双向链表中的节点
            nodeMap_.erase(it); // erase 方法接受一个迭代器，删除对应的元素 // 从哈希表中删除该节点
        }
    }

private:
    void initializeList()
    {
        // 创建首尾虚拟节点
        dummyHead_ = std::make_shared<LruNodeType>(-1, -1); // key 和 value 使用默认构造函数初始化, 这样可以确保虚拟节点不会干扰实际数据，调用的是LRU的默认构造函数
        dummyTail_ = std::make_shared<LruNodeType>(Key(), Value());
        dummyHead_->next_ = dummyTail_;
        dummyTail_->prev_ = dummyHead_;
    }

    void updateExistingNode(NodePtr node, const Value& value) 
    {
        node->setValue(value);  // 更新链表节点的值
        moveToMostRecent(node); // 将节点移动到最近使用位置
    }

    void addNewNode(const Key& key, const Value& value) 
    {
       if (nodeMap_.size() >= capacity_) 
       {
           evictLeastRecent(); // 驱逐最近最少访问
       }

       NodePtr newNode = std::make_shared<LruNodeType>(key, value);
       insertNode(newNode); // 从尾部插入新节点，链表操作
       nodeMap_[key] = newNode; // 哈希表更新
    }

    // 将该节点移动到最新的位置
    void moveToMostRecent(NodePtr node) 
    {
        removeNode(node);
        insertNode(node);
    }

    void removeNode(NodePtr node) 
    {
        if(!node->prev_.expired() && node->next_) // .expired() 用于检查 weak_ptr 是否指向一个已被销毁的对象
        {
            /*weak_ptr 不能直接访问对象，必须通过 lock() 方法获取对应的 shared_ptr（如果前驱节点还存在，lock() 返回非空 shared_ptr；如果已销毁，返回 nullptr）。*/
            auto prev = node->prev_.lock(); // 使用lock()获取shared_ptr（前驱节点）     // auto 是 NodePtr 类型
            prev->next_ = node->next_;
            node->next_->prev_ = prev;
            node->next_ = nullptr; // 清空next_指针，彻底断开节点与链表的连接
        }
    }

    // 从尾部插入结点
    void insertNode(NodePtr node) 
    {
        node->next_ = dummyTail_;
        node->prev_ = dummyTail_->prev_;    // 使用weak_ptr赋值。为什么这里不要lock()？因为我们只是想让node的prev_指向dummyTail_的前驱节点，不需要访问该节点本身
        dummyTail_->prev_.lock()->next_ = node; // 使用lock()获取shared_ptr // dummyTail_->prev_ 是 weak_ptr
        dummyTail_->prev_ = node;
    }

    // 驱逐最近最少访问
    void evictLeastRecent() 
    {
        NodePtr leastRecent = dummyHead_->next_;
        removeNode(leastRecent);
        nodeMap_.erase(leastRecent->getKey());
    }

private:
    int           capacity_; // 缓存容量
    NodeMap       nodeMap_; // key -> Node ， 用于快速查找节点，实现O(1)复杂度的访问，哈希表
    std::mutex    mutex_;
    NodePtr       dummyHead_; // 虚拟头结点
    NodePtr       dummyTail_; // 虚拟尾结点
};