/* lodepng_wrapper.cpp — exposes lodepng_decode32_file with C linkage
   so that C translation units (hal_wasm.c) can call it without
   C++ name-mangling issues. */
#include "lodepng.h"

extern "C" unsigned lodepng_decode32_file_w(unsigned char **out,
                                              unsigned *w, unsigned *h,
                                              const char *filename)
{
    return lodepng_decode32_file(out, w, h, filename);
}
