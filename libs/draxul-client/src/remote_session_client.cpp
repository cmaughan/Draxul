#include <draxul/remote_session_client.h>

#include <draxul/agent_client.h>
#include <draxul/topology_client.h>

#include <algorithm>
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
        , externally_fed_(options_.externally_fed)
    {
        if (!options_.recovery)
        {
            options_.recovery = std::make_shared<ClientRecoveryState>(
                options_.client_id);
        }
        session_poll_epoch_ = options_.recovery->server_epoch();
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

    void acknowledge_topology(
        std::string_view server_epoch, uint64_t revision)
    {
        std::lock_guard guard(mutex_);
        if (pending_topology_
            && pending_topology_->server_epoch == server_epoch
            && pending_topology_->snapshot.revision == revision)
        {
            pending_topology_.reset();
        }
        if (published_ && published_->topology
            && published_->topology_server_epoch == server_epoch
            && published_->topology->revision == revision)
        {
            published_->topology.reset();
            published_->topology_server_epoch.clear();
        }
    }

    void acknowledge_agents(
        std::string_view server_epoch, uint64_t revision)
    {
        std::lock_guard guard(mutex_);
        if (pending_agents_
            && pending_agents_->server_epoch == server_epoch
            && pending_agents_->snapshot.revision == revision)
        {
            pending_agents_.reset();
        }
        if (published_ && published_->agents
            && published_->agent_server_epoch == server_epoch
            && published_->agents->revision == revision)
        {
            published_->agents.reset();
            published_->agent_server_epoch.clear();
        }
    }

    RemoteSessionPollRevisions session_poll_revisions() const
    {
        std::lock_guard guard(mutex_);
        return {
            .topology = topology_poll_revision_,
            .agents = agent_poll_revision_,
        };
    }

    void accept_session_poll_topology(
        std::string server_epoch, TopologySnapshot snapshot)
    {
        accept_session_poll_epoch(server_epoch);
        {
            std::lock_guard guard(mutex_);
            topology_poll_revision_ = snapshot.revision;
        }
        publish_topology(snapshot);
        options_.recovery->note_connected("session.poll");
    }

    void accept_session_poll_agents(
        std::string server_epoch, ServerAgentSnapshot snapshot)
    {
        accept_session_poll_epoch(server_epoch);
        {
            std::lock_guard guard(mutex_);
            agent_poll_revision_ = snapshot.revision;
        }
        publish_agents(snapshot);
        options_.recovery->note_connected("session.poll");
    }

    void accept_session_poll_epoch(std::string server_epoch)
    {
        {
            std::lock_guard guard(mutex_);
            if (server_epoch.empty()
                || server_epoch == session_poll_epoch_)
            {
                return;
            }
        }
        invalidate_session_poll_cursors(std::move(server_epoch));
    }

    void invalidate_session_poll_cursors(std::string server_epoch)
    {
        if (!server_epoch.empty()
            && server_epoch != options_.recovery->server_epoch())
            options_.recovery->set_server_epoch(server_epoch);
        std::string previous;
        {
            std::lock_guard guard(mutex_);
            previous = std::exchange(
                session_poll_epoch_, server_epoch);
            topology_poll_revision_ = 0;
            agent_poll_revision_ = 0;
            pending_topology_.reset();
            pending_agents_.reset();
            if (published_)
            {
                published_->topology.reset();
                published_->agents.reset();
                published_->topology_server_epoch.clear();
                published_->agent_server_epoch.clear();
                published_->commands.clear();
            }
        }
        publish([&](RemoteSessionPublishedState& state) {
            state.server_epoch_changed = !previous.empty()
                && !server_epoch.empty()
                && previous != server_epoch;
            state.recovery
                = options_.recovery->snapshot("session.poll");
        });
    }

    void accept_session_poll_error(
        std::string channel, std::string error)
    {
        const bool topology = channel == "topology";
        const auto recovery
            = options_.recovery->snapshot("session.poll");
        publish([&, error = std::move(error)](
                    RemoteSessionPublishedState& state) mutable {
            state.recovery = recovery;
            if (topology)
                state.topology_error = std::move(error);
            else
                state.agent_error = std::move(error);
        });
    }

    void enable_legacy_polling()
    {
        externally_fed_ = false;
        wake_.notify_one();
    }

private:
    template <typename Snapshot>
    struct PendingSnapshot
    {
        std::string server_epoch;
        Snapshot snapshot;
    };

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
        const std::string epoch
            = options_.recovery->server_epoch();
        {
            std::lock_guard guard(mutex_);
            pending_topology_ = PendingSnapshot<TopologySnapshot>{
                .server_epoch = epoch,
                .snapshot = snapshot,
            };
            if (!published_)
                published_.emplace();
            published_->topology = snapshot;
            published_->topology_server_epoch = epoch;
            published_->topology_error.reset();
        }
        if (options_.wake_consumer)
            options_.wake_consumer();
    }

    void publish_agents(const ServerAgentSnapshot& snapshot)
    {
        const std::string epoch
            = options_.recovery->server_epoch();
        {
            std::lock_guard guard(mutex_);
            pending_agents_ = PendingSnapshot<ServerAgentSnapshot>{
                .server_epoch = epoch,
                .snapshot = snapshot,
            };
            if (!published_)
                published_.emplace();
            published_->agents = snapshot;
            published_->agent_server_epoch = epoch;
            published_->agent_error.reset();
        }
        if (options_.wake_consumer)
            options_.wake_consumer();
    }

    void republish_pending()
    {
        bool published = false;
        {
            std::lock_guard guard(mutex_);
            if (!pending_topology_ && !pending_agents_)
                return;
            if (!published_)
                published_.emplace();
            if (pending_topology_)
            {
                published_->topology
                    = pending_topology_->snapshot;
                published_->topology_server_epoch
                    = pending_topology_->server_epoch;
            }
            if (pending_agents_)
            {
                published_->agents = pending_agents_->snapshot;
                published_->agent_server_epoch
                    = pending_agents_->server_epoch;
            }
            published = true;
        }
        if (published && options_.wake_consumer)
            options_.wake_consumer();
    }

    std::chrono::milliseconds publish_recovery_failure(
        std::string error, std::string_view channel,
        bool topology)
    {
        const auto delay
            = options_.recovery->note_failure(channel);
        const auto recovery
            = options_.recovery->snapshot(channel);
        publish([&, error = std::move(error)](
                    RemoteSessionPublishedState& state) mutable {
            state.recovery = recovery;
            if (topology)
                state.topology_error = std::move(error);
            else
                state.agent_error = std::move(error);
        });
        return delay;
    }

    bool refresh_epoch(std::string& error)
    {
        const std::string previous
            = options_.recovery->server_epoch();
        if (!options_.recovery->refresh_server_epoch(
                options_.runtime_directory,
                options_.client_id, error))
        {
            return false;
        }
        const bool changed = previous
            != options_.recovery->server_epoch();
        if (changed)
        {
            {
                std::lock_guard guard(mutex_);
                pending_topology_.reset();
                pending_agents_.reset();
                topology_poll_revision_ = 0;
                agent_poll_revision_ = 0;
                if (published_)
                {
                    published_->topology.reset();
                    published_->topology_server_epoch.clear();
                    published_->agents.reset();
                    published_->agent_server_epoch.clear();
                    published_->commands.clear();
                }
            }
            publish([&](RemoteSessionPublishedState& state) {
                state.server_epoch_changed = true;
                state.recovery
                    = options_.recovery->snapshot("topology.poll");
            });
        }
        return true;
    }

    void initialize_clients(
        TopologyClient& topology, AgentClient& agents)
    {
        std::string error;
        if (topology_supported_ && topology.refresh(error))
        {
            topology_failed_ = false;
            publish_topology(topology.snapshot());
            options_.recovery->note_connected("topology.poll");
        }
        else if (!topology_supported_)
        {
            topology_failed_ = false;
        }
        else
        {
            if (topology.last_error_code() == "unknown_method")
                topology_supported_ = false;
            topology_failed_ = true;
            publish([&](RemoteSessionPublishedState& state) {
                state.topology_error = std::move(error);
            });
        }

        error.clear();
        if (agents_supported_ && agents.refresh(error))
        {
            agents_failed_ = false;
            publish_agents(agents.snapshot());
            options_.recovery->note_connected("agent.poll");
        }
        else if (!agents_supported_)
        {
            agents_failed_ = false;
        }
        else
        {
            if (agents.last_error_code() == "unknown_method")
                agents_supported_ = false;
            agents_failed_ = true;
            publish([&](RemoteSessionPublishedState& state) {
                state.agent_error = std::move(error);
            });
        }
        if ((!topology_supported_ || !topology_failed_)
            && (!agents_supported_ || !agents_failed_))
        {
            publish([&](RemoteSessionPublishedState& state) {
                state.recovery
                    = options_.recovery->snapshot("topology.poll");
            });
        }
    }

    bool execute_command(
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
                completion.created_id
                    = std::move(result.created_id);
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
        if (!completion.ok
            && (is_transient_client_error(completion.error_code)
                || is_resynchronizing_client_error(
                    completion.error_code)))
        {
            std::lock_guard guard(mutex_);
            commands_.push_front(std::move(command));
            return false;
        }
        if (!completion.ok)
            completion.error_message = std::move(error);
        else
            options_.recovery->note_connected("topology.command");
        if (completion.snapshot)
        {
            const std::string epoch
                = options_.recovery->server_epoch();
            {
                std::lock_guard guard(mutex_);
                pending_topology_
                    = PendingSnapshot<TopologySnapshot>{
                        .server_epoch = epoch,
                        .snapshot = *completion.snapshot,
                    };
                if (!published_)
                    published_.emplace();
                published_->topology
                    = *completion.snapshot;
                published_->topology_server_epoch = epoch;
                published_->topology_error.reset();
                published_->commands.push_back(
                    std::move(completion));
            }
            if (options_.wake_consumer)
                options_.wake_consumer();
        }
        else
        {
            publish([&](RemoteSessionPublishedState& state) {
                state.commands.push_back(
                    std::move(completion));
            });
        }
        return true;
    }

    bool poll_clients(
        TopologyClient& topology, AgentClient& agents)
    {
        bool changed = false;
        std::string error;
        if (topology_supported_
            && !topology.poll(changed, error))
        {
            if (topology.last_error_code() == "unknown_method")
                topology_supported_ = false;
            if (!topology_failed_)
            {
                publish([&](RemoteSessionPublishedState& state) {
                    state.topology_error = std::move(error);
                });
            }
            topology_failed_ = true;
            if (is_transient_client_error(
                    topology.last_error_code())
                || is_resynchronizing_client_error(
                    topology.last_error_code()))
            {
                last_poll_failure_channel_ = "topology.poll";
                return false;
            }
        }
        else if (topology_supported_
            && (changed || topology_failed_))
        {
            topology_failed_ = false;
            publish_topology(topology.snapshot());
            options_.recovery->note_connected("topology.poll");
        }

        changed = false;
        error.clear();
        if (agents_supported_
            && !agents.poll(changed, error))
        {
            if (agents.last_error_code() == "unknown_method")
                agents_supported_ = false;
            if (!agents_failed_)
            {
                publish([&](RemoteSessionPublishedState& state) {
                    state.agent_error = std::move(error);
                });
            }
            agents_failed_ = true;
            if (is_transient_client_error(
                    agents.last_error_code())
                || is_resynchronizing_client_error(
                    agents.last_error_code()))
            {
                last_poll_failure_channel_ = "agent.poll";
                return false;
            }
        }
        else if (agents_supported_
            && (changed || agents_failed_))
        {
            agents_failed_ = false;
            publish_agents(agents.snapshot());
            options_.recovery->note_connected("agent.poll");
        }
        return true;
    }

    void worker_main()
    {
        TopologyClient topology({
            .runtime_directory = options_.runtime_directory,
            .client_id = options_.client_id,
            .session_id = options_.session_id,
            .recovery = options_.recovery,
        });
        AgentClient agents({
            .runtime_directory = options_.runtime_directory,
            .client_id = options_.client_id,
            .session_id = options_.session_id,
            .recovery = options_.recovery,
        });
        const auto initial_status = ServerClient::status(
            options_.runtime_directory,
            std::chrono::milliseconds(500));
        if (initial_status.ok && initial_status.status)
        {
            std::vector<std::string> warnings
                = initial_status.status->restore_warnings;
            const std::string selected_session
                = options_.session_id.empty()
                ? "default"
                : options_.session_id;
            const auto selected = std::ranges::find(
                initial_status.status->session_statuses,
                selected_session,
                &ServerSessionStatusSnapshot::session_id);
            if (selected
                != initial_status.status->session_statuses.end())
            {
                warnings.insert(warnings.end(),
                    selected->restore_warnings.begin(),
                    selected->restore_warnings.end());
                if (!selected->checkpoint_error.empty())
                    warnings.push_back(selected->checkpoint_error);
                else if (!selected->checkpoint_state.empty()
                    && selected->checkpoint_state != "ok"
                    && selected->checkpoint_state != "pending"
                    && selected->checkpoint_state != "restored"
                    && selected->checkpoint_state != "writing")
                {
                    warnings.push_back(
                        "Session checkpoint state is '"
                        + selected->checkpoint_state + "'.");
                }
            }
            std::ranges::sort(warnings);
            warnings.erase(std::unique(
                               warnings.begin(), warnings.end()),
                warnings.end());
            if (!warnings.empty())
            {
                publish([warnings = std::move(warnings)](
                            RemoteSessionPublishedState& state) mutable {
                    state.persistence_warnings
                        = std::move(warnings);
                });
            }
        }
        bool clients_initialized = !externally_fed_;
        if (clients_initialized)
            initialize_clients(topology, agents);
        auto next_poll
            = externally_fed_
            ? std::chrono::steady_clock::time_point::max()
            : std::chrono::steady_clock::now() + kPollInterval;
        std::optional<std::chrono::steady_clock::time_point>
            command_retry_not_before;

        while (!stopping_)
        {
            if (!externally_fed_ && !clients_initialized)
            {
                initialize_clients(topology, agents);
                clients_initialized = true;
                next_poll = std::chrono::steady_clock::now()
                    + kPollInterval;
            }
            std::optional<TopologyCommand> command;
            std::optional<uint64_t> status_id;
            {
                std::unique_lock lock(mutex_);
                const auto now = std::chrono::steady_clock::now();
                const auto wake_at = command_retry_not_before
                    ? std::min(next_poll, *command_retry_not_before)
                    : next_poll;
                const auto ready = [this, &command_retry_not_before,
                                       &clients_initialized] {
                    const bool command_ready
                        = !commands_.empty()
                        && (!command_retry_not_before
                            || std::chrono::steady_clock::now()
                                >= *command_retry_not_before);
                    return stopping_ || (!externally_fed_
                            && !clients_initialized)
                        || !commands_.empty()
                            && command_ready
                        || !statuses_.empty();
                };
                if (wake_at
                    == std::chrono::steady_clock::time_point::max())
                {
                    wake_.wait(lock, ready);
                }
                else
                {
                    wake_.wait_until(lock, wake_at, ready);
                }
                if (stopping_)
                    break;
                if (!commands_.empty()
                    && (!command_retry_not_before
                        || std::chrono::steady_clock::now()
                            >= *command_retry_not_before))
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

            if (command
                && !execute_command(topology, std::move(*command)))
            {
                std::string probe_error;
                refresh_epoch(probe_error);
                const auto delay = publish_recovery_failure(
                    probe_error.empty()
                        ? "Shared Session command transport is unavailable."
                        : std::move(probe_error),
                    "topology.command",
                    true);
                command_retry_not_before
                    = std::chrono::steady_clock::now() + delay;
                continue;
            }
            if (command)
                command_retry_not_before.reset();
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
                if ((!topology_supported_ && !agents_supported_)
                    || poll_clients(topology, agents))
                {
                    next_poll = now + kPollInterval;
                }
                else
                {
                    std::string probe_error;
                    const bool epoch_ready
                        = refresh_epoch(probe_error);
                    if (epoch_ready)
                    {
                        // A fresh snapshot is authoritative even if its
                        // revision is lower than the previous server's.
                        initialize_clients(topology, agents);
                        const bool recovered
                            = last_poll_failure_channel_
                                    == "topology.poll"
                            ? !topology_failed_
                            : !agents_failed_;
                        if (recovered)
                        {
                            next_poll = now + kPollInterval;
                            continue;
                        }
                    }
                    const auto delay = publish_recovery_failure(
                        probe_error.empty()
                            ? "Shared Session transport is unavailable."
                            : std::move(probe_error),
                        last_poll_failure_channel_,
                        last_poll_failure_channel_
                            == "topology.poll");
                    next_poll = now + delay;
                }
                republish_pending();
            }
        }
    }

    RemoteSessionClientOptions options_;
    std::jthread worker_;
    std::atomic<bool> running_ = false;
    std::atomic<bool> stopping_ = false;
    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::deque<TopologyCommand> commands_;
    std::deque<uint64_t> statuses_;
    uint64_t next_status_id_ = 1;
    std::optional<RemoteSessionPublishedState> published_;
    std::optional<PendingSnapshot<TopologySnapshot>>
        pending_topology_;
    std::optional<PendingSnapshot<ServerAgentSnapshot>>
        pending_agents_;
    bool topology_failed_ = false;
    bool agents_failed_ = false;
    bool topology_supported_ = true;
    bool agents_supported_ = true;
    std::string last_poll_failure_channel_
        = "topology.poll";
    uint64_t topology_poll_revision_ = 0;
    uint64_t agent_poll_revision_ = 0;
    std::string session_poll_epoch_;
    std::atomic<bool> externally_fed_ = false;
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

