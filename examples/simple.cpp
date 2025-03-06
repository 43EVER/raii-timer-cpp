#include <iostream>
#include <thread>
#include <chrono>
#include <memory>
#include <vector>
#include "timekeeper/timekeeper.hpp"

// 模拟处理请求的函数
void process_request(const std::string& request_id) {
    // 初始化线程数据，获取一个线程数据的守护对象
    auto guard = timekeeper::ThreadDataManager::Instance().Init(request_id);
    // auto guard = timekeeper::ThreadDataManager::Instance().Init(request_id);
    
    // 添加日志字段
    guard->add_log_field("request_type", "standard");
    guard->add_log_field("priority", "high");
    
    // 创建一个时间记录器，用于记录处理时间
    auto main_timer = guard->add_recorder("main_process");
    
    // 模拟主要处理步骤
    {
        std::cout << "开始处理请求: " << request_id << std::endl;
        
        // 子处理步骤 1
        {
            auto step1_timer = guard->add_recorder("step1");
            std::cout << "  - 执行步骤 1..." << std::endl;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        
        // 子处理步骤 2
        {
            auto step2_timer = guard->add_recorder("step2");
            std::cout << "  - 执行步骤 2..." << std::endl;
            std::this_thread::sleep_for(std::chrono::milliseconds(150));
            
            // 嵌套子步骤
            {
                auto substep_timer = guard->add_recorder("step2_subprocess");
                std::cout << "    - 执行子步骤..." << std::endl;
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
        }
        
        // 子处理步骤 3
        {
            auto step3_timer = guard->add_recorder("step3");
            std::cout << "  - 执行步骤 3..." << std::endl;
            std::this_thread::sleep_for(std::chrono::milliseconds(80));
            
            // 添加额外信息
            guard->add_log_field("step3_status", "完成");
        }
    }
    
    // 输出最终报告
    std::cout << "请求处理完成，详细信息: " << std::endl;
    std::cout << guard->report() << std::endl;
}

// 模拟子线程函数，继承父线程的上下文
void sub_task(const std::string& parent_request_id, const std::string& subtask_id) {
    // 初始化子任务的线程数据，基于父请求
    auto subtask_guard = timekeeper::ThreadDataManager::Instance().Init(subtask_id);
    
    std::cout << "开始子任务: " << subtask_id << "（父请求: " << parent_request_id << "）" << std::endl;
    
    // 添加子任务特定字段
    subtask_guard->add_log_field("subtask_type", "async");
    
    // 记录子任务时间
    auto subtask_timer = subtask_guard->add_recorder("subtask_execution");
    
    // 模拟子任务处理
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    std::cout << "子任务完成，详细信息: " << std::endl;
    std::cout << subtask_guard->report() << std::endl;
}

// 演示多个并发请求
void demonstrate_concurrent_requests() {
    std::vector<std::thread> threads;
    
    for (int i = 1; i <= 3; i++) {
        std::string request_id = "request_" + std::to_string(i);
        threads.emplace_back(process_request, request_id);
    }
    
    for (auto& t : threads) {
        t.join();
    }
}

// 演示嵌套上下文
void demonstrate_nested_context() {
    std::string main_request_id = "parent_request";
    
    // 初始化主请求
    auto main_guard = timekeeper::ThreadDataManager::Instance().Init(main_request_id);
    main_guard->add_log_field("main_request", "true");
    
    std::cout << "启动主请求: " << main_request_id << std::endl;
    
    // 启动几个子任务线程
    std::vector<std::thread> subtasks;
    for (int i = 1; i <= 2; i++) {
        std::string subtask_id = "subtask_" + std::to_string(i);
        subtasks.emplace_back(sub_task, main_request_id, subtask_id);
    }
    
    // 等待所有子任务完成
    for (auto& t : subtasks) {
        t.join();
    }
    
    std::cout << "所有子任务完成，主请求详情: " << std::endl;
    std::cout << main_guard->report() << std::endl;
}

int main() {
    std::cout << "====== 演示 TimeKeeper 库的基本功能 ======" << std::endl << std::endl;
    
    std::cout << "== 基本请求处理示例 ==" << std::endl;
    process_request("simple_request");
    std::cout << std::endl;
    
    std::cout << "== 并发请求处理示例 ==" << std::endl;
    demonstrate_concurrent_requests();
    std::cout << std::endl;
    
    std::cout << "== 嵌套上下文示例 ==" << std::endl;
    demonstrate_nested_context();
    
    return 0;
}
