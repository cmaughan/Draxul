#include <draxul/remote_session_client.h>

#include <draxul/agent_client.h>
#include <draxul/topology_client.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <utility>

namespace draxul
{

namespace
{

constexpr size_t kCommandQueueLimit = 128;
constexpr size_t kStatusQueueLimit = 8;
constexpr auto kPollInterval = std::chrono::milliseconds(100);

} // namespace

class RemoteSessionClient::Impl
{
public:
    explicit Impl(RemoteSessionClientOptions options)
        : options_(std::move(options))
    {
    }

    ~Impl()
    {
        stop();
    }

    bool start()
    {
        bool expected = false;
        if (!running_.compare_exchange_strong(expected, true))
            return false;
        stopping_ = false;
        worker_ = std::jthread([this] { worker_main(); });
        return true;
    }

    void stop()
    {
        stopping_ = true;
        wake_.notify_all();
        if (worker_.joinable())
            worker_.join();
        running_ = false;
    }

    bool enqueue(TopologyCommand command)
    {
        std::lock_guard guard(mutex_);
        if (stopping_ || commands_.size() >= kCommandQueueLimit)
            return false;
        commands_.push_back(std::move(command));
        wake_.notify_one();
        return true;
    }

    std::optional<uint64_t> request_status()
    {
        std::lock_guard guard(mutex_);
        if (stopping_ || statuses_.size() >= kStatusQueueLimit)
            return std::nullopt;
        const uint64_t id = next_status_id_++;
        statuses_.push_back(id);
        wake_.notify_one();
        return id;
    }

    std::optional<RemoteSessionPublishedState>
    take_published_state()
    {
        std::lock_guard guard(mutex_);
        if (!published_)
            return std::nullopt;
        auto result = std::move(published_);
        published_.reset();
        return result;
    }

private:
    void publish(std::function<void(RemoteSessionPublishedState&)> update)
    {
        {
            std::lock_guard guard(mutex_);
            if (!published_)
                published_.emplace();
            update(*published_);
        }
        if (options_.wake_consumer)
            options_.wake_consumer();
    }

    void publish_topology(const TopologySnapshot& snapshot)
    {
        publish([&](RemoteSessionPublishedState& state) {
            state.topology = snapshot;
            state.topology_error.reset();
        });
    }

    void publish_agents(const ServerAgentSnapshot& snapshot)
    {
        publish([&](RemoteSessionPublishedState& state) {
            state.agents = snapshot;
            state.agent_error.reset();
        });
    }

    void initialize_clients(
        TopologyClient& topology, AgentClient& agents)
    {
        std::string error;
        if (topology.refresh(error))
            publish_topology(topology.snapshot());
        else
        {
            topology_failed_ = true;
            publish([&](RemoteSessionPublishedState& state) {
                state.topology_error = std::move(error);
            });
        }

        error.clear();
        if (agents.refresh(error))
            publish_agents(agents.snapshot());
        else
        {
            agents_failed_ = true;
            publish([&](RemoteSessionPublishedState& state) {
                state.agent_error = std::move(error);
            });
        }
    }

    void execute_command(
        TopologyClient& topology, TopologyCommand command)
    {
        RemoteTopologyCommandCompletion completion{
            .command = command,
        };
        std::string error;
        for (int attempt = 0; attempt < 2; ++attempt)
        {
            command.expected_revision
                = topology.snapshot().revision;
            TopologyCommandResult result;
            if (topology.execute(command, result, error))
            {
                completion.ok = true;
                completion.snapshot = std::move(result.snapshot);
                break;
            }
            completion.error_code = topology.last_error_code();
            if (completion.error_code != "revision_conflict"
                || !topology.refresh(error))
            {
                break;
            }
            publish_topology(topology.snapshot());
        }
        if (!completion.ok)
            completion.error_message = std::move(error);
        publish([&](RemoteSessionPublishedState& state) {
            if (completion.snapshot)
                state.topology = *completion.snapshot;
            state.commands.push_back(std::move(completion));
        });
    }

