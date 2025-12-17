#include <iostream>
#include <unordered_map>
#include <list>
#include <string>

template <typename K, typename V>
class ARCCache {
private:
    // 缓存容量
    size_t capacity;
    // T1: 最近使用的项 (LRU 队列)
    std::list<K> T1;
    // T2: 频繁命中的项 (LFU 队列)
    std::list<K> T2;
    // B1: T1 淘汰的幽灵项
    std::list<K> B1;
    // B2: T2 淘汰的幽灵项
    std::list<K> B2;

    // 映射 key 到 value (仅 T1/T2 有值)
    std::unordered_map<K, V> cache_map;
    // 快速查找 key 是否在某个 list 中 (避免遍历)
    std::unordered_map<K, bool> in_T1, in_T2, in_B1, in_B2;
    // T1 的目标容量 (动态调整)
    size_t p;

    // 淘汰 T1 头部项到 B1
    void evict_T1_to_B1() {
        if (T1.empty()) return;
        K key = T1.front();
        T1.pop_front();
        in_T1.erase(key);
        cache_map.erase(key); // 移除实际缓存

        // B1 满则淘汰头部
        if (B1.size() >= capacity) {
            K b1_key = B1.front();
            B1.pop_front();
            in_B1.erase(b1_key);
        }
        B1.push_back(key);
        in_B1[key] = true;
    }

    // 淘汰 T2 头部项到 B2
    void evict_T2_to_B2() {
        if (T2.empty()) return;
        K key = T2.front();
        T2.pop_front();
        in_T2.erase(key);
        cache_map.erase(key); // 移除实际缓存

        // B2 满则淘汰头部
        if (B2.size() >= capacity) {
            K b2_key = B2.front();
            B2.pop_front();
            in_B2.erase(b2_key);
        }
        B2.push_back(key);
        in_B2[key] = true;
    }

    // 淘汰幽灵缓存头部
    void evict_ghost(std::list<K>& ghost, std::unordered_map<K, bool>& in_ghost) {
        if (ghost.empty()) return;
        K key = ghost.front();
        ghost.pop_front();
        in_ghost.erase(key);
    }

public:
    explicit ARCCache(size_t cap) : capacity(cap), p(0) {}

    // 获取缓存项 (命中则更新位置)
    bool get(const K& key, V& value) {
        // 命中 T1: 移到 T2 尾部
        if (in_T1.count(key)) {
            T1.remove(key); // 注意: list.remove 是 O(n)，生产可用 splice 优化
            in_T1.erase(key);
            T2.push_back(key);
            in_T2[key] = true;
            value = cache_map[key];
            return true;
        }

        // 命中 T2: 移到 T2 尾部 (刷新热度)
        if (in_T2.count(key)) {
            T2.remove(key);
            T2.push_back(key);
            value = cache_map[key];
            return true;
        }

        // 命中 B1: 自适应调整 p，替换 T2 项，移到 T2
        if (in_B1.count(key)) {
            // 调整 p (向 LRU 倾斜)
            size_t delta = std::max<size_t>(1, B2.size() * capacity / (B1.size() + B2.size()));
            p = std::min(p + delta, capacity);

            // 若 T1+T2 满，淘汰 T2 头部到 B2
            if (T1.size() + T2.size() >= capacity) {
                if (T1.size() < p) {
                    evict_T2_to_B2();
                } else {
                    evict_T1_to_B1();
                }
            }

            // 移除 B1 中的项
            B1.remove(key);
            in_B1.erase(key);
            // 加入 T2 尾部
            T2.push_back(key);
            in_T2[key] = true;
            cache_map[key] = value; // 这里假设 value 是外部传入的最新值 (实际需重新加载)
            return true;
        }

        // 命中 B2: 自适应调整 p，替换 T1 项，移到 T2
        if (in_B2.count(key)) {
            // 调整 p (向 LFU 倾斜)
            size_t delta = std::max<size_t>(1, B1.size() * capacity / (B1.size() + B2.size()));
            p = std::max(p - delta, 0UL);

            // 若 T1+T2 满，淘汰 T1 头部到 B1
            if (T1.size() + T2.size() >= capacity) {
                if (T1.size() > p) {
                    evict_T1_to_B1();
                } else {
                    evict_T2_to_B2();
                }
            }

            // 移除 B2 中的项
            B2.remove(key);
            in_B2.erase(key);
            // 加入 T2 尾部
            T2.push_back(key);
            in_T2[key] = true;
            cache_map[key] = value; // 重新加载值
            return true;
        }

        // 未命中
        return false;
    }

    // 插入缓存项
    void put(const K& key, const V& value) {
        // 已存在则更新值并刷新位置
        if (in_T1.count(key) || in_T2.count(key)) {
            cache_map[key] = value;
            get(key, const_cast<V&>(value)); // 复用 get 的刷新逻辑
            return;
        }

        // 新项: 先保证缓存容量不超限
        if (T1.size() + T2.size() >= capacity) {
            if (T1.size() < p) {
                evict_T1_to_B1(); // T1 小则淘汰 T1
            } else {
                evict_T2_to_B2(); // T1 大则淘汰 T2
            }
        }

        // 处理幽灵缓存容量
        if (T1.size() + B1.size() >= capacity) {
            if (T1.size() < capacity) {
                evict_ghost(B1, in_B1); // B1 满则淘汰幽灵
            } else {
                evict_T1_to_B1(); // T1 满则淘汰 T1
            }
        }

        // 插入 T1 尾部
        T1.push_back(key);
        in_T1[key] = true;
        cache_map[key] = value;
    }

    // 调试: 打印缓存状态
    void print_status() {
        std::cout << "T1: ";
        for (const auto& k : T1) std::cout << k << " ";
        std::cout << "\nT2: ";
        for (const auto& k : T2) std::cout << k << " ";
        std::cout << "\nB1: ";
        for (const auto& k : B1) std::cout << k << " ";
        std::cout << "\nB2: ";
        for (const auto& k : B2) std::cout << k << " ";
        std::cout << "\np: " << p << "\n\n";
    }
};

// 测试案例
int main() {
    // 创建容量为 3 的 ARC 缓存
    ARCCache<std::string, int> arc_cache(3);

    // 插入数据
    arc_cache.put("A", 1);
    arc_cache.put("B", 2);
    arc_cache.put("C", 3);
    std::cout << "插入 A/B/C 后:\n";
    arc_cache.print_status();

    // 命中 A (T1 -> T2)
    int val;
    arc_cache.get("A", val);
    std::cout << "命中 A 后:\n";
    arc_cache.print_status();

    // 插入 D (淘汰 T1 头部 B 到 B1)
    arc_cache.put("D", 4);
    std::cout << "插入 D 后:\n";
    arc_cache.print_status();

    // 命中 B (B1 命中，调整 p，淘汰 T2 头部)
    arc_cache.get("B", val);
    std::cout << "命中 B 后:\n";
    arc_cache.print_status();

    return 0;
}