#include <unordered_map>
#include <cassert>  // 用于断言检查, assert

// 相比csdn的代码 LFU_csdn.h 这里做了一些改进：
// 1. 频率链表和节点的析构函数中释放内存，避免内存泄漏
// 2. 删除节点时释放内存，防止悬空指针
// 3. clear方法修复内存重复释放问题

/**
 * @brief LFU (Least Frequently Used) 缓存实现
 * 核心特性：
 * 1. 淘汰访问频率最低的元素
 * 2. 频率相同时淘汰最久未使用的元素（即最近最少使用）
 * 3. 所有操作(get/put)均保证O(1)时间复杂度
 */
class LFUCache {
private:
    // 双向链表节点结构 - 存储缓存的键值对及访问频率
    struct Node {
        int key;                // 缓存键
        int value;              // 缓存值
        int freq;               // 访问频率
        Node* prev;             // 前驱节点指针
        Node* next;             // 后继节点指针

        /**         // 这是标准化的 Doxygen 文档注释，是 C/C++ 项目中规范的注释方式。
         * @brief 节点构造函数
         * @param k 键
         * @param v 值
         * @param f 访问频率
         */
        Node(int k, int v, int f) : key(k), value(v), freq(f), prev(nullptr), next(nullptr) {}
    };

    // 频率双向链表结构 - 管理相同访问频率的节点
    struct FreqList {
        int freq;               // 该链表所有节点的访问频率
        Node* head;             // 链表头哨兵节点（虚拟节点）
        Node* tail;             // 链表尾哨兵节点（虚拟节点）

        /**
         * @brief 频率链表构造函数
         * @param f 频率值
         */
        FreqList(int f) : freq(f) {
            // 初始化哨兵节点，简化链表操作
            head = new Node(-1, -1, f);
            tail = new Node(-1, -1, f);
            head->next = tail;
            tail->prev = head;
        }

        /**
         * @brief 检查频率链表是否为空（仅包含哨兵节点）
         * @return 空返回true，否则返回false
         */
        bool isEmpty() const {
            return head->next == tail;
        }

        /**
         * @brief 析构函数 - 释放频率链表所有节点内存
         */
        ~FreqList() {
            Node* curr = head;
            while (curr) {
                Node* next = curr->next;
                delete curr;    // 释放当前节点内存               
                curr = next;
            }
            curr = nullptr; // 可选：将指针置为空，防止悬空指针
        }
    };

    int capacity;              // 缓存最大容量
    int min_freq;              // 当前缓存中的最小访问频率
    std::unordered_map<int, Node*> key_node_map;      // 键到节点的映射（快速查找节点）
    std::unordered_map<int, FreqList*> freq_list_map; // 频率到频率链表的映射

    /**
     * @brief 从频率链表中删除指定节点
     * @param node 要删除的节点
     */
    void removeNode(Node* node) {
        if (!node) return;
        node->prev->next = node->next;
        node->next->prev = node->prev;
        delete node;  // 释放节点内存
        node = nullptr; // 可选：将指针置为空，！仅防止函数内部使用悬空指针，对函数外无影响
    }

    /**
     * @brief 将节点添加到对应频率的链表尾部（最近使用位置）
     * @param node 要添加的节点
     */
    void addNodeToFreqList(Node* node) {
        if (!node) return;  // 安全检查
        
        int freq = node->freq;
        // 如果该频率的链表不存在，则创建
        if (freq_list_map.find(freq) == freq_list_map.end()) {
            freq_list_map[freq] = new FreqList(freq);
        }
        
        FreqList* freq_list = freq_list_map[freq];  // 获取对应频率链表
        // 插入到尾哨兵节点前面（链表尾部）
        Node* tail_prev = freq_list->tail->prev;
        tail_prev->next = node;
        node->prev = tail_prev;
        node->next = freq_list->tail;
        freq_list->tail->prev = node;
    }

    /**
     * @brief 清理指定频率的链表（如果为空则删除）
     * @param freq 要清理的频率值
     */
    void cleanFreqList(int freq) {
        auto it = freq_list_map.find(freq); // 返回指定频率的迭代器
        if (it != freq_list_map.end() && it->second->isEmpty()) {
            delete it->second;
            freq_list_map.erase(it);
        }
        // 如果链表不为空，则不做任何操作
    }

public:
    /**
     * @brief 构造函数
     * @param capacity 缓存容量
     */
    LFUCache(int capacity) : capacity(capacity), min_freq(0) {}

