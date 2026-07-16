#include <draxul/http/http_client.h>

#include <utility>

namespace draxul::http
{

CancellationToken::CancellationToken(std::shared_ptr<std::atomic<bool>> state)
    : state_(std::move(state))
{
}

bool CancellationToken::is_cancelled() const noexcept
{
    return state_ && state_->load(std::memory_order_acquire);
}

CancellationSource::CancellationSource()
    : state_(std::make_shared<std::atomic<bool>>(false))
{
}

CancellationToken CancellationSource::token() const
{
    return CancellationToken(state_);
}

void CancellationSource::cancel() const noexcept
{
    state_->store(true, std::memory_order_release);
}

std::string encode_query_component(std::string_view value)
{
    constexpr char kHex[] = "0123456789ABCDEF";
    std::string result;
    result.reserve(value.size());
    for (const unsigned char byte : value)
    {
        const bool unreserved = (byte >= 'A' && byte <= 'Z')
            || (byte >= 'a' && byte <= 'z')
            || (byte >= '0' && byte <= '9')
            || byte == '-' || byte == '.' || byte == '_' || byte == '~';
        if (unreserved)
        {
            result.push_back(static_cast<char>(byte));
        }
        else
        {
            result.push_back('%');
            result.push_back(kHex[byte >> 4]);
            result.push_back(kHex[byte & 0x0F]);
        }
    }
    return result;
}

} // namespace draxul::http
