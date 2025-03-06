#pragma once

#include <iostream>
#include <map>
#include <vector>
#include <mutex>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <queue>
#include <string>
#include <atomic>
#include <thread>

#include "timekeeper/time_counter.hpp"

namespace timekeeper {

class ThreadData {
private:
    std::string _logid;
    std::unique_ptr<TimeCounter> _tc;
    std::map<std::string, std::string> _log_fields;
    std::mutex _mtx;

public:
    // 默认构造函数，初始化 TimeCounter
    explicit ThreadData() : _tc(std::make_unique<TimeCounter>()) {}

    // 带 logid 的构造函数，委托给无参构造函数并初始化 logid
    ThreadData(const std::string& logid) : ThreadData() {
        _logid = logid;
    }

    std::string report() {
        std::lock_guard lock(_mtx);

        std::stringstream ss;
        ss << "[logid: " << _logid << "]";
        for (auto& item : _log_fields) {
            ss << " [" << item.first << ": " << item.second << "]";
        }
        ss << " " << _tc->report();
        return ss.str();
    }

    void set_log_id(const std::string& logid) {
        std::lock_guard lock(_mtx);
        _logid = logid;
    }

    std::string get_log_id() {
        std::lock_guard lock(_mtx);
        return _logid;
    }

    std::shared_ptr<TimeRecorder> add_recorder(const std::string& name) {
        // _tc 本身是 bthread safe 的
        std::lock_guard lock(_mtx);
        return _tc->add_recorder(name);
    }

    void add_log_field(const std::string& key, const std::string& value, bool need_overwrite = false) {
        std::lock_guard lock(_mtx);
        if (_log_fields.count(key) && !need_overwrite) {
            return;
        }
        _log_fields[key] = value;
    }  
};

// 模板类，表示一个分层结构的键-数据映射关系
template <typename DataType, typename MutexType=std::mutex>
class HierarchicalMap {
public:
    using DataPtr = std::shared_ptr<DataType>;  // 数据的共享指针类型

    // KeyGuard 使用 shared_ptr 来管理 DataType 数据，并包含自定义的 deleter
    using KeyGuard = std::shared_ptr<DataType>;

    // 添加数据到映射中
    // 参数：key 表示要添加的键，data 表示要添加的数据，baseKey（可选）表示继承的数据键
    void AddData(const std::string& key, DataPtr data, const std::string& baseKey = "") {
        std::lock_guard lock(mutex_);  // 确保线程安全

        // 如果提供了 baseKey，继承 baseKey 的数据
        if (!baseKey.empty()) {
            if (!map_.count(baseKey)) {
                // 有问题，提供了 basekey map 里一定有
                std::cerr << "there is no basekey in map, basekey: " << baseKey
                    << " key: " << key << std::endl;
            } else {
                children_[baseKey].insert(key);  // baseKey 的子节点添加新键
                data = map_[baseKey];  // 共享 baseKey 的数据
            }
        }

        map_[key] = data;  // 添加或更新键-数据映射
        children_.emplace(key, std::unordered_set<std::string>{});  // 初始化子节点集合
    }

    // 查找数据
    // 参数：key 表示要查找的键
    // 返回：如果找到则返回对应的数据指针，否则返回空指针
    DataPtr FindData(const std::string& key) {
        std::lock_guard lock(mutex_);  // 确保线程安全
        auto it = map_.find(key);
        return (it != map_.end()) ? it->second : nullptr;  // 如果找到返回数据指针，否则返回空指针
    }

