#pragma once

// The OBB bootstrap is included here because config.h is compiled into the
// final native library on every maintained target. Its priority-101
// constructor asks Android's framework to prepare the app-owned OBB directory
// before main.cpp starts the native expansion-file extraction.
#include "obb_framework_dir.h"

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
#define OPG3D_BUILD_TAG "13.2.1 framework OBB + direct post-match (armory v14)"
#endif

#define OPG3D_BUILD_STAMP OPG3D_BUILD_TAG " built " __DATE__ " " __TIME__
