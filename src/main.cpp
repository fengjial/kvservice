#include <iostream>
#include <cassert>
#include <ctime>
#include <gflags/gflags.h>
#include <thread>
#include <vector>
#include <chrono>
#include <random>
#include "baidu/rpc/server.h"
#include "server.h"

DEFINE_int32(port, 8666, "kv server port");
DEFINE_string(dump_file, "./dump", "kv dump file path");
int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    baidu::rpc::Server server;
    kvservice::KVServiceImpl kv_service;
    if (kv_service.start() != 0) {
        LOG(FATAL) << "Fail to start kv service";
    }
    if (server.AddService(&kv_service, baidu::rpc::SERVER_DOESNT_OWN_SERVICE) != 0) {
        LOG(FATAL) << "Fail to add kv service";
        return -1;
    }
    baidu::rpc::ServerOptions options;
    if (server.Start(FLAGS_port, &options) != 0) {
        LOG(FATAL) << "Fail to start rpc service";
        return -1;
    }
    server.RunUntilAskedToQuit();
    return 0;
}
