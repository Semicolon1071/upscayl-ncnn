#ifndef WEBP_IMAGE_H
#define WEBP_IMAGE_H

#include "platform.h"

unsigned char *webp_load(const unsigned char *buffer, int len, int *w, int *h, int *c);

#if _WIN32
int webp_save(const wchar_t *filepath, int w, int h, int c, const unsigned char *pixeldata, int quality);
#else
int webp_save(const char *filepath, int w, int h, int c, const unsigned char *pixeldata, int quality);
#endif

#endif // WEBP_IMAGE_H
