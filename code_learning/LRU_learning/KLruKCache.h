// LRU优化：Lru-k版本。 通过继承的方式进行再优化    // Lru-k 中文意思是 最近最少使用的 k 次
/*LRU-k算法有两个队列一个是缓存队列，一个是数据访问历史队列。
当访问一个数据时，首先将其添加进入访问历史队列并进行累加访问次数，当该数据的访问次数超过k次后，才将数据缓存到缓存队列，从而避免缓存队列被冷数据所污染。
同时访问历史队列中的数据也不是一直保留的，也是需要按照LRU的规则进行淘汰的。*/
template<typename Key, typename Value>
class KLruKCache : public KLruCache<Key, Value>
{
public:
    KLruKCache(int capacity, int historyCapacity, int k)
        : KLruCache<Key, Value>(capacity) // 调用基类构造，初始化主缓存容量，主缓存是 KLruCache<Key, Value> 类型。这里主缓存的变量并未显式定义独立的成员变量名，而是通过继承自基类 KLruCache<Key, Value> 来实现的。也就是调用基类的构造函数初始化主缓存容量
        , historyList_(std::make_unique<KLruCache<Key, size_t>>(historyCapacity))   // 初始化历史记录缓存，value为访问次数。调用的是 KLruCache 的构造函数
        , k_(k) // 初始化k值，k代表访问阈值
    {}

