#pragma once
/* net68k_prefs.h — identity of the engine's own prefs file.
 *
 * Claude 68 kept these two macros in its src/config.h next to the app
 * identity and API endpoint. The engine needs neither, so the vendored
 * copy of prefs_io.cpp takes them from here instead (see UPSTREAM.md).
 *
 * The file holds only the engine's TLS acceleration caches and the
 * client random seed. Application settings live in the application's
 * own prefs file.
 */

#define PREFS_FILE         "\pNet68k Cache"  /* HFS filename (Pascal) */
#define PREFS_FILE_CREATOR 'McAI'            /* 4-char creator code   */
