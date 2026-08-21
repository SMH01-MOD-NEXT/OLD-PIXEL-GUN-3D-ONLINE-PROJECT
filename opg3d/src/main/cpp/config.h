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

// Human-readable identity of the source revision this library was compiled
// from. It is printed as the first line of the init log so a stale
// libopg3d.so can be recognised immediately instead of being mistaken for a
// failing hook. Bump the feature tag whenever runtime behaviour changes.
#ifndef OPG3D_BUILD_TAG
#define OPG3D_BUILD_TAG "13.2.1 obb self-provisioning (armory v11)"
#endif

#define OPG3D_BUILD_STAMP OPG3D_BUILD_TAG " built " __DATE__ " " __TIME__
