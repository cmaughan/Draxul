#pragma once

// Compatibility include for app code. Durable Session values and codec APIs
// are owned below the UI in draxul-session-model.
#include <draxul/session_state.h>

// Preserve the former transitive app types while callers migrate to the
// renderer-neutral names.
#include "pane_manager.h"
