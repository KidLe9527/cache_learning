#include <iostream>
#include <unordered_map>
#include <list> // 用于维护缓存项的顺序
#include <string>

/*这是ARC的极简演示版，没有实现完整的动态容量调节
代码未实现 p 是因为这是简化版，仅演示核心思想；完整 ARC 中 p 是动态调节 LRU/LFU 目标容量的核心，初始 p 并非 “全部分配给 LRU”，而是通过命中幽灵区动态调整；*/

template <typename KeyType, typename ValueType>
class ARCCache {
private:
    // 缓存总容量
    size_t cache_capacity;

    // ---------------- 核心缓存区域 ----------------   // 四个链表，以及对应的哈希表；还有一个实际缓存的键值对哈希表
    std::list<KeyType> LRU_area;   // LRU区域：存储最近使用的项
    std::list<KeyType> LFU_area;   // LFU区域：存储频繁命中的项

    // ---------------- 幽灵缓存区域 ----------------
    std::list<KeyType> LRU_ghost;  // LRU_ghost：LRU区域淘汰的项（仅存key）
    std::list<KeyType> LFU_ghost;  // LFU_ghost：LFU区域淘汰的项（仅存key）

    // ---------------- 快速索引（避免遍历链表） ----------------
    std::unordered_map<KeyType, ValueType> key_to_value; // 实际缓存的键值对（仅LRU/LFU区域有值）
    std::unordered_map<KeyType, bool> in_LRU_area;       // 标记key是否在LRU区域
    std::unordered_map<KeyType, bool> in_LFU_area;       // 标记key是否在LFU区域
    std::unordered_map<KeyType, bool> in_LRU_ghost;      // 标记key是否在LRU_ghost
    std::unordered_map<KeyType, bool> in_LFU_ghost;      // 标记key是否在LFU_ghost

    // ---------------- 辅助函数：淘汰并移动到幽灵缓存 ----------------
    // 从LRU区域淘汰头部项，移动到LRU_ghost
    void evict_LRU_to_ghost() {
        if (LRU_area.empty()) return;
        // 淘汰LRU区域的头部项，准备移动到LRU_ghost
        KeyType evict_key = LRU_area.front();
        LRU_area.pop_front();   // 从LRU区域移除该key
        in_LRU_area.erase(evict_key);
        key_to_value.erase(evict_key); // 移除实际缓存值

        // LRU_ghost满则淘汰头部
        if (LRU_ghost.size() >= cache_capacity) {
            KeyType ghost_key = LRU_ghost.front();
            LRU_ghost.pop_front();
            in_LRU_ghost.erase(ghost_key);
        }
        // 将淘汰的key加入LRU_ghost尾部
        LRU_ghost.push_back(evict_key);
        in_LRU_ghost[evict_key] = true;
    }

    // 从LFU区域淘汰头部项，移动到LFU_ghost
    void evict_LFU_to_ghost() {
        if (LFU_area.empty()) return;
        KeyType evict_key = LFU_area.front();
        LFU_area.pop_front();
        in_LFU_area.erase(evict_key);
        key_to_value.erase(evict_key); // 移除实际缓存值

        // LFU_ghost满则淘汰头部
        if (LFU_ghost.size() >= cache_capacity) {
            KeyType ghost_key = LFU_ghost.front();
            LFU_ghost.pop_front();
            in_LFU_ghost.erase(ghost_key);
        }
        LFU_ghost.push_back(evict_key);
        in_LFU_ghost[evict_key] = true;
    }

    // 淘汰幽灵缓存的头部项
    void evict_ghost_list(std::list<KeyType>& ghost_list, std::unordered_map<KeyType, bool>& in_ghost) {
        if (ghost_list.empty()) return;
        KeyType evict_key = ghost_list.front();
        ghost_list.pop_front();
        in_ghost.erase(evict_key);
    }

public:
    // 构造函数：初始化缓存容量
    explicit ARCCache(size_t capacity) : cache_capacity(capacity) {}

