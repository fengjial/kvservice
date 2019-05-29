#ifndef KV_SERVER_SKIP_LIST_H
#define KV_SERVER_SKIP_LIST_H
#include <boost/lockfree/queue.hpp>
#include <atomic>
#include <cassert>
#include <fstream>
#include <base/logging.h>
#include <list>
#include "hazard.h"

namespace skiplist {
//forward declaration
template<typename K, typename V>
class SkipList;

template<typename K, typename V>
struct Node {
    friend class SkipList<K, V>;
    
    Node() {}

    Node(const K& k, const V& v) : key(k), value(v) {
        level = 0;
    }
   
    virtual ~Node() {
        delete []forward;
    }

    K key;
    V value;
    int level;

    void set_next(int level, Node<K, V>* node) {
        assert(level >= 0);
        forward[level].store(node, std::memory_order_release);
    }

    void set_next_relaxed(int level, Node<K, V>* node) {
        assert(level >= 0);
        forward[level].store(node, std::memory_order_relaxed);
    }

    Node<K, V>* next(int level) {
        assert(level >= 0);
        return reinterpret_cast<Node<K, V>*>(forward[level].load(std::memory_order_acquire));
    }

    Node<K, V>* next_relaxed(int level) {
        assert(level >= 0);
        return reinterpret_cast<Node<K, V>*>(forward[level].load(std::memory_order_relaxed));
    }

    friend std::ostream & operator << (std::ostream &out, const Node<K, V> & obj) {
        out << obj.key << obj.value << std::endl;
        return out;
    }

private:
    std::atomic<Node<K, V>*>* forward;
};


class Random {
public:
    explicit Random(uint32_t s) : _seed(s & 0x7fffffffu) {
        // Avoid bad seeds.
        if (_seed == 0 || _seed == 2147483647L) {
            _seed = 1;
        }
    }

    uint32_t next() {
        static const uint32_t M = 2147483647L;   // 2^31-1
        static const uint64_t A = 16807;  // bits 14, 8, 7, 5, 2, 1, 0
        // We are computing
        //       seed_ = (seed_ * A) % M,    where M = 2^31-1
        //
        // seed_ must not be zero or M, or else all subsequent computed values
        // will be zero or M respectively.  For all other values, seed_ will end
        // up cycling through every number in [1,M-1]
        uint64_t product = _seed * A;

        // Compute (product % M) using the fact that ((x << 31) % M) == x.
        _seed = static_cast<uint32_t>((product >> 31) + (product & M));
        // The first reduction may overflow by 1 bit, so we may need to
        // repeat.  mod == M is not possible; using > allows the faster
        // sign-bit-based test.
        if (_seed > M) {
            _seed -= M;
        }
        return _seed;
    }

private:
    uint32_t _seed;
};

template<typename K, typename V>
class SkipList{
public:
    SkipList(K footerKey) : _rnd(0x12345678) {
        create_list(footerKey);
    }
    virtual ~SkipList() {
        free_list();
    }
    
    bool search(const K& key, V& value);
    bool insert(K key, V value);
    bool remove(K key, V& value);
    void dump(std::string path);
    bool load(std::string path);
    
    int size() {
        return _size;
    }

private:
    void create_list(K footerKey);

    void free_list();

    void create_node(int level, Node<K, V>* &node);
    
    void create_node(int level, Node<K, V>* &node, K key, V value);
    
    int get_random_level();
    
    Node<K, V>* find_greater_or_equal(const K& key, Node<K,V>** prev);
    
    void defer_free(Node<K, V>* node);
    
    void haz_gc();

