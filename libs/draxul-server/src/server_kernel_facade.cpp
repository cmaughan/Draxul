#include "server_kernel_impl.h"

namespace draxul
{

ServerKernel::ServerKernel(ServerKernelOptions options)
    : impl_(std::make_unique<Impl>(std::move(options)))
{
}

ServerKernel::~ServerKernel()
{
    stop();
}

ServerStartResult ServerKernel::start()
{
    return impl_->start();
}

int ServerKernel::run_until_stopped()
{
    return impl_->run_until_stopped();
}

void ServerKernel::request_stop()
{
    impl_->request_stop();
}

void ServerKernel::stop()
{
    impl_->stop();
}

bool ServerKernel::running() const
{
    return impl_->started;
}

const std::string& ServerKernel::epoch() const
{
    return impl_->epoch_value;
}

uint64_t ServerKernel::process_id() const
{
    return impl_->pid;
}

ServerStatusSnapshot ServerKernel::status_snapshot() const
{
    return impl_->status_snapshot();
}

} // namespace draxul
