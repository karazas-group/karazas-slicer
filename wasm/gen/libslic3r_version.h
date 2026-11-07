#ifndef __SLIC3R_VERSION_H
#define __SLIC3R_VERSION_H

#define SLIC3R_APP_NAME "Karaza"
#define SLIC3R_APP_KEY "Karaza"
#define SLIC3R_VERSION "2.4.2"
#define SoftFever_VERSION "2.4.2"
#ifndef GIT_COMMIT_HASH
    #define GIT_COMMIT_HASH "0000000" // 0000000 means uninitialized
#endif
#define SLIC3R_BUILD_ID "wasm"
//#define SLIC3R_RC_VERSION "2.4.2"
#define BBL_INTERNAL_TESTING 0
#define ORCA_CHECK_GCODE_PLACEHOLDERS 0

#endif /* __SLIC3R_VERSION_H */
