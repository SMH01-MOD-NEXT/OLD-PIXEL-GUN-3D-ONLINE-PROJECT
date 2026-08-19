#pragma once

namespace photon {

// Ставит обязательный хук выбора AppID и диагностические хуки сетевого пути.
// Вызывать один раз из потока, присоединённого к IL2CPP runtime.
bool install_hooks();

} // namespace photon
