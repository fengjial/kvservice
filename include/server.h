#ifndef KV_SERVER_SERVER_H
#define KV_SERVER_SERVER_H
#include <boost/lockfree/queue.hpp>
#include <condition_variable>
#include <mutex>
#include <thread>
#include "baidu/rpc/server.h"
#include "baidu/personal-code/fengjialin-kv-server/proto/kvservice.pb.h"
#include "skiplist.h"

namespace kvservice {

template <typename L>
class ClosureWithLamba : public ::google::protobuf::Closure {
public:
    ClosureWithLamba(L&& l) : _l(l) {}
    void Run() override {
        _l();
        delete this;
    }
private:
    L _l;
};

template <typename L>
::google::protobuf::Closure* create_closure(L&& l) {
    return new ClosureWithLamba<L>(std::move(l));
}

class KVServiceImpl : public KVService
{
public:
    KVServiceImpl() : _queue(512) { };
    virtual ~KVServiceImpl() {stop();};
    void get(::google::protobuf::RpcController* cntl_base,
            const GetRequest* request,
            CommonResponse* response,
            ::google::protobuf::Closure* done);
    void put(::google::protobuf::RpcController* cntl_base,
            const PutRequest* request,
            CommonResponse* response,
            ::google::protobuf::Closure* done);
    void remove(::google::protobuf::RpcController* cntl_base,
            const RemoveRequest* request,
            CommonResponse* response,
            ::google::protobuf::Closure* done);
    int stop();
    int start();
private:
    void write_loop();
    skiplist::SkipList<int, std::string>* _skip_list = nullptr;
    // one thread for write
    std::thread _write_thread;
    // use queue
    boost::lockfree::queue<::google::protobuf::Closure*> _queue;
    std::mutex _mutex;
    std::condition_variable _cond;
    // run status
    bool _stop;
    int _write_cnt;
};
}
#endif