    Value get(Key key) 
    {
        // 步骤1：尝试从主缓存（继承的KLruCache实例）获取数据
        Value value{};  // 初始化默认值：若主缓存未命中，最终可能返回该默认值
        // 调用基类KLruCache的get方法：
        // - 若key在主缓存中，会将对应value写入传入的value引用，并返回true；
        // - 若未命中，value保持默认值，返回false
        bool inMainCache = KLruCache<Key, Value>::get(key, value);

        // 步骤2：更新该key的历史访问计数（无论是否命中主缓存，本次访问都需计数）// 主缓存还是要计数，具体原因见后面注释！💥
        // historyList_是智能指针指向的KLruCache<Key, size_t>实例：
        // - Key：与业务数据一致的键类型，对应要统计访问次数的目标key；
        // - size_t：值类型为无符号整数，专门存储该key的累计访问次数；
        // 调用historyList_的get方法：未找到key时返回size_t默认值0，找到则返回当前计数
        size_t historyCount = historyList_->get(key);
        historyCount++;  // 本次访问使计数+1
        // 将更新后的计数写回historyList_：若key已存在则更新计数，不存在则新增key-计数记录
        historyList_->put(key, historyCount);

        // 步骤3：主缓存命中，直接返回从主缓存获取到的有效值
        if (inMainCache) 
        {
            return value;
        }

        // 步骤4：主缓存未命中，但访问次数达到K阈值 → 触发历史数据晋升主缓存
        if (historyCount >= k_) 
        {
            // 检查historyValueMap_中是否暂存了该key的业务值（未达标前的put操作会暂存）
            auto it = historyValueMap_.find(key);
            if (it != historyValueMap_.end()) 
            {
                // 取出暂存的业务值（不能复用value：value是主缓存未命中后的默认值，无业务意义）
                Value storedValue = it->second;
                
                // 清理历史区：晋升后无需保留历史计数和暂存值
                historyList_->remove(key);  // 用->是因为historyList_是std::unique_ptr智能指针，需解引用访问成员方法
                historyValueMap_.erase(it);
                
                // 将暂存值写入主缓存（复用基类KLruCache的put逻辑，自动维护LRU顺序）
                KLruCache<Key, Value>::put(key, storedValue);
                
                // 返回晋升后的数据值
                return storedValue;
            }
            // 无历史值记录（仅计数但未存值），无法晋升，后续返回默认值 // “计数达 K 但无暂存值”：本质是 “访问行为（get）” 和 “存入行为（put）” 的解耦 —— 只访问不存入，自然只有计数没有值；
        }

        // 步骤5：主缓存未命中 + 访问次数未达K → 返回初始的默认值
        return value;

        /*“主缓存未命中 + 历史有值 + 计数未达 K”，为什么不能返回该值？*/
        /*这是 LRU-K 算法的核心设计原则——未达到 K 次访问的 Key 属于 “低频 / 偶然访问”，不应该返回其值（或仅返回默认值），避免缓存污染，
        简单说：未达 K 次的 Key，即使有值，也不对外返回 —— 这是算法层面的 “筛选策略”，而非技术上无法返回。*/
    }

    
    void put(Key key, Value value) 
    {
        // 步骤1：检查当前key是否已存在于主缓存（继承的KLruCache实例）
        Value existingValue{}; // 临时变量：接收主缓存get方法的返回值（仅用于判断命中状态，无实际业务意义）
        // 调用基类KLruCache的get方法：
        // - 命中：existingValue被赋值为主缓存中key对应的value，返回true；这个值是旧值，但我们这里只关心命中状态
        // - 未命中：existingValue保持默认值，返回false
        bool inMainCache = KLruCache<Key, Value>::get(key, existingValue);
        
        if (inMainCache)    
        {
            // 逻辑1：主缓存已命中 → 直接更新主缓存的value（复用基类put逻辑，自动维护LRU顺序）
            // 主缓存中的key已是高频数据，无需参与历史计数/晋升流程，直接更新值即可。这里不更新历史计数，但是在主缓存队列中，会把该缓存提到最近使用位置
            KLruCache<Key, Value>::put(key, value); 
            return;
        }
        
        // 步骤2：主缓存未命中 → 更新该key的历史访问计数（仅未命中主缓存时才需统计历史访问）
        // historyList_：智能指针管理的KLruCache<Key, size_t>实例（Key=业务键类型，Value=无符号整数类型，专门存储key的访问次数）
        // 优先查historyList_而非historyValueMap_：因为historyList_是访问次数的唯一权威存储，historyValueMap_仅暂存值，无计数能力
        size_t historyCount = historyList_->get(key);   // 未找到key时返回size_t默认值0
        historyCount++;  // 本次put操作视为一次访问，计数+1
        historyList_->put(key, historyCount);  // 更新计数：存在则覆盖，不存在则新增（满了按LRU淘汰旧计数）
        
        // 步骤3：暂存value到历史值映射表 → 供后续get操作中“计数达标后晋升主缓存”使用
        // 无论计数是否达标，先暂存值：避免未达标前丢失业务值
        historyValueMap_[key] = value;
        
        // 步骤4：检查计数是否达到K阈值 → 达标则触发晋升主缓存
        if (historyCount >= k_) 
        {
            // 清理历史区：晋升后无需保留计数和暂存值
            historyList_->remove(key);  // 移除历史计数（智能指针需用->访问成员方法）
            historyValueMap_.erase(key); // 移除暂存的业务值
            
            // 将value写入主缓存（复用基类put逻辑，自动处理主缓存容量满时的LRU淘汰）
            KLruCache<Key, Value>::put(key, value);
        }
    }
    

private:
    int                                     k_; // 进入缓存队列的评判标准, 建议k >=2
    std::unique_ptr<KLruCache<Key, size_t>> historyList_; // historyList_ 对应的 KLruCache<Key, size_t>：Value 是 size_t 类型（无符号整数），专门用来存 “Key 的访问次数”。
    std::unordered_map<Key, Value>          historyValueMap_; // 暂存未达到 K 次访问的 Key 对应的 Value（避免丢失值），这里的value是真正的数据值 -->  暂存值的定位：“晋升备用值”，而非 “可返回值”
};

/*
在 C++ 中，友元的子类不能直接访问父类的友元关系—— 友元关系是 “单方向、非传递、非继承” 的,
子类不会自动继承父类的友元权限，友元也不会主动将权限延伸到子类。*/


/*
主缓存命中仍计数的实际价值：
① 主缓存可能因容量满被淘汰（比如 Key 从主缓存被 LRU 踢出去），此时 historyList_ 中已积累的高计数能让 Key 快速重新晋升主缓存（无需再凑 K 次访问）；
② 避免 “主缓存中的 Key 因不计数，被误判为低频”：比如一个 Key 长期在主缓存，若不计数，一旦被淘汰，需重新从 0 开始计数，违背 “高频 Key 优先保留” 的设计。
因此，
主缓存命中后会删除历史记录，再次命中计数将从0开始，这个计数是为了防止主缓存中的key被淘汰后，下一次访问时还能快速晋升回主缓存。
计数会占用historyList_容量，但因historyList_存储成本低、有 LRU 淘汰，是可接受的代价，且有工程手段优化；
*/