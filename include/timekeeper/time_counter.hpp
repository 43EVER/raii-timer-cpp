#pragma once

#include <string>
#include <functional>
#include <algorithm>
#include <memory>
#include <mutex>

namespace timekeeper {

using namespace std::chrono;

class TimeRecorder {
public:
    // disable copy, assignment
    TimeRecorder(const TimeRecorder &) = delete;
    TimeRecorder& operator=(const TimeRecorder &) = delete;

    using CB = std::function<void(const std::string &name, int64_t start_us, int64_t end_us)>;
    explicit TimeRecorder(const std::string &name, CB cb) : _name(name), _cb(cb) {
        _create_at = duration_cast<microseconds>(system_clock::now().time_since_epoch()).count();
        _is_start = false;
        _is_end = false;
        _uploaded = false;
    }

    ~TimeRecorder() {
        std::lock_guard lock(_mtx);
        upload();
    }

    int64_t get_time_from_start() {
        std::lock_guard lock(_mtx);
        int64_t start = _is_start ? _start_at : _create_at;
        return duration_cast<microseconds>(system_clock::now().time_since_epoch()).count() - start;
    }

    void start() {
        std::lock_guard lock(_mtx);
        if (_is_end || _is_start) {
            return;
        }
        _start_at = duration_cast<microseconds>(system_clock::now().time_since_epoch()).count();
        _is_start = true;
    }

    void end() {
        std::lock_guard lock(_mtx);
        if (_is_end) {
            return;
        }
        _end_at = duration_cast<microseconds>(system_clock::now().time_since_epoch()).count();
        _is_end = true;

        upload();
    }

private:
    void upload() {
        if (_uploaded) {
            return;
        }
        int64_t start = _is_start ? _start_at : _create_at;
        int64_t end = _is_end ? _end_at : duration_cast<microseconds>(system_clock::now().time_since_epoch()).count();

        _cb(_name, start, end);
        _uploaded = true;
    }

private:
    std::mutex _mtx;

    std::string _name;
    CB _cb;
    int64_t _create_at;
    int64_t _start_at, _end_at;
    bool _is_start, _is_end;
    bool _uploaded;
};

// bthread safe
class TimeCounter {
public:
    // disable copy, assignment, move
    TimeCounter(const TimeCounter &) = delete;
    TimeRecorder& operator=(const TimeRecorder &) = delete;
    TimeCounter(TimeCounter &&) = delete;

    explicit TimeCounter() {}
    ~TimeCounter() {}
    
    // 同名的记录会合并，上报时，所有记录都会上传
    std::shared_ptr<TimeRecorder> add_recorder(const std::string &name) {
        auto rc = std::make_shared<TimeRecorder>(name, [this](const std::string &name, int64_t start_us, int64_t end_us) {
            std::lock_guard lock(_spans_mtx);
            // 合并时，start 取 min，end 取 max
            if (!_spans.count(name)) {
                _spans[name] = std::make_pair(start_us, end_us);
            }
            _spans[name].first = std::min(_spans[name].first, start_us);
            _spans[name].second = std::max(_spans[name].second, end_us);
        });

        std::lock_guard lock(_trs_mtx);
        _trs.push_back(std::weak_ptr<TimeRecorder>(rc));

        return rc;
    }

    std::string report() {
        // report 时，所有记录都会上传
        std::lock_guard lock(_trs_mtx);
        for (auto&& tr : _trs) {
            auto real_tr = tr.lock();
            if (real_tr) {
                real_tr->end();
            }
        }

        // 生成字符串
        std::vector<std::string> view;
        std::transform(_spans.begin(), _spans.end(), 
            std::back_inserter(view), 
            [](auto&& item) {
                // 或者直接返回
                char buffer[100]; // 确保缓冲区足够大
                snprintf(buffer, sizeof(buffer), "[%s: %.3f(ms)]", 
                        item.first.c_str(), 
                        (item.second.second - item.second.first) / 1000.0);
                return std::string(buffer);
            }
        );

        std::string result;
        if (!view.empty()) {
            result = view[0];
            for (size_t i = 1; i < view.size(); ++i) {
                result += " " + view[i];
            }
        }
        return result;
    }

private:
    std::mutex _spans_mtx;
    std::map<std::string, std::pair<int64_t, int64_t>> _spans;

    std::mutex _trs_mtx;
    std::vector<std::weak_ptr<TimeRecorder>> _trs;
};

}