# 简易KV存储

## 使用说明
### Skiplist

#### 初始化

```c++
skiplist::SkipList<int, std::string> sl(0x7fffffff);
```

#### 接口

```c++
bool search(const K& key, V& value);
bool insert(K key, V value);
bool remove(K key, V& value);
void dump(std::string path);
bool load(std::string path);
```

### KVServer

#### 初始化

```c++
baidu::rpc::Server server;
if (kv_service.start() != 0) {
    LOG(FATAL) << "Fail to start kv service";
}
if (server.AddService(&kv_service, baidu::rpc::SERVER_DOESNT_OWN_SERVICE) != 0){
    LOG(FATAL) << "Fail to add kv service";
    return -1;
}
baidu::rpc::ServerOptions options;
if (server.Start(FLAGS_port, &options) != 0) {
    LOG(FATAL) << "Fail to start rpc service";
    return -1;
}
server.RunUntilAskedToQuit();
```

#### 接口

```
service KVService {
    rpc get(GetRequest) returns (CommonResponse);
    rpc put(PutRequest) returns (CommonResponse);
    rpc remove(RemoveRequest) returns (CommonResponse);
}
```

## 设计思路
实现lock free的skiplist, 允许单线程写, 多线程读. 使用hazard point, 保障在读写并
发的场景下，不会因为读取到过期的数据而引起core.

### 优势
* 写是wait free，读是lock free

### 缺陷
* 单线程写是一个性能瓶颈
* 并发读写的场景下，部分删除会延时较大

### TODO
* 使用bloom filter提供查询性能
* 使用后端进程进行GC，优化删除操作

## 性能测试
对5000000条记录进行CRUD操作, 其中读为5线程并发
|ACTION|总耗时(s)|单条耗时(ns)|
|------|---------|------------|
|PUT   |5.1      |1015        |
|UPDATE|11.6     |2326        |
|GET   |10.4     |1309        |
|DELETE|9.3      |1162        |

