#ifndef SKIPLIST_H
#define SKIPLIST_H

#include <iostream>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <mutex>
#include <fstream>
#include <string>
using namespace std;

#define STORE_FILE "./store/dumpFile"
#include "../locker/locker.h"

//元素节点
template<typename K,typename V>
class Node
{
public:
    Node(){}
    //自身最高可以处于Level node_level层
    Node(K key,V value,int level):m_key(key),m_value(value),node_level(level)
    {
        forward = new Node<K,V>* [level+1];
        memset(forward,0,sizeof(Node<K, V>*) *(level+1));
    }
    ~Node() {delete [] forward;}

    K get_key() const {return m_key;}
    V get_value() const {return m_value;}
    void set_value(V value) {m_value = value;}
    //下一节点
    Node<K, V> **forward;

    //最大Level层
    int node_level;

private:
    K m_key;
    V m_value;
};

//跳表类模板,skipList<key,value> 若key为自定义类型 需要重载运算符比较
template<typename K,typename V>
class skipList
{
public:
    skipList(int max_level = 8);
    ~skipList();

    bool display_List(string &);
    bool insert_element(K,V);
    bool search_element(K,V&);
    bool delete_element(K);
    void clear();
    bool load_file();
    void dump_file();
    void timed_dump();
    int size();
    int highestLevel();

private:
    Node<K,V>* create_node(K,V,int);
    bool get_key_value_from_string(const string &,string &,string &);
    int get_random_level();
    locker mutex;
    string delimiter ="-";

private:
    //最大处于 Level _max_level层
    int _max_level;

    //当前最大层数以Level 0 开始, _skip_list_level 最大值为 _max_level;
    //当前最高占有层
    int _skip_list_level;

    //每一层的头节点
    Node<K, V> *_header;

    //文件操作指针
    ofstream _file_writer;
    ifstream _file_reader;

    //跳表当前元素个数
    int _element_count;
};

//创建跳表节点
template<typename K, typename V>
Node<K, V>* skipList<K, V>::create_node(const K key, const V value, int level) {
    Node<K, V> *n = new Node<K, V>(key, value, level);
    return n;
}

// 构造函数
template<typename K, typename V>
skipList<K, V>::skipList(int max_level)
{
    _max_level = max_level;
    _skip_list_level = 0;
    _element_count = 0;
    K k;
    V v;
    _header = new Node<K,V>(k,v,_max_level);
}

// 析构函数
template<typename K, typename V>
skipList<K, V>::~skipList()
{
    if (_file_writer.is_open()) {
        _file_writer.close();
    }
    if (_file_reader.is_open()) {
        _file_reader.close();
    }
    clear();
    //删除头节点
    if(_header != nullptr)
    {
        delete _header;
        _header = nullptr;
    }
}

//元素占据的最高层Level
template<typename K, typename V>
int skipList<K, V>::highestLevel()
{
    return _skip_list_level;
}

// 获取随机层数,该数为最高占有层
template<typename K, typename V>
int skipList<K, V>::get_random_level()
{
    int k = 1;
    // rand % n + a = [a,n-1+a];
    //[0,1] 50% 
    while (rand() % 2) {
        k++;
    }
    //不超过最大层数_max_level
    k = (k < _max_level) ? k : _max_level;
    return k;
}

//清空储存的节点
template<typename K, typename V>
void skipList<K, V>::clear()
{
    mutex.lock();
    if(_element_count == 0)
    {
        mutex.unlock();
        return;
    }
    //删除所有创建的节点,头节点不删除
    Node<K,V> *cur = _header->forward[0];
    while(cur)
    {
        Node<K,V> *temp = cur->forward[0];
        delete cur;
        cur = temp;
    }

    //初始化跳表
    memset(_header->forward,0,sizeof(Node<K, V>*) *(_header->node_level+1));
    _skip_list_level = 0;
    _element_count = 0;
    mutex.unlock();    
}


// 插入节点
template<typename K, typename V>
bool skipList<K, V>::insert_element(const K key, const V value)
{
    mutex.lock();
    Node<K,V> *cur = _header;

    Node<K, V> *update[_max_level+1];
    memset(update, 0, sizeof(Node<K, V>*)*(_max_level+1));

    //从跳表最顶层开始
    for(int i = _skip_list_level; i >= 0; i--)
    {
        while(cur->forward[i] != NULL && cur->forward[i]->get_key() < key)
        {
            cur = cur->forward[i];
        }
        update[i] = cur;
    }

    //到达底层,并且此时cur后移一位
    //此时应该把节点插入到cur之前
    cur = cur->forward[0];

    //该点已经存在
    if(cur && cur->get_key() == key)
    {
        cur->set_value(value);
        mutex.unlock();
        return true;
    }

    //若cur == null 则表示应该插入到末尾
    //若cur != null && cur != key 那么应该插入到update[0]和cur之间
    if(cur == nullptr || cur->get_key() != key)
    {
        //获得该节点的最高占有层数
        int random_level = get_random_level();
        
        /*
        例如:
            level _max_level head->  null
            .....
            level 4  head->  null

            level 3  head->            (2,2)->null
            level 2  head->  (1,1)->   (2,2)->null
            level 1  head->  (1,1)->   (2,2)->        (3,3)->null
            level 0  head->  (1,1)->   (2,2)->        (3,3)->null

            此时 _skip_list_level 为 3;
        */

        if(random_level > _skip_list_level)
        {
            for(int i = _skip_list_level + 1; i <= random_level; i++)
            {
                update[i] = _header;
            }
            _skip_list_level = random_level;
        }

        //创建新的节点
        Node<K, V>* inserted_node = create_node(key, value, random_level);

        //插入节点 update[i]之后插入
        for(int i = 0; i <= random_level; i++)
        {
            inserted_node->forward[i] = update[i]->forward[i];
            update[i]->forward[i] = inserted_node;
        }

        //cout<<"key : "<<key<<",已经插入成功."<<endl;
        _element_count++;
        
    }    

    mutex.unlock();
    return true;
}