    /**
     * @brief 析构函数 - 释放所有内存
     */
    ~LFUCache() {
        // 释放所有频率链表
        for (auto& pair : freq_list_map) {
            delete pair.second; // FreqList 的析构函数会释放其包含的所有节点Node
        }
        // // 释放所有节点（虽然频率链表析构时已释放，这里做双重保障） --- 避免双重释放，推荐使用智能指针
        // for (auto& pair : key_node_map) {
        //     delete pair.second;
        // }

        // 修改后可以直接复用clear方法
        //clear();
    }

    /**
     * @brief 获取缓存中指定键的值
     * @param key 要查找的键
     * @return 找到返回对应值，未找到返回-1
     */
    int get(int key) {
        // 缓存中不存在该键
        if (key_node_map.find(key) == key_node_map.end()) {
            return -1;
        }

        Node* node = key_node_map[key];
        int old_freq = node->freq;
        
        // 1. 从原频率链表中移除节点
        removeNode(node);
        
        // 2. 如果原频率是最小频率且链表为空，更新最小频率
        if (old_freq == min_freq && freq_list_map[old_freq]->isEmpty()) {
            min_freq++;
            cleanFreqList(old_freq); // 清理空的频率链表
        }
        
        // 3. 更新节点频率并添加到新的频率链表
        node->freq++;
        addNodeToFreqList(node);

        return node->value;
    }

    /**
     * @brief 插入/更新缓存键值对
     * @param key 键
     * @param value 值
     */
    void put(int key, int value) {
        // 容量为0时直接返回
        if (capacity == 0) {
            return;
        }

        // 情况1：键已存在 - 更新值并复用get操作更新频率
        if (get(key) != -1) {
            key_node_map[key]->value = value;
            return;
        }

        // 情况2：键不存在 - 需要插入新节点
        // 2.1 缓存已满，需要淘汰最少使用的节点
        if (key_node_map.size() >= capacity) {
            assert(freq_list_map.find(min_freq) != freq_list_map.end());   // 确保最小频率链表存在
            FreqList* min_freq_list = freq_list_map[min_freq];
            
            // 找到最小频率链表中最久未使用的节点（链表头部）
            Node* del_node = min_freq_list->head->next;
            removeNode(del_node);
            
            // 从映射中删除并释放节点内存
            key_node_map.erase(del_node->key);
            delete del_node;    // 释放节点内存
            del_node = nullptr; // 可选：将指针置为空，防止悬空指针
            
            // 清理空的频率链表
            cleanFreqList(min_freq);
        }

        // 2.2 插入新节点
        Node* new_node = new Node(key, value, 1);
        key_node_map[key] = new_node;
        min_freq = 1; // 新节点频率为1，最小频率重置为1
        addNodeToFreqList(new_node);
    }

    /**
     * @brief 获取当前缓存大小（可选接口）
     * @return 缓存中的元素数量
     */
    int size() const {
        return key_node_map.size();
    }

    /**
     * @brief 清空缓存（修复内存重复释放问题）
     * 核心逻辑：
     * 1. 释放所有FreqList（其析构会自动释放内部所有Node）
     * 2. 清空两个哈希表（避免悬空指针）
     * 3. 重置最小频率
     * 注意：不手动delete key_node_map中的Node，避免重复释放
     */
    void clear() {
        // 第一步：释放所有频率链表（会自动释放内部所有Node）
        for (auto& pair : freq_list_map) {
            delete pair.second; // FreqList析构 → 释放其内部所有Node
        }
        freq_list_map.clear(); // 清空频率链表映射
        
        // 第二步：仅清空节点映射（不delete！Node已被FreqList析构释放）
        key_node_map.clear();
        
        // 第三步：重置状态
        min_freq = 0;
    }
};

/**
 * @brief 使用示例
 * int main() {
 *     LFUCache cache(2);   // 这是一个容量为 2 的 LFU 缓存
 *     cache.put(1, 1);
 *     cache.put(2, 2);
 *     cache.get(1);       // 返回 1
 *     cache.put(3, 3);    // 淘汰 key 2
 *     cache.get(2);       // 返回 -1
 *     cache.get(3);       // 返回 3
 *     cache.put(4, 4);    // 淘汰 key 1
 *     cache.get(1);       // 返回 -1
 *     cache.get(3);       // 返回 3
 *     cache.get(4);       // 返回 4
 *     return 0;
 * }
 */