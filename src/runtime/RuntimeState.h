#pragma once

namespace llocr {

// App-wide busy states. These are exclusive: at any moment the app is in at
// most one. A single `busy` bool is not enough once the app can start and stop
// a local server, download files, or install a runtime.
enum class AppBusyState {
    Idle,
    StartingRuntime,  // server is starting / loading the model
    Recognizing,
    StoppingRuntime,
    Downloading,      // model or runtime download in progress
    Installing,       // archive extraction / verification
};

// State of the managed llama-server process (only meaningful in `Managed`).
enum class RuntimeState {
    NotConfigured,  // no valid binary / no model selected
    Stopped,
    Starting,
    Ready,
    Stopping,
    Failed,
};

}  // namespace llocr