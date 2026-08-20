#pragma once

// Values are provided at build time only:
// GitHub Actions: ORG_GRADLE_PROJECT_PHOTON_APP_ID from a repository secret.
// Local:          gradle ... -PPHOTON_APP_ID=...
// The credential is never stored in the sources and never logged in full.
#ifndef PHOTON_APP_ID
#define PHOTON_APP_ID ""
#endif

#ifndef PHOTON_SERVER_ADDRESS
#define PHOTON_SERVER_ADDRESS ""
#endif

#ifndef PHOTON_SERVER_PORT
#define PHOTON_SERVER_PORT 5055
#endif
