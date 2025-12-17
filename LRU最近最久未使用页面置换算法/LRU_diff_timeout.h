#include <time.h>
#include <unordered_map>

/*含过期时间机制（过期间隔不同，自行指定）的LRU-begin ---> 不定时过期LRU算法*/
class LRUWithDiffTTL{
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
    int n;
    std::unordered_map<int,Node *> hash;
    Node *L,*R;
    
    void remove(Node *node)
    {
        Node *pre=node->pre;
        Node *next=node->next;
        pre->next=next;
        next->pre=pre;
        
        hash.erase(node->key);
    }

    //每次插入新节点需要加上其存活时间 ttl（每个节点 ttl 可不同）
    void insert(int key,int value,int ttl)
    {
        time_t curTime;
        curTime=time(nullptr);
        Node *newNode=new Node(key,value,curTime+ttl);
        
        Node *pre=R->pre;
        Node *next=R;
        pre->next=newNode;
        newNode->next=next;
        next->pre=newNode;
        newNode->pre=pre;
        
        hash[key]=newNode;
    }
public:
    LRUWithDiffTTL(int capacity)
    {
        n=capacity;
        L=new Node(-1,-1,0);
        R=new Node(-1,-1,0);
        L->next=R;
        R->pre=L;
    }
    int get(int key)
    {
        if(hash.find(key)!=hash.end())
        {
            Node *node=hash[key];
            time_t curTime = time(nullptr);
            if(difftime(node->expireTime,curTime)<=0)
            {
                remove(node);
                return -1;
            }
            else
            {
                remove(node);
                //保持该节点过期时间不变    // 定时过期LRU中是重新设置过期时间的；这里是不变，过期时间在第一次插入时就已经设定好了
                int timeLeft=difftime(node->expireTime,curTime);    // 计算剩余存活时间
                insert(node->key,node->value,timeLeft);
                return node->value;
            }
        }
        else return -1;
    }

    //每次更新新节点需要显式加上其存活时间 ttl（每个节点 ttl 可不同）
    void put(int key,int value,int ttl)
    {
        if(hash.find(key)!=hash.end())
        {
            Node *node=hash[key];
            remove(node);
            insert(key,value,ttl);//加 ttl
        }
        else
        {
            if(hash.size()==n)
            {
                time_t curTime;
                curTime=time(nullptr);
                std::unordered_map<int,Node *>::iterator it;
                bool isExpire=false;
                for(it=hash.begin();it!=hash.end();++it)
                {
                    if(difftime(it->second->expireTime,curTime)<=0)
                    {
                        isExpire=true;
                        break;
                    }
                }
                if(isExpire)
                {
                    remove(it->second);
                    insert(key,value,ttl);//加 ttl
                }
                else
                {
                    Node *node=L->next;
                    remove(node);
                    insert(key,value,ttl);//加 ttl
                }
            }
            else insert(key,value,ttl);//加 ttl
        }
    }
};
/*含过期时间机制（过期间隔不同-自行指定）的LRU-end*/
// 这里的改动是新增了 LRUWithDiffTTL 类，实现了每个缓存项可以有不同的过期时间的 LRU 缓存机制。
// 不定时过期不能够直接淘汰表头元素，因为表头元素可能未过期，而其他元素已过期。---》 每个LRU节点的过期时间不同

/*
带过期机制的 LRU 是普通 LRU 的功能增强，核心差异在于多了「过期校验与淘汰」逻辑；
其核心优势是解决了普通 LRU 无法处理 “数据时效性” 的痛点，
确保缓存数据有效、优化空间利用率、贴合动态业务场景，同时仅引入轻微的额外开销，是更贴近实际业务需求的缓存策略。*/