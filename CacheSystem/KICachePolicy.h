/*缓存策略接口*/
#pragma once    // 防止多重包含

namespace KamaCache 
{

template <class Key, class Value>   // 定义模版之后必须要紧跟一个“模板化的实体”（类 / 结构体 / 函数）
class KICachePolicy 
{
public:
    virtual ~KICachePolicy() = default;    // 虚析构函数，确保派生类对象通过基类指针删除时能正确调用析构函数
    virtual bool get(const Key& key, Value& value) = 0; // 纯虚函数，获取缓存项
    virtual Value get(const Key& key) = 0; // 纯虚函数，获取缓存项
    virtual void put(const Key& key, const Value& value) = 0; // 纯虚函数，插入缓存项
};

}