#pragma once

namespace photon {

// Installs the mandatory AppID-selection hook plus the network-path
// diagnostic hooks. Call once from a thread attached to the IL2CPP runtime.
bool install_hooks();

} // namespace photon
