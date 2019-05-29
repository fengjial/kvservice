#ifndef KV_SERVER_HAZARD_H
#define KV_SERVER_HAZARD_H
#include <atomic>

namespace hp {
template <typename> struct HazardPointerList;

template <typename T>
struct HazardPointer {
    friend struct HazardPointerList<T>;
    
    HazardPointer() 
        : next(nullptr)
        , is_active(ATOMIC_VAR_INIT(true))
        , hazardous_pointer(ATOMIC_VAR_INIT(nullptr))
    { }
    
    void remember(T* ptr) {
        hazardous_pointer.store(ptr);
    }

    void release() {
        hazardous_pointer.store(nullptr, std::memory_order_release);
        is_active.store(false, std::memory_order_release);
    }
private:
    HazardPointer<T> *next;
    std::atomic<bool> is_active;
    std::atomic<T*> hazardous_pointer;
};

template <typename T>
struct HazardPointerList {
    HazardPointerList() : head(ATOMIC_VAR_INIT(nullptr)) {}
    
    ~HazardPointerList() {
        HazardPointer<T> *ptr = head.load(std::memory_order_relaxed);
        HazardPointer<T> *next_ptr = nullptr;

        for(; ptr; ptr = next_ptr) {
            next_ptr = ptr->next;
            delete ptr;
        }
    }

    HazardPointer<T>* acquire() {
        
        HazardPointer<T> *p = head.load(std::memory_order_acquire);
        bool inactive = false;
        for(; p; p = p->next) {
            if(p->is_active.load()) {
                continue;
            }

            if(!p->is_active.compare_exchange_weak(inactive, true)) {
                continue;
            }

            return p;
        }

        p = new HazardPointer<T>();
        HazardPointer<T> *head_hp = nullptr;

        do {
            head_hp = head.load();
            p->next = head_hp;
        } while(!head.compare_exchange_weak(head_hp, p));
        
        return p;
    }

    bool contains(const T * const ptr) {
        HazardPointer<T> *p(head.load());
        //std::vector<T*> hp;
        for(; p; p = p->next) {
            if(!p->is_active.load()) {
                continue;
            }
            
            auto hazardous_pointer = p->hazardous_pointer.load();
            if (hazardous_pointer == ptr) {
                //hp.push_back(hazardous_pointer);
                return true;
            }
        }
        
        /*
        std::sort(hp.begin(), hp.end(), std::less<T*>());
        if (std::binary_search(hp.begin(), hp.end(), ptr)) {
            return true;
        }
        */

        return false;
    }
private:
    std::atomic<HazardPointer<T>*> head;
};
}
#endif
