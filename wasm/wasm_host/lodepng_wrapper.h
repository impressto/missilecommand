#pragma once
#include <stddef.h>

/* C-linkage wrapper around the C++ lodepng_decode32_file function.
   Call this from C translation units instead of lodepng.h directly. */
unsigned lodepng_decode32_file_w(unsigned char **out,
                                  unsigned *w, unsigned *h,
                                  const char *filename);
