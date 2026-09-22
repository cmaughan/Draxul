#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <utility>

namespace draxul::remote_session_client_test_seam
{

using BeforeWaitHook = std::function<void()>;

// Tests use this private seam to pause one named client after its wait
// predicate evaluates false but before condition_variable::wait enters.
void set_before_wait_hook(
    std::string client_id, BeforeWaitHook hook);
void clear_before_wait_hook(std::string_view client_id);

} // namespace draxul::remote_session_client_test_seam