// 显示跳表
template<typename K, typename V>
bool skipList<K, V>::display_List(string& str)
{
    if(_element_count == 0)
    {
        //cout<<"跳表为空！！！"<<endl;
        return false;
    }
    str+="\n************** Skip List **************\n";

    //cout<<"\n************** Skip List **************\n";

    for(int i = _skip_list_level; i >= 0; i--)
    {
        Node<K, V> *node = _header->forward[i];

        str += "Level " + to_string(i) + ": ";
        //cout<<"Level "<<i<<": ";
        while(node != nullptr)
        {
            str += "(" + node->get_key() + "," + node->get_value() + ")" + " ";
            //cout<<"("<<node->get_key()<<","<<node->get_value()<<")"<<" ";
            node = node->forward[i];
        }
        //cout<<endl;
        str += "\n";
    }
    str += "************** Skip List **************\n";
    //cout<<"************** Skip List **************";
    //cout<<endl;
    return true;
}

// 获取跳表大小
template<typename K, typename V>
int skipList<K, V>::size()
{
    return _element_count;
}

//查找元素
template<typename K, typename V>
bool skipList<K, V>::search_element(K key,V& value)
{
    Node<K,V> *cur = _header;

    for(int i = _skip_list_level; i >= 0; i--)
    {
        while(cur->forward[i] && cur->forward[i]->get_key() < key)
        {
            cur = cur->forward[i];
        }
        if(cur->forward[i] && cur->forward[i]->get_key() == key)
        {
            //cout<<"key : "<<key<<", value : "<<cur->forward[i]->get_value()<<" ,查找成功！！！"<<endl;
            value = cur->forward[i]->get_value();
            return true;
        }
    }
    //cout<<"查找key : "<<key<<" 失败,无该元素！！！"<<endl;
    return false;
}

//删除元素
template<typename K, typename V>
bool skipList<K, V>::delete_element(K key)
{
    mutex.lock();

    Node<K,V> *cur = _header;
    Node<K,V> *update[_max_level+1];
    memset(update, 0, sizeof(Node<K, V>*)*(_max_level+1));
    Node<K,V> *temp;
    for(int i = _skip_list_level; i >= 0; i--)
    {
        while(cur->forward[i] && cur->forward[i]->get_key() < key)
        {
            cur = cur->forward[i];
        }
        update[i] = cur;
    }

    //此时cur 指向要删除的节点
    //update[i]为前序节点
    cur = cur->forward[0];

    if(cur == nullptr || cur->get_key() != key)
    {
        //cout<<"该节点不存在,删除失败！！！"<<endl;
        mutex.unlock();
        return false;
    }

    temp = cur;
    for(int i = 0; i <= _skip_list_level; i++)
    {
        //该层无删除节点则停止
        if(update[i]->forward[i] != cur)
            break;
        update[i]->forward[i] = cur->forward[i];
    }

    //移除多余空层
    while(_skip_list_level > 0 && _header->forward[_skip_list_level] == nullptr)
    {
        _skip_list_level--;
    }
    //cout<<"key : "<<temp->get_key()<<",已经成功删除."<<endl;
    delete temp;
    temp = nullptr;
    _element_count--;

    mutex.unlock();
    return true;
}

//获取一行的key 和 value
template<typename K, typename V>
bool skipList<K, V>::get_key_value_from_string(const string& str, string& key, string& value)
{
    if(str.empty())
        return false;
    int pos;
    if((pos = str.find(delimiter)) != string::npos)
    {
        key = str.substr(0,pos);
        value = str.substr(pos + 1,str.size() - pos - 1);
        return true;
    }
    return false;
}

//转储文件
template<typename K, typename V>
void skipList<K, V>::dump_file()
{
    //cout<<"dump: "<<STORE_FILE<<endl;
    //清除文件内容
    if(_element_count == 0)
    {
        _file_writer.open(STORE_FILE,ios::trunc);
        _file_writer.close();
        return;
    }
    _file_writer.open(STORE_FILE,ios::out | ios::app );
    Node<K, V> *node = this->_header->forward[0];

    while(node)
    {
        _file_writer<<node->get_key()<<"-"<<node->get_value()<<"\n";
        node = node->forward[0];
    }
    _file_writer.flush();
    _file_writer.close();
}

template<typename K, typename V>
void skipList<K, V>::timed_dump()
{
    _file_writer.open(STORE_FILE,ios::out | ios::trunc);
    Node<K, V> *node = this->_header->forward[0];

    while(node)
    {
        _file_writer<<node->get_key()<<"-"<<node->get_value()<<"\n";
        node = node->forward[0];
    }
    _file_writer.flush();
    _file_writer.close();
}

//加载文件
template<typename K, typename V>
bool skipList<K, V>::load_file()
{
    _file_reader.open(STORE_FILE);
    string line;
    string key;
    string value;
    while(getline(_file_reader,line))
    {
        if(!get_key_value_from_string(line,key,value))
        {
            exit(-1);
        }
        insert_element(key,value);
    }
    _file_reader.close();
}

#endif