#include <fstream>
#include "server.h"
DECLARE_string(dump_file);

namespace kvservice {

int KVServiceImpl::stop() {
    {
        std::lock_guard<std::mutex> lk(_mutex);
        if (!_stop) {
            _stop = true;
        }
    }
    _cond.notify_one();
    _write_thread.join();
    if (_skip_list) {
        _skip_list->dump(FLAGS_dump_file);
        delete _skip_list;
        _skip_list = nullptr;
    }
    return 0;
}

int KVServiceImpl::start() {
    int ret = 0;
    _stop = false;
    _write_cnt = 0;
    _skip_list = new skiplist::SkipList<int, std::string>(0x7fffffff);
    _skip_list->load(FLAGS_dump_file);

    // start write thread, no read thread
    _write_thread = std::thread([this](){ this->write_loop(); });
    return ret;
}

void KVServiceImpl::get(::google::protobuf::RpcController* cntl_base,
        const GetRequest* request,
        CommonResponse* response,
        ::google::protobuf::Closure* done) {
    (void)cntl_base;
    baidu::rpc::ClosureGuard done_guard(done);
    std::string value;
    bool result = _skip_list->search(request->key(), value);
    if (result) {
        response->set_messages("success");
        response->set_code(200);
        response->set_value(value);
    } else {
        response->set_messages("not found");
        response->set_code(404);
    }
    response->set_request_id(request->request_id());
}

void KVServiceImpl::put(::google::protobuf::RpcController* cntl_base,
        const PutRequest* request,
        CommonResponse* response,
        ::google::protobuf::Closure* done) {
    (void)cntl_base;
    baidu::rpc::ClosureGuard done_guard(done);
    auto l = [=]() {
        baidu::rpc::ClosureGuard done_guard(done);
        bool result = _skip_list->insert(request->key(), request->value());
        if (result) {
            response->set_code(200);
            response->set_messages("success");
        } else {
            response->set_code(404);
            response->set_messages("put failed");
        }
        response->set_request_id(request->request_id());
    };
    auto closure = create_closure(std::move(l));
    _queue.push(closure);
    {
        std::lock_guard<std::mutex> lk(_mutex);
        _write_cnt++;
    }
    _cond.notify_one();
    done_guard.release();
}

void KVServiceImpl::remove(::google::protobuf::RpcController* cntl_base,
        const RemoveRequest* request,
        CommonResponse* response,
        ::google::protobuf::Closure* done) {
    (void)cntl_base;
    baidu::rpc::ClosureGuard done_guard(done);
    auto l = [=]() {
        baidu::rpc::ClosureGuard done_guard(done);
        std::string value;
        bool result = _skip_list->remove(request->key(), value);
        if (result) {
            response->set_code(200);
            response->set_messages("success");
        } else {
            response->set_code(404);
            response->set_messages("remove failed");
        }
        response->set_request_id(request->request_id());
    };
    auto closure = create_closure(std::move(l));
    _queue.push(closure);
    {
        std::lock_guard<std::mutex> lk(_mutex);
        _write_cnt++;
    }
    _cond.notify_one();
    done_guard.release();
}

void KVServiceImpl::write_loop() {
    while (true) {
        {
            std::unique_lock<std::mutex> lock(_mutex);
            _cond.wait(lock, [&]{return _write_cnt || _stop;});
        }
        // handle request in queue
        ::google::protobuf::Closure* done;
        while (_queue.pop(done)) {
            baidu::rpc::ClosureGuard done_guard(done);
            {
                std::lock_guard<std::mutex> lk(_mutex);
                _write_cnt--;
            }
        }
        {
            std::lock_guard<std::mutex> lk(_mutex);
            if (_stop) {
                break;
            }
        }
    }
}
}
