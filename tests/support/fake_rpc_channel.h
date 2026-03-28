#pragma once

#include <draxul/nvim.h>

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <vector>

namespace draxul::tests
{

class FakeRpcChannel final : public IRpcChannel
{
public:
    struct Call
    {
        std::string method;
        std::vector<MpackValue> params;
    };

    explicit FakeRpcChannel(bool block_requests = false)
        : block_requests_(block_requests)
    {
    }

    RpcResult request(const std::string& method, const std::vector<MpackValue>& params) override
    {
        std::unique_lock<std::mutex> lock(mutex_);
        requests.push_back({ method, params });
        request_in_flight_ = true;
        cv_.notify_all();

        if (block_requests_)
            cv_.wait(lock, [&]() { return released_; });

        request_in_flight_ = false;
        cv_.notify_all();

        RpcResult result;
        result.transport_ok = true;
        result.result = NvimRpc::make_nil();
        return result;
    }

    void notify(const std::string& method, const std::vector<MpackValue>& params) override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        notifications.push_back({ method, params });
    }

    bool wait_for_request_count(size_t count, std::chrono::milliseconds timeout)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        return cv_.wait_for(lock, timeout, [&]() { return requests.size() >= count; });
    }

    bool wait_for_in_flight(std::chrono::milliseconds timeout)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        return cv_.wait_for(lock, timeout, [&]() { return request_in_flight_; });
    }

    void release()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        released_ = true;
        cv_.notify_all();
    }

    size_t request_count() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return requests.size();
    }

    Call request_at(size_t index) const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return requests[index];
    }

    mutable std::mutex mutex_;
    std::vector<Call> requests;
    std::vector<Call> notifications;

private:
    const bool block_requests_ = false;
    std::condition_variable cv_;
    bool request_in_flight_ = false;
    bool released_ = false;
};

} // namespace draxul::tests
