#pragma once

#include "runtime/RuntimeController.h"

namespace llocr {

// The enums live on RuntimeController (Q_ENUM exposure for QML);
// these aliases keep the original C++ spelling working.
using RuntimeState = RuntimeController::RuntimeState;
using AppBusyState = RuntimeController::AppBusyState;

}  // namespace llocr
