#pragma once
// Compatibility convenience header: includes protocol, transport, and UI APIs.
// Prefer the narrower headers in new code:
//   <draxul/nvim_protocol.h> — values, channel contract, and RPC records
//   <draxul/nvim_transport.h> — process transport and threaded NvimRpc
//   <draxul/nvim_ui.h>        — ModeInfo, UiEventHandler, and NvimInput
#include <draxul/nvim_protocol.h>
#include <draxul/nvim_transport.h>
#include <draxul/nvim_ui.h>
