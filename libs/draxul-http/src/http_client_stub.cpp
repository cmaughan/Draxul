#include <draxul/http/http_client.h>

namespace draxul::http
{
namespace
{
class UnsupportedHttpClient final : public IHttpClient
{
public:
    Response get(const Request&, CancellationToken cancellation) override
    {
        Response response;
        response.cancelled = cancellation.is_cancelled();
        response.error = response.cancelled ? "request cancelled" : "native HTTP is unsupported on this platform";
        return response;
    }
};
} // namespace

std::shared_ptr<IHttpClient> create_platform_http_client()
{
    return std::make_shared<UnsupportedHttpClient>();
}

} // namespace draxul::http