    // 返回 KeyGuard，用于管理键的生命周期，如果找不到 key 则返回空指针
    // KeyGuard 是一个 shared_ptr，带有自定义 deleter 用于删除键及其子节点
    KeyGuard GetKeyGuard(const std::string& key) {
        std::lock_guard lock(mutex_);  // 确保线程安全
        auto it = map_.find(key);
        if (it == map_.end()) {
            return nullptr;  // 如果找不到键，返回空指针
        }

        DataPtr data = it->second;

        // 创建一个带有自定义 deleter 的 shared_ptr
        // 当引用计数降为零时，调用 RemoveKeyRecursive 递归删除
        return KeyGuard(data.get(), [this, key, data](DataType* ptr) {
            if (!this) {
                return;
            }
            std::lock_guard lock(mutex_);  // 确保删除过程是线程安全的
            RemoveKeyRecursive(key);  // 递归删除键及其子节点
        });
    }

private:
    // 递归删除指定键及其子节点
    // 参数：key 表示要删除的键
    void RemoveKeyRecursive(const std::string& key) {
        std::queue<std::string> keys_to_remove;  // 用于广度优先遍历的队列
        keys_to_remove.push(key);
        std::vector<std::string> removed_keys;
        removed_keys.push_back(key);

        // 循环直到所有子节点都被删除
        while (!keys_to_remove.empty()) {
            auto current = keys_to_remove.front();
            keys_to_remove.pop();

            // 查找当前键的子节点
            if (auto childIt = children_.find(current); childIt != children_.end()) {
                // 将所有子节点添加到待删除队列中
                for (const auto& child : childIt->second) {
                    keys_to_remove.push(child);
                    removed_keys.push_back(child);
                }
                children_.erase(childIt);  // 删除子节点记录
            }

            map_.erase(current);  // 删除当前键的数据
        }

        std::stringstream ss;
        ss << "[begin recursive remove key] parent key is " << removed_keys[0];
        for (size_t i = 1; i < removed_keys.size(); i++) {
            ss << " " << removed_keys[i];
        }
        std::cout << ss.str() << std::endl;
    }

    std::unordered_map<std::string, DataPtr> map_;  // 存储键-数据映射
    std::unordered_map<std::string, std::unordered_set<std::string>> children_;  // 存储键及其子节点的关系
    MutexType mutex_;
};

// 利用 HierarchicalMap 实现线程数据管理，使用 bthreadid 作为key，获取 logid，使用 RAII 的方式实现数据自动删除
// 全局单例
class ThreadDataManager {
public:
    // 获取单例实例
    static ThreadDataManager& Instance() {
        static ThreadDataManager instance;  // 使用局部静态变量实现单例
        return instance;
    }

    // 初始化线程数据，传入 logid，传出一个 RAII 的 KeyGuard，销毁时会递归销毁子 key
    // 要保证 KeyGuard 的生命周期 > 所有子 key
    std::shared_ptr<ThreadData> Init(std::string logid) {
        std::lock_guard lock(_mtx);

        clear_if_exist();
        _logid_ptr = new std::string(logid);

        auto data_ptr = data_map_.FindData(logid);
        if (!data_ptr) {
            // 如果不存在，添加新的数据
            std::cout << "Adding new ThreadData: " << logid << std::endl;
            auto new_data = std::shared_ptr<ThreadData>(new ThreadData(logid), [](ThreadData* ptr) {
                std::cout << "Deleting ThreadData: " << ptr->get_log_id() << std::endl;
                delete ptr;
            });
            data_map_.AddData(logid, new_data);
        } else {
            // 如果已存在，添加一个 dummy key
            std::string dummy_key = GenerateDummyKey(logid);
            std::cout << "Adding dummy key: " << dummy_key
                << ", for logid: " << logid << std::endl;
            clear_if_exist();
            _logid_ptr = new std::string(dummy_key);
            data_map_.AddData(dummy_key, data_ptr, logid);
        }

        return GetCurrentThreadData();


        // // 设置线程局部存储 logid
        // clear_if_exist();
        // bthread_setspecific(bthread_key_, new std::string(logid));
        
        // // 检查 logid 是否在 HierarchicalMap 中
        // auto data_ptr = data_map_.FindData(logid);
        // if (!data_ptr) {
        //     // 如果不存在，添加新的数据
        //     std::cout << "Adding new ThreadData: " << logid << std::endl;
        //     auto new_data = std::shared_ptr<ThreadData>(new ThreadData(logid), [](ThreadData* ptr) {
        //         std::cout << "Deleting ThreadData: " << ptr->get_log_id() << std::endl;
        //         delete ptr;
        //     });
        //     data_map_.AddData(logid, new_data);
        // } else {
        //     // 如果已存在，添加一个 dummy key
        //     std::string dummy_key = GenerateDummyKey(logid);
        //     std::cout << "Adding dummy key: " << dummy_key
        //         << ", for logid: " << logid << std::endl;
        //     clear_if_exist();
        //     bthread_setspecific(bthread_key_, new std::string(dummy_key));
        //     data_map_.AddData(dummy_key, data_ptr, logid);
        // }

        // return GetCurrentThreadData();
    }

