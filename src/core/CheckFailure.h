#pragma once

namespace llocr {

// Why a check did not pass. The kind travels from the runtime to the wizard's
// last step, where it becomes the plain-language answer: what went wrong and
// what the user can do about it. The message alone cannot carry that — it is
// written for the log, and it mixes the cause with the raw server output.
enum class CheckFailure : int {
    None = 0,
    Unknown,
    RuntimeMissing,      ///< the llama.cpp runtime is not on disk
    ModelMissing,        ///< the model file of the task is not on disk
    DownloadFailed,      ///< the runtime or the model could not be downloaded
    PortBusy,            ///< the port the server wants is taken
    NotEnoughMemory,     ///< the model did not fit into the memory
    BackendUnavailable,  ///< the GPU backend cannot start on this computer
    ModelRejected,       ///< the server cannot read the model file
    InstanceRunning,     ///< another app instance holds the runtime
    AutostartOff,        ///< the server is not set to start on its own
    ServerStartFailed,   ///< the server died for another reason
    NoAnswer,            ///< the server is up but did not answer in time
    RequestRejected,     ///< the server answered the request with an error
};

// The key the UI reads: a stable name, so the QML side does not depend on the
// order of the enum above.
inline const char *checkFailureKey(CheckFailure failure)
{
    switch (failure) {
    case CheckFailure::None:
        return "none";
    case CheckFailure::RuntimeMissing:
        return "runtimeMissing";
    case CheckFailure::ModelMissing:
        return "modelMissing";
    case CheckFailure::DownloadFailed:
        return "downloadFailed";
    case CheckFailure::PortBusy:
        return "portBusy";
    case CheckFailure::NotEnoughMemory:
        return "notEnoughMemory";
    case CheckFailure::BackendUnavailable:
        return "backendUnavailable";
    case CheckFailure::ModelRejected:
        return "modelRejected";
    case CheckFailure::InstanceRunning:
        return "instanceRunning";
    case CheckFailure::AutostartOff:
        return "autostartOff";
    case CheckFailure::ServerStartFailed:
        return "serverStartFailed";
    case CheckFailure::NoAnswer:
        return "noAnswer";
    case CheckFailure::RequestRejected:
        return "requestRejected";
    case CheckFailure::Unknown:
        break;
    }
    return "unknown";
}

}  // namespace llocr
