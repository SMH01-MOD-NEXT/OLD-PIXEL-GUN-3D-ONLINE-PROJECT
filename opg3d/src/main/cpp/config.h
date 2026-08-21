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

// Human-readable identity printed before any runtime diagnostics. Keep this
// target-specific: mixing the 13.2.1 and 14.1.1 libraries is unsafe even
// though ordinary hooks resolve through metadata.
#ifndef OPG3D_BUILD_TAG
#define OPG3D_BUILD_TAG "14.1.1 full port v1 (re-sign + direct post-match + lobby)"
#endif

#define OPG3D_BUILD_STAMP OPG3D_BUILD_TAG " built " __DATE__ " " __TIME__