    // 从缓存中获取值（命中则更新热度）
    bool get(const KeyType& key, ValueType& out_value) {
        // 1. 命中LRU区域：移动到LFU区域尾部（提升为高频项）
        if (in_LRU_area.count(key)) {   // 为什么不用find？因为我们只关心是否存在，不需要具体的迭代器；count返回值为0或1，语义更清晰
            LRU_area.remove(key);   // 从LRU区域移除该key（list的remove操作是O(n)，生产环境可用splice优化）
            in_LRU_area.erase(key); // 为什么要erase？因为我们要更新它的位置到LFU区域，原来的位置已经不存在了
            LFU_area.push_back(key);
            in_LFU_area[key] = true;
            out_value = key_to_value[key];  // 修改的只是位置，值不变
            return true;
        }

        // 2. 命中LFU区域：移动到LFU区域尾部（刷新热度）
        if (in_LFU_area.count(key)) {
            LFU_area.remove(key);
            LFU_area.push_back(key);
            out_value = key_to_value[key];
            return true;
        }

        // 3. 命中LRU_ghost：说明LRU淘汰的项仍有价值，扩容LRU区域
        if (in_LRU_ghost.count(key)) {
            // 若缓存已满，先淘汰LFU区域项（为LRU腾空间）
            if (LRU_area.size() + LFU_area.size() >= cache_capacity) {
                evict_LFU_to_ghost();
            }
            // 从LRU_ghost移除该key
            LRU_ghost.remove(key);
            in_LRU_ghost.erase(key);
            // 将key加入LFU区域（重新缓存）
            LFU_area.push_back(key);
            in_LFU_area[key] = true;
            key_to_value[key] = out_value; // out_value 是该 key 对应的 实际数据值（比如从数据库查询的结果、从文件读取的内容）；这里是重建缓存映射。out_value 需要在调用 get 方法前被赋值（模拟从外部加载数据）
            return true;
        }

        // 4. 命中LFU_ghost：说明LFU淘汰的项仍有价值，扩容LFU区域
        if (in_LFU_ghost.count(key)) {
            // 若缓存已满，先淘汰LRU区域项（为LFU腾空间）
            if (LRU_area.size() + LFU_area.size() >= cache_capacity) {
                evict_LRU_to_ghost();
            }
            // 从LFU_ghost移除该key
            LFU_ghost.remove(key);
            in_LFU_ghost.erase(key);
            // 将key加入LFU区域（重新缓存）
            LFU_area.push_back(key);
            in_LFU_area[key] = true;
            key_to_value[key] = out_value; // 实际场景需重新加载值
            return true;
        }

        // 5. 未命中    // 未命中任何区域， 这个时候需要从外部加载数据， 所以返回false
        return false;
    }

    // 向缓存中插入键值对
    void put(const KeyType& key, const ValueType& value) {
        // 若key已在缓存中：更新值并刷新热度
        if (in_LRU_area.count(key) || in_LFU_area.count(key)) {
            key_to_value[key] = value;  // 更新值
            ValueType dummy {}; // 占位变量，复用get的热度刷新逻辑，dummy只是一个临时变量，不会被实际使用
            get(key, dummy); // 复用get的热度刷新逻辑
            return;
        }

        // 保证缓存总容量不超限
        if (LRU_area.size() + LFU_area.size() >= cache_capacity) {
            // 优先淘汰LRU区域项（符合LRU策略）--> 避免频繁淘汰高频但不常访问的 LFU 项。
            evict_LRU_to_ghost();
        }

        // 保证LRU区域+LRU_ghost不超限
        if (LRU_area.size() + LRU_ghost.size() >= cache_capacity) {
            evict_ghost_list(LRU_ghost, in_LRU_ghost);  // 传参是LRU的幽灵链表和对应的哈希表，目的是淘汰幽灵缓存的头部项
        }

        // 将新key插入LRU区域尾部
        LRU_area.push_back(key);    // 插入数据的key对应的链表尾部
        in_LRU_area[key] = true;    // 数据哈希表标记
        key_to_value[key] = value;  // 实际缓存的键值对
    }

    // 打印缓存状态（调试用）
    void print_status() const {
        auto print_list = [](const std::string& name, const std::list<KeyType>& list) {
            std::cout << name << ": ";
            for (const auto& k : list) std::cout << k << " ";
            std::cout << "\n";
        };

        print_list("LRU区域", LRU_area);
        print_list("LFU区域", LFU_area);
        print_list("LRU_ghost", LRU_ghost);
        print_list("LFU_ghost", LFU_ghost);
        std::cout << "------------------------\n";
    }
};

// 测试代码
int main() {
    ARCCache<std::string, int> cache(3); // 容量为3的ARC缓存

    // 插入数据
    cache.put("A", 1);
    cache.put("B", 2);
    cache.put("C", 3);
    std::cout << "插入A/B/C后:\n";
    cache.print_status();

    // 命中A（从LRU移到LFU）
    int val;
    cache.get("A", val);
    std::cout << "命中A后:\n";
    cache.print_status();

    // 插入D（淘汰LRU头部B到LRU_ghost）
    cache.put("D", 4);
    std::cout << "插入D后:\n";
    cache.print_status();

    // 命中B（从LRU_ghost移到LFU）
    cache.get("B", val);
    std::cout << "命中B后:\n";
    cache.print_status();   // 这个时候LRU_area的目标容量应该是3吧？但是目前只有两个缓存，LFU_area的实际容量是1，但是目标容量是0，当再次插入新数据，LFU_area的唯一数据会被淘汰掉，
                            // 这是因为LFU_area的目标容量是0，导致每次插入新数据时，LFU_area的唯一数据都会被淘汰掉，对吗？
                            /*注释中的疑问：插入新数据不会优先淘汰 LFU 项（简化版代码优先淘汰 LRU），只有命中 LRU_ghost 且总容量超限时，才会淘汰 LFU 项；*/

    return 0;
}