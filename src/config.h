#pragma once

// Конфигурация подставляется CMake'ом на этапе сборки.
// AppID сюда НЕ вшивается в репозиторий — приходит из GitHub Secrets
// (workflow: -DPHOTON_APP_ID="${{ secrets.PHOTON_APP_ID }}").
//
// Сборка без секретов валидна: библиотека скомпилируется, хук встанет,
// но AppID меняться не будет (passthrough + warning в logcat).

#ifndef PHOTON_APP_ID
#define PHOTON_APP_ID ""
#endif

// Режим по умолчанию — Photon Cloud (подмена AppID на свой).
// Альтернатива: -DPHOTON_MODE=selfhosted (свой Photon Server OnPremise).

#ifndef PHOTON_SERVER_ADDRESS
#define PHOTON_SERVER_ADDRESS ""
#endif

#ifndef PHOTON_SERVER_PORT
#define PHOTON_SERVER_PORT 5055   // дефолтный UDP-порт Photon Server OnPremise
#endif
