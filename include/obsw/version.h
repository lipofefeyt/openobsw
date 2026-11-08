#ifndef OBSW_VERSION_H
#define OBSW_VERSION_H

/**
 * @file obsw/version.h
 * @brief openobsw software version — the single source of truth.
 *
 * Bump on every release, together with the git tag. The root CMakeLists.txt
 * reads these numbers for project(VERSION), so the two cannot drift.
 * Distinct from SRDB_VERSION, which versions the SRDB data set.
 */

#define OBSW_VERSION_MAJOR 0
#define OBSW_VERSION_MINOR 18
#define OBSW_VERSION_PATCH 0

#define OBSW_VERSION_STR_(x) #x
#define OBSW_VERSION_STR(x)  OBSW_VERSION_STR_(x)

/** "MAJOR.MINOR.PATCH", e.g. "0.18.0" */
#define OBSW_VERSION_STRING                                                    \
    OBSW_VERSION_STR(OBSW_VERSION_MAJOR)                                       \
    "." OBSW_VERSION_STR(OBSW_VERSION_MINOR) "." OBSW_VERSION_STR(OBSW_VERSION_PATCH)

#endif /* OBSW_VERSION_H */