    std::shared_ptr<ThreadData> GetKeyGuard() {
        if (_logid_ptr) {
            std::cerr << "ThreadData not initialized";
            return std::make_shared<ThreadData>();
        }
        auto logid = *_logid_ptr;
        return data_map_.GetKeyGuard(logid);

        // auto log_ptr = static_cast<std::string*>(bthread_getspecific(bthread_key_));
        // if (!log_ptr) {
        //     std::cerr << "ThreadData not initialized";
        //     return std::make_shared<ThreadData>();
        // }
        // auto logid = *log_ptr;
        // return data_map_.GetKeyGuard(logid);
    }

    // 获取当前线程的数据
    std::shared_ptr<ThreadData> GetCurrentThreadData() {
        if (!_logid_ptr) {
            std::cerr << "ThreadData not initialized";
            return std::make_shared<ThreadData>();
        }

        std::string logid = *_logid_ptr;

        // 查找 HierarchicalMap 中对应的数据
        auto result = data_map_.FindData(logid);
        if (!result) {
            std::cerr << "ThreadData initialized, but cannot find in map."
                << ", logid: " << logid;
            return std::make_shared<ThreadData>();
        }
        return result;

        // auto logid_ptr = static_cast<std::string*>(bthread_getspecific(bthread_key_));
        // if (!_logid_ptr) {
        //     std::cerr << "ThreadData not initialized";
        //     return std::make_shared<ThreadData>();
        // }

        // std::string logid = *logid_ptr;

        // // 查找 HierarchicalMap 中对应的数据
        // auto result = data_map_.FindData(logid);
        // if (!result) {
        //     std::cerr << "ThreadData initialized, but cannot find in map."
        //         << ", logid: " << logid;
        //     return std::make_shared<ThreadData>();
        // }
        // return result;
    }

private:
    void clear_if_exist() {
        if (!_logid_ptr) {
            return;
        }
        std::cout << "find old logid: " << *_logid_ptr << ", need to clear it" << std::endl; 
        delete _logid_ptr;
        _logid_ptr = nullptr;

        // auto log_ptr = static_cast<std::string*>(bthread_getspecific(bthread_key_));
        // if (!log_ptr) {
        //     return;
        // }
        // std::cout << "find old logid: " << *log_ptr << ", need to clear it" << std::endl; 
        // delete log_ptr;
        // bthread_setspecific(bthread_key_, nullptr);
    }

    // 构造函数私有化以实现单例模式
    ThreadDataManager() {
        if (_logid_ptr) {
            std::cout << "Deleting thread-local data: " << *_logid_ptr << std::endl;
            delete _logid_ptr;
        }

        // // 初始化 bthread_key_
        // bthread_key_create(&bthread_key_, [](void* data) {
        //     // 释放线程局部存储的内存
        //     auto str_ptr = static_cast<std::string*>(data);
        //     if (str_ptr) {
        //         std::cout << "Deleting thread-local data: " << *str_ptr << std::endl;
        //         delete str_ptr;
        //     }
        // });
    }

    // 生成一个独特的 dummy key
    std::string GenerateDummyKey(std::string logid) {
        return "dummy_" + logid + "_" + std::to_string(dummy_counter_.fetch_add(1, std::memory_order_relaxed));
    }

    std::mutex _mtx;
    static inline thread_local std::string* _logid_ptr = nullptr;
    // bthread_key_t bthread_key_;  // bthread 特定数据的键
    HierarchicalMap<ThreadData, std::mutex> data_map_;  // 用于存储线程数据的 HierarchicalMap
    std::atomic<uint64_t> dummy_counter_ = 0;  // 用于生成 dummy key 的原子计数器
};

}