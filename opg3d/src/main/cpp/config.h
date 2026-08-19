#pragma once

// Значения приходят только на этапе сборки:
// GitHub Actions: ORG_GRADLE_PROJECT_PHOTON_APP_ID из repository secret.
// Локально:       gradle ... -PPHOTON_APP_ID=...
// Credential никогда не хранится в исходниках и никогда целиком не логируется.
#ifndef PHOTON_APP_ID
#define PHOTON_APP_ID ""
#endif

#ifndef PHOTON_SERVER_ADDRESS
#define PHOTON_SERVER_ADDRESS ""
#endif

#ifndef PHOTON_SERVER_PORT
#define PHOTON_SERVER_PORT 5055
#endif
