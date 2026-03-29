// Single compilation unit for STB header-only library implementations.
// These #define IMPLEMENTATION macros must appear in exactly one .cpp file.

#if !_WIN32
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_PSD
#define STBI_NO_TGA
#define STBI_NO_GIF
#define STBI_NO_HDR
#define STBI_NO_PIC
#define STBI_NO_STDIO
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#endif

#define STB_IMAGE_RESIZE2_IMPLEMENTATION
#include "stb_image_resize2.h"
