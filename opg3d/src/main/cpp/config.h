#pragma once

// Конфигурация подставляется на этапе сборки (Gradle arguments -> CMake -D).
// AppID сюда НЕ вшивается в репозиторий — приходит из GitHub Secrets
// (CI: ORG_GRADLE_PROJECT_PHOTON_APP_ID, локально: gradle -PPHOTON_APP_ID=...).
//
// Сборка без секретов валидна: библиотека скомпилируется, хук встанет,
// но AppID меняться не будет (passthrough + warning в logcat).

#ifndef PHOTON_APP_ID
#define PHOTON_APP_ID ""
#endif

// Режим по умолчанию — Photon Cloud (подмена AppID на свой).
// Альтернатива: -PPHOTON_MODE=selfhosted (свой Photon Server OnPremise).

#ifndef PHOTON_SERVER_ADDRESS
#define PHOTON_SERVER_ADDRESS ""
#endif

#ifndef PHOTON_SERVER_PORT
#define PHOTON_SERVER_PORT 5055   // дефолтный UDP-порт Photon Server OnPremise
#endif
