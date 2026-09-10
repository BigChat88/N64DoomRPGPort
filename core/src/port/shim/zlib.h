/*
 * zlib.h  --  redirect DoomRPG-RE's <zlib.h> use onto bundled miniz.
 *
 * Z_Zip.c is the only translation unit that includes <zlib.h>, and it only
 * needs the raw-inflate streaming API (inflateInit2(-15) / inflate / inflateEnd).
 * miniz provides drop-in zlib-compatible names, so including it here means the
 * implementation is compiled exactly once (inside Z_Zip.c) with no extra .c file.
 */
#ifndef PORT_SHIM_ZLIB_H__
#define PORT_SHIM_ZLIB_H__

#define MINIZ_NO_STDIO
#define MINIZ_NO_TIME
#define MINIZ_NO_ARCHIVE_APIS
#define MINIZ_NO_ARCHIVE_WRITING_APIS

#include "miniz.h"

#endif /* PORT_SHIM_ZLIB_H__ */
