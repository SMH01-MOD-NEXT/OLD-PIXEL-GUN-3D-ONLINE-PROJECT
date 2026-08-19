#pragma once

namespace photon {

// Ставит хук на PhotonNetwork.ConnectUsingSettings (il2cpp_base + RVA из дампа).
// true = хук установлен.
bool install_hook(void* il2cpp_base);

} // namespace photon
