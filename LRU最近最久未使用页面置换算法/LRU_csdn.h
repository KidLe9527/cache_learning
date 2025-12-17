#include <unordered_map>

/*LRU的基本实现方法*/

class LRUCache {
private:
    //双向链表节点，采用kv键值对形式
    struct Node{
        int key;
        int value;
        Node *pre,*next;//双向链表的前驱和后继指针
        Node(int k,int v)//构造函数
        {
            key=k;
            value=v;
            pre=nullptr;
            next=nullptr;
        }
    };
 
    int n;//缓存容量
    //哈希表，哈希表的 <int,Node *> 结构是为了方便与双向链表进行交互
    std::unordered_map<int,Node *> hash;
    //双向链表哨兵节点
    Node *L,*R;
 
    //删除双向链表及 hash 中的点
    void remove(Node *node)
    {
        //双向链表中 node 前驱指向后继，后继指向前驱，消灭 node
        Node *pre=node->pre;
        Node *next=node->next;
        pre->next=next;
        next->pre=pre;
 
        //哈希表删除 node
        hash.erase(node->key);
        node = nullptr; // 可选：将指针置为空，防止悬空指针
    }
    //更新双向链表及 hash 中的点
    void insert(int key,int value)
    {
        //双向链表中表尾 R 的前驱对应的是最近使用的节点
        Node *pre=R->pre;
        Node *next=R;
        //构造新节点，插入双向链表（前驱后继两个方向的指针都要修改）
        Node *newNode=new Node(key,value);
 
        //后继方向right ->
        pre->next=newNode;
        newNode->next=next;
        //前驱方向left <-
        next->pre=newNode;
        newNode->pre=pre;
 
        //哈希表更新 node
        hash[key]=newNode;
    }
public:
    //缓存空间初始化函数
    LRUCache(int capacity) {
        n=capacity;//初始化空间大小 n 为 capacity
        //为哨兵节点分配内存空间
        L=new Node(-1,-1);
        R=new Node(-1,-1);
        //完成哨兵节点的链接
        L->next=R;
        R->pre=L;
    }
    
    //访问缓存数据函数
    int get(int key) {
        if(hash.find(key)!=hash.end())//缓存中存在该 key
        {
            Node *node=hash[key];//从 hash 表中以 O(1) 速度取出 Node * 结构数据
            //下面完成的是删除双向链表及 hash 中原有的点，并将该节点加入最近使用的表尾 R 的前驱操作
            remove(node);
            insert(node->key,node->value);
            return node->value;//返回 key 对应的 val
        }
        else return -1;//缓存中不存在该 key
    }
    
    //更新缓存数据函数
    void put(int key, int value) {
        if(hash.find(key)!=hash.end())//缓存中存在该 key
        {
            Node *node=hash[key];//从 hash 表中以 O(1) 速度取出 Node * 结构数据
            //下面完成的是删除双向链表及 hash 中原有的点，并将该节点更新 value 值后加入最近使用的表尾 R 的前驱操作
            remove(node);
            insert(key,value);
        }
        // if(get(key)!=-1)
        //     hash[key]->val=value;
        else//缓存中不存在该 key
        {
            if(hash.size()==n)//缓存已满
            {
                //删除双向链表表头 L 指向的节点
                Node *node=L->next;
                remove(node);
                //新节加入最近使用的表尾 R 的前驱操作
                insert(key,value);
            }
            else insert(key,value);//缓存未满，直接插入
        }
    }
};