#include <gflags/gflags.h>

#include <base/logging.h>
#include <base/time.h>
#include <baidu/rpc/channel.h>
#include <baidu/rpc/policy/giano_authenticator.h>
#include "baidu/personal-code/fengjialin-kv-server/proto/kvservice.pb.h"

DEFINE_string(protocol, "baidu_std", "Protocol type. Defined in protocol/baidu/rpc/options.proto");
DEFINE_string(connection_type, "", "Connection type. Available values: single, pooled, short");
DEFINE_string(server, "0.0.0.0:8666", "IP Address of server");
DEFINE_string(load_balancer, "", "The algorithm for load balancing");
DEFINE_int32(timeout_ms, 100, "RPC timeout in milliseconds");
DEFINE_int32(max_retry, 3, "Max retries(not including the first RPC)");
DEFINE_int32(interval_ms, 1000, "Milliseconds between consecutive requests");

int main(int argc, char* argv[]) {
    // Parse gflags. We recommend you to use gflags as well.
    google::ParseCommandLineFlags(&argc, &argv, true);

    // Login to get `CredentialGenerator' (see baas-lib-c/baas.h for more
    // information) and then pass it to `GianoAuthenticator'.
    std::unique_ptr<baidu::rpc::policy::GianoAuthenticator> auth;

    // A Channel represents a communication line to a Server. Notice that
    // Channel is thread-safe and can be shared by all threads in your program.
    baidu::rpc::Channel channel;

    // Initialize the channel, NULL means using default options.
    baidu::rpc::ChannelOptions options;
    options.protocol = FLAGS_protocol;
    options.connection_type = FLAGS_connection_type;
    options.auth = auth.get();
    options.timeout_ms = FLAGS_timeout_ms/*milliseconds*/;
    options.max_retry = FLAGS_max_retry;
    if (channel.Init(FLAGS_server.c_str(), FLAGS_load_balancer.c_str(), &options) != 0) {
        LOG(ERROR) << "Fail to initialize channel";
        return -1;
    }

    kvservice::KVService_Stub stub(&channel);
    {
        baidu::rpc::Controller cntl;
        kvservice::PutRequest request;
        kvservice::CommonResponse response;
        
        request.set_key(1);
        request.set_value("a");
        request.set_request_id("1");

        stub.put(&cntl, &request, &response, NULL);
        if (!cntl.Failed()) {
            LOG(INFO) << "Received response from " << cntl.remote_side()
                      << "\tcode:" << response.code()
                      << "\tmessages:" << response.messages()
                      << "\tlatency=" << cntl.latency_us() << "us";
        } else {
            LOG(WARNING) << cntl.ErrorText();
        }
    }
    {
        baidu::rpc::Controller cntl;
        kvservice::GetRequest request;
        kvservice::CommonResponse response;
        
        request.set_key(1);
        request.set_request_id("2");

        stub.get(&cntl, &request, &response, NULL);
        if (!cntl.Failed()) {
            LOG(INFO) << "Received response from " << cntl.remote_side()
                      << "\tcode:" << response.code()
                      << "\tmessages:" << response.messages()
                      << "\tvalue:" << response.value()
                      << "\tlatency=" << cntl.latency_us() << "us";
        } else {
            LOG(WARNING) << cntl.ErrorText();
        }
    }
    {
        baidu::rpc::Controller cntl;
        kvservice::RemoveRequest request;
        kvservice::CommonResponse response;
        
        request.set_key(1);
        request.set_request_id("3");

        stub.remove(&cntl, &request, &response, NULL);
        if (!cntl.Failed()) {
            LOG(INFO) << "Received response from " << cntl.remote_side()
                      << "\tcode:" << response.code()
                      << "\tmessages:" << response.messages()
                      << "\tlatency=" << cntl.latency_us() << "us";
        } else {
            LOG(WARNING) << cntl.ErrorText();
        }
    }

    LOG(INFO) << "EchoClient is going to quit";
    return 0;
}
