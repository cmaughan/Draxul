#include "server_kernel_impl.h"

#include <algorithm>
#include <iomanip>
#include <random>
#include <sstream>

namespace draxul
{

std::string ServerKernel::Impl::random_epoch()
{
    std::random_device random;
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (int index = 0; index < 16; ++index)
        out << std::setw(2) << (random() & 0xff);
    return out.str();
}

ServerKernel::Impl::ClientAccessResult
ServerKernel::Impl::register_client_hello(
    const ServerHello& hello, bool token_capable,
    std::string& connection_token)
{
    const auto now = std::chrono::steady_clock::now();
    prune_inactive_clients(now);
    std::lock_guard guard(mutex);
    const auto found = clients.find(hello.client_id);
    if (found == clients.end()
        && clients.size() >= kServerMaxConnectedClients)
    {
        return ClientAccessResult::LimitReached;
    }
    if (found == clients.end())
    {
        if (token_capable && hello.registration_nonce.empty())
            return ClientAccessResult::InvalidToken;
        ClientRegistration registration{
            .last_activity = now,
            .registration_nonce = hello.registration_nonce,
            .token_required = token_capable,
        };
        if (token_capable)
            registration.connection_token = random_epoch();
        connection_token = registration.connection_token;
        clients.emplace(hello.client_id, std::move(registration));
        return ClientAccessResult::Accepted;
    }

    ClientRegistration& registration = found->second;
    if (registration.token_required
        && hello.connection_token != registration.connection_token)
    {
        if (hello.registration_nonce.empty()
            || hello.registration_nonce
                != registration.registration_nonce)
        {
            return ClientAccessResult::InvalidToken;
        }
    }
    if (!registration.token_required && token_capable)
    {
        if (hello.registration_nonce.empty())
            return ClientAccessResult::InvalidToken;
        registration.token_required = true;
        registration.connection_token = random_epoch();
        registration.registration_nonce
            = hello.registration_nonce;
    }
    else if (registration.token_required
        && hello.connection_token == registration.connection_token
        && !hello.registration_nonce.empty())
    {
        registration.registration_nonce
            = hello.registration_nonce;
    }
    registration.last_activity = now;
    connection_token = registration.connection_token;
    return ClientAccessResult::Accepted;
}

ServerKernel::Impl::ClientAccessResult
ServerKernel::Impl::authenticate_or_touch_client(
    std::string_view client_id,
    std::string_view connection_token)
{
    const auto now = std::chrono::steady_clock::now();
    prune_inactive_clients(now);
    std::lock_guard guard(mutex);
    const auto found = clients.find(std::string(client_id));
    if (found == clients.end())
    {
        if (!connection_token.empty())
            return ClientAccessResult::InvalidToken;
        if (clients.size() >= kServerMaxConnectedClients)
            return ClientAccessResult::LimitReached;
        clients.emplace(std::string(client_id),
            ClientRegistration{
                .last_activity = now,
            });
        return ClientAccessResult::Accepted;
    }

    ClientRegistration& registration = found->second;
    if (registration.token_required
        && connection_token != registration.connection_token)
    {
        return ClientAccessResult::InvalidToken;
    }
    if (!registration.token_required && !connection_token.empty())
        return ClientAccessResult::InvalidToken;
    registration.last_activity = now;
    return ClientAccessResult::Accepted;
}

void ServerKernel::Impl::detach_client_from_services(
    std::string_view client_id)
{
    if (fake_terminal_service)
        fake_terminal_service->disconnect_client(client_id);
    for (auto& [session_id, session] : sessions)
    {
        (void)session_id;
        for (auto& [terminal_id, endpoint] : session->terminals)
        {
            (void)terminal_id;
            endpoint.service->disconnect_client(client_id);
        }
        if (session->poll_service)
            session->poll_service->disconnect_client(client_id);
    }
}

void ServerKernel::Impl::remember_client_session(
    std::string_view client_id,
    std::string_view session_id)
{
    std::lock_guard guard(mutex);
    if (clients.contains(std::string(client_id)))
    {
        client_sessions[std::string(client_id)]
            .insert(std::string(session_id));
    }
}

size_t ServerKernel::Impl::active_clients_for_session(
    std::string_view session_id)
{
    prune_inactive_clients(
        std::chrono::steady_clock::now());
    std::lock_guard guard(mutex);
    return static_cast<size_t>(
        std::ranges::count_if(
            client_sessions,
            [&](const auto& item) {
                return clients.contains(item.first)
                    && item.second.contains(
                        std::string(session_id));
            }));
}

void ServerKernel::Impl::forget_session_clients(
    std::string_view session_id)
{
    std::lock_guard guard(mutex);
    for (auto it = client_sessions.begin();
        it != client_sessions.end();)
    {
        it->second.erase(std::string(session_id));
        if (it->second.empty())
            it = client_sessions.erase(it);
        else
            ++it;
    }
}

void ServerKernel::Impl::disconnect_client(
    std::string_view client_id)
{
    {
        std::lock_guard guard(mutex);
        clients.erase(std::string(client_id));
        client_sessions.erase(std::string(client_id));
    }
    detach_client_from_services(client_id);
}

void ServerKernel::Impl::prune_inactive_clients(
    std::chrono::steady_clock::time_point now)
{
    std::vector<std::string> expired;
    {
        std::lock_guard guard(mutex);
        for (auto it = clients.begin(); it != clients.end();)
        {
            if (now - it->second.last_activity
                > options.client_activity_timeout)
            {
                expired.push_back(it->first);
                client_sessions.erase(it->first);
                it = clients.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }
    for (const auto& client_id : expired)
        detach_client_from_services(client_id);
}

} // namespace draxul