void RemoteSessionClient::acknowledge_topology(
    std::string_view server_epoch, uint64_t revision)
{
    impl_->acknowledge_topology(server_epoch, revision);
}

void RemoteSessionClient::acknowledge_agents(
    std::string_view server_epoch, uint64_t revision)
{
    impl_->acknowledge_agents(server_epoch, revision);
}

RemoteSessionPollRevisions
RemoteSessionClient::session_poll_revisions() const
{
    return impl_->session_poll_revisions();
}

void RemoteSessionClient::accept_session_poll_topology(
    std::string server_epoch, TopologySnapshot snapshot)
{
    impl_->accept_session_poll_topology(
        std::move(server_epoch), std::move(snapshot));
}

void RemoteSessionClient::accept_session_poll_agents(
    std::string server_epoch, ServerAgentSnapshot snapshot)
{
    impl_->accept_session_poll_agents(
        std::move(server_epoch), std::move(snapshot));
}

void RemoteSessionClient::accept_session_poll_epoch(
    std::string server_epoch)
{
    impl_->accept_session_poll_epoch(std::move(server_epoch));
}

void RemoteSessionClient::invalidate_session_poll_cursors(
    std::string server_epoch)
{
    impl_->invalidate_session_poll_cursors(
        std::move(server_epoch));
}

void RemoteSessionClient::accept_session_poll_error(
    std::string channel, std::string error)
{
    impl_->accept_session_poll_error(
        std::move(channel), std::move(error));
}

void RemoteSessionClient::enable_legacy_polling()
{
    impl_->enable_legacy_polling();
}

} // namespace draxul
