#pragma once

namespace llocr {

// Two connection modes the app can be in. `External` is the default and covers
// every pre-existing profile: the app only holds `ProviderConfig` pointing at
// a server it does not manage. `Managed` means the app owns a
// `llama-server` process it starts on loopback and stops on exit.
enum class ConnectionMode {
    External,
    Managed,
};

}  // namespace llocr