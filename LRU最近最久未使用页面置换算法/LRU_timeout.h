#include <time.h>
#include <unordered_map>

/*含过期时间机制（过期间隔相同）的LRU-begin*/
const int ttl=5;//存活时间（过期时间间隔）

class LRUWithTTL{
private:
    struct Node{
        int key;
        int value;
        time_t expireTime;
        Node *pre,*next;
        Node(int key,int value,time_t expireTime)//expireTime 以秒为单位
        {
            this->key=key;
            this->value=value;
            this->expireTime=expireTime;
            pre=nullptr;
            next=nullptr;
        }
    };
    int n;  // 缓存容量
    std::unordered_map<int,Node *> hash;
    Node *L,*R; // L: 表头哨兵节点，R: 表尾哨兵节点
 
    void remove(Node *node)
    {
        Node *pre=node->pre;
        Node *next=node->next;
        pre->next=next;
        next->pre=pre;
 
        hash.erase(node->key);
    }
    void insert(int key,int value)
    {
        time_t curTime;
        curTime=time(nullptr);  // 获取当前时间,单位为秒. time(nullptr) 返回自1970年1月1日00:00:00 UTC以来的秒数
        Node *newNode=new Node(key,value,curTime+ttl);//每次重新插入均重置超时时间为 curTime + ttl
 
        Node *pre=R->pre;
        Node *next=R;
        pre->next=newNode;
        newNode->next=next;
        next->pre=newNode;
        newNode->pre=pre;
 
        hash[key]=newNode;
    }
public:
    LRUWithTTL(int capacity)
    {
        n=capacity;
        L=new Node(-1,-1,0);    // 哨兵节点不存储有效数据，过期时间设为0,表示永远过期
        R=new Node(-1,-1,0);
        L->next=R;
        R->pre=L;
    }
    int get(int key)
    {
        if(hash.find(key)!=hash.end())
        {
            Node *node=hash[key];
            //在访问时就判断该数据是否过期
            //取系统当前时间
            time_t curTime;
            curTime=time(nullptr);
            //判断是否过期
            if(difftime(node->expireTime,curTime)<=0)//过期返回 -1  // difftime 返回两个时间点的差值，单位为秒 -- 判断是否过期
            {
                remove(node);
                return -1;
            }
            else//未过期，重新设置该数据的过期时间
            {
                remove(node);
                insert(node->key,node->value);
                return node->value;
            }
        }
        else return -1;
    }

    /*
    只有在「所有缓存项的过期时间间隔相同，且 get/put 都会重置过期时间」的前提下，
    表头（最久未使用）元素才一定是最先过期的；---> 可以直接淘汰表头元素。
    如果不满足这个前提，表头元素可能未过期，而其他元素已过期。*/
    void put(int key,int value)
    {
        if(hash.find(key)!=hash.end())
        {
            Node *node=hash[key];
            remove(node);
            insert(key,value);
        }
        else
        {
            if(hash.size()==n)//缓存满
            {
                //取系统当前时间
                time_t curTime;
                curTime=time(nullptr);
                bool isExpire=false;//标记是否有过期节点
                //遍历哈希表试图寻找第一个过期的 Node
                std::unordered_map<int,Node *>::iterator it;
                for(it=hash.begin();it!=hash.end();++it)
                {
                    if(difftime(it->second->expireTime,curTime)<=0)
                    {
                        isExpire=true;
                        break;
                    }
                }
                if(isExpire)//有过期节点，淘汰过期节点
                {
                    remove(it->second);
                    insert(key,value);
                }
                else//没有过期节点，淘汰双向链表的表头 L 的后继节点
                {
                    Node *node=L->next;
                    remove(node);
                    insert(key,value);
                }
            }
            else insert(key,value);
        }
    }
};
/*含过期时间机制（过期间隔相同）的LRU-end*/