    void poll_clients(
        TopologyClient& topology, AgentClient& agents)
    {
        bool changed = false;
        std::string error;
        if (!topology.poll(changed, error))
        {
            if (!topology_failed_)
            {
                publish([&](RemoteSessionPublishedState& state) {
                    state.topology_error = std::move(error);
                });
            }
            topology_failed_ = true;
        }
        else if (changed || topology_failed_)
        {
            topology_failed_ = false;
            publish_topology(topology.snapshot());
        }

        changed = false;
        error.clear();
        if (!agents.poll(changed, error))
        {
            if (!agents_failed_)
            {
                publish([&](RemoteSessionPublishedState& state) {
                    state.agent_error = std::move(error);
                });
            }
            agents_failed_ = true;
        }
        else if (changed || agents_failed_)
        {
            agents_failed_ = false;
            publish_agents(agents.snapshot());
        }
    }

    void worker_main()
    {
        TopologyClient topology({
            .runtime_directory = options_.runtime_directory,
            .client_id = options_.client_id,
            .session_id = options_.session_id,
        });
        AgentClient agents({
            .runtime_directory = options_.runtime_directory,
            .client_id = options_.client_id,
            .session_id = options_.session_id,
        });
        initialize_clients(topology, agents);
        auto next_poll
            = std::chrono::steady_clock::now() + kPollInterval;

        while (!stopping_)
        {
            std::optional<TopologyCommand> command;
            std::optional<uint64_t> status_id;
            {
                std::unique_lock lock(mutex_);
                wake_.wait_until(lock, next_poll, [this] {
                    return stopping_ || !commands_.empty()
                        || !statuses_.empty();
                });
                if (stopping_)
                    break;
                if (!commands_.empty())
                {
                    command = std::move(commands_.front());
                    commands_.pop_front();
                }
                if (!statuses_.empty())
                {
                    status_id = statuses_.front();
                    statuses_.pop_front();
                }
            }

            if (command)
                execute_command(topology, std::move(*command));
            if (status_id)
            {
                RemoteStatusCompletion completion{
                    .request_id = *status_id,
                    .result = ServerClient::status(
                        options_.runtime_directory),
                };
                publish([&](RemoteSessionPublishedState& state) {
                    state.statuses.push_back(
                        std::move(completion));
                });
            }
            const auto now = std::chrono::steady_clock::now();
            if (now >= next_poll)
            {
                poll_clients(topology, agents);
                next_poll = now + kPollInterval;
            }
        }
    }

    RemoteSessionClientOptions options_;
    std::jthread worker_;
    std::atomic<bool> running_ = false;
    std::atomic<bool> stopping_ = false;
    std::mutex mutex_;
    std::condition_variable wake_;
    std::deque<TopologyCommand> commands_;
    std::deque<uint64_t> statuses_;
    uint64_t next_status_id_ = 1;
    std::optional<RemoteSessionPublishedState> published_;
    bool topology_failed_ = false;
    bool agents_failed_ = false;
};

RemoteSessionClient::RemoteSessionClient(
    RemoteSessionClientOptions options)
    : impl_(std::make_unique<Impl>(std::move(options)))
{
}

RemoteSessionClient::~RemoteSessionClient() = default;

bool RemoteSessionClient::start()
{
    return impl_->start();
}

void RemoteSessionClient::stop()
{
    impl_->stop();
}

bool RemoteSessionClient::enqueue(TopologyCommand command)
{
    return impl_->enqueue(std::move(command));
}

std::optional<uint64_t> RemoteSessionClient::request_status()
{
    return impl_->request_status();
}

std::optional<RemoteSessionPublishedState>
RemoteSessionClient::take_published_state()
{
    return impl_->take_published_state();
}

} // namespace draxul