    Node<K, V>* _header;
    Node<K, V>* _footer;
    int _level; //level scope [1, MAX_LEVEL]
    int _size;
    Random _rnd;
    static const int MAX_LEVEL = 16;
    static const int GC_THRESHOLD = 50;
    hp::HazardPointerList<Node<K, V>> _all_haz_points;
    std::vector<Node<K, V>*> _lazy_trash_queue;
};

template<typename K, typename V>
void SkipList<K, V>::create_list(K footerKey) {
    create_node(1, _footer);
    _footer->key = footerKey;
    _footer->set_next(0, nullptr);

    create_node(MAX_LEVEL, _header);
    for (int i = 0; i < MAX_LEVEL; ++i) {
        _header->set_next(i, _footer);
    }
    
    _level = 1;
    _size = 0;
}

template<typename K, typename V>
void SkipList<K, V>::create_node(int level, Node<K, V> *&node) {
    node = new Node<K, V>();
    assert(level > 0);
    node->level = level;
    node->forward = new std::atomic<Node<K, V>*>[level];
    assert(node != NULL);
}

template<typename K, typename V>
void SkipList<K, V>::create_node(int level, Node<K, V> *&node, K key, V value) {
    node = new Node<K, V>(key, value);
    assert(level > 0);
    node->level = level;
    node->forward = new std::atomic<Node<K, V>*>[level];
    assert(node != NULL);
}

template<typename K, typename V>
void SkipList<K, V>::free_list() {
    Node<K, V> *p = _header;
    Node<K, V> *q;
    while (p != NULL) {
        q = p->forward[0];
        delete p;
        p = q;
    }
}

template<typename K, typename V>
bool SkipList<K, V>::search(const K& key, V& value) {
    Node<K, V>* prev[MAX_LEVEL];
    Node<K, V>* result;
    // need to mark point before use, after that also need to check the point is
    // available
    // think about this senario:
    // 1. point been removed by other thread
    // 2. point then been marked
    // 3. then try to use unavailable point
    auto haz_point = _all_haz_points.acquire();
    do {
        result = find_greater_or_equal(key, prev);
        haz_point->remember(result);
    } while (prev[0]->next_relaxed(0) != result);
    
    if (result != nullptr && result->key == key) {
        value = result->value;
        haz_point->release();
        return true;
    }
    haz_point->release();
    return false;
}

template<typename K, typename V>
bool SkipList<K, V>::insert(K key, V value) {
    bool update = false;
    Node<K, V>* prev[MAX_LEVEL];
    Node<K, V>* result = find_greater_or_equal(key, prev);

    if (nullptr != result && result->key == key) {
        update = true;
    }
    
    int node_level = result->level;
    if (!update) {
        node_level = get_random_level();
    }
    
    if (node_level > _level) {
        for (int i = _level; i < node_level; ++i) {
            prev[i] = _header;
        }
        _level = node_level;
    }
    
    Node<K, V>* new_node;
    create_node(node_level, new_node, key, value);
    for (int i = 0; i < node_level; ++i) {
        if (update) {
            new_node->set_next_relaxed(i, result->next_relaxed(i));
        } else {
            new_node->set_next_relaxed(i, prev[i]->next_relaxed(i));
        }
        prev[i]->set_next(i, new_node);
    }

    if (!update) {
        ++_size;
    } else {
        defer_free(result);
    }
    return true;
}

template<typename K, typename V>
bool SkipList<K, V>::remove(K key, V &value) {
    Node<K, V>* prev[MAX_LEVEL];
    Node<K, V>* result = find_greater_or_equal(key, prev);

    if (nullptr != result && result->key != key) {
        return false;
    }

    for (int i = 0; i < _level; ++i) {
        if (prev[i]->next_relaxed(i) != result) {
            continue;
        }
        prev[i]->set_next(i, result->next_relaxed(i));
    }
    value = result->value;
    // defer free point, to make sure all read is finished
    defer_free(result);

    while (_level > 1
            && _header->next_relaxed(_level - 1) == _footer) {
        _level--;
    }

    --_size;
    return true;
}

template<typename K, typename V>
int SkipList<K, V>::get_random_level() {
    static const unsigned int prob_level = 4;
    int level = 1;
    while (level < MAX_LEVEL && ((_rnd.next() % prob_level) == 0)) {
      level++;
    }
    return level;
}

template<typename K, typename V>
Node<K, V>* SkipList<K, V>::find_greater_or_equal(const K& key, Node<K,V>** prev) {
    Node<K, V>* x = _header;
    int index = _level - 1;
    while(true) {
        Node<K, V>* next = x->next(index);
        if ((nullptr != next) && (next->key < key)) {
            x = next;
        } else {
            if (nullptr != prev) prev[index] = x;
            if (0 == index) {
                return next;
            } else {
                index--;
            }
        }
    }
}

template<typename K, typename V>
void SkipList<K, V>::dump(std::string path) {
    std::ofstream out(path);

    Node<K, V>* tmp = _header;
    while (tmp->forward[0] != _footer) {
        tmp = tmp->forward[0];
        if (tmp != NULL) {
            out << *tmp;
        }
    }

    out.close();
}

template<typename K, typename V>
bool SkipList<K, V>::load(std::string path) {
    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line)) {
        K key;
        V value;
        //LOG(INFO) << line;
        std::stringstream linestream(line);
        linestream >> key >> value;
        //LOG(INFO) << "key:" << key << " value:" << value;
        if (insert(key, value) == false) {
            in.close();
            return false;
        }
    }
    in.close();
    return true;
}

template<typename K, typename V>
void SkipList<K, V>::defer_free(Node<K, V>* node) {
    //LOG(INFO) << "push free list, key:" << node->key;
    _lazy_trash_queue.push_back(node);
    haz_gc();
}

template<typename K, typename V>
void SkipList<K, V>::haz_gc() {
    if (_lazy_trash_queue.size() >= GC_THRESHOLD) {
        auto it = _lazy_trash_queue.begin();
        while (it != _lazy_trash_queue.end()){
            if (!_all_haz_points.contains(*it)) {
                //LOG(INFO) << "key:" << (*it)->key;
                //LOG(INFO) << "free list size:" << _lazy_trash_queue.size();
                delete *it;
                *it = nullptr;
                if (&*it != &_lazy_trash_queue.back()) {
                    *it = _lazy_trash_queue.back();
                }
                _lazy_trash_queue.pop_back();
                //LOG(INFO) << "list end" << _lazy_trash_queue.end();
            } else {
                LOG(INFO) << "key:" << (*it)->key << " in use";
                ++it;
            }
        }
    }
}
}
#endif
