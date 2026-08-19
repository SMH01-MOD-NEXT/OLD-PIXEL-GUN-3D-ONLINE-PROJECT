#pragma once

namespace photon {

enum class ApplyResult {
    NoClass,         // класс/поле не найдены (чужая сборка игры?)
    NotReady,        // игра ещё не создала объект настроек
    Applied,         // подмена только что записана
    AlreadyApplied,  // всё на месте, делать нечего
};

// Читает PhotonNetwork.PhotonServerSettings и при необходимости переписывает поля.
// Вызывать только из потока, присоединённого через il2cpp_thread_attach.
ApplyResult apply();

} // namespace photon
