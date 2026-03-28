#include "image_utils.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

const char *const resize_mode_names[] = {
    "default",      // STBIR_FILTER_DEFAULT
    "box",          // STBIR_FILTER_BOX
    "triangle",     // STBIR_FILTER_TRIANGLE
    "cubicbspline", // STBIR_FILTER_CUBICBSPLINE
    "catmullrom",   // STBIR_FILTER_CATMULLROM
    "mitchell",     // STBIR_FILTER_MITCHELL
    "pointsample"   // STBIR_FILTER_POINT_SAMPLE
};

const int resize_mode_count = sizeof(resize_mode_names) / sizeof(resize_mode_names[0]);

std::vector<int> parse_int_array(const char *input)
{
    std::vector<int> array;
    array.push_back(atoi(input));

    const char *p = strchr(input, ',');
    while (p)
    {
        p++;
        array.push_back(atoi(p));
        p = strchr(p, ',');
    }

    return array;
}

bool parse_resize_spec(const char *input, int *width, int *height, int *mode,
                       bool width_only)
{
    *mode = 0; // default

    const char *colon = strchr(input, ':');
    if (colon)
    {
        bool found = false;
        const char *modestr = colon + 1;
        for (int i = 0; i < resize_mode_count; i++)
        {
            if (strcmp(modestr, resize_mode_names[i]) == 0)
            {
                *mode = i;
                found = true;
                break;
            }
        }
        if (!found)
        {
            return false;
        }
    }

    if (width_only)
    {
        return sscanf(input, "%d", width) == 1;
    }

    return sscanf(input, "%dx%d", width, height) == 2;
}

int detect_scale_from_model_name(const std::string &modelname)
{
    // Check larger scales first to avoid "x16" matching "x1"
    if (modelname.find("x16") != std::string::npos || modelname.find("16x") != std::string::npos)
        return 16;
    else if (modelname.find("x8") != std::string::npos || modelname.find("8x") != std::string::npos)
        return 8;
    else if (modelname.find("x4") != std::string::npos || modelname.find("4x") != std::string::npos)
        return 4;
    else if (modelname.find("x3") != std::string::npos || modelname.find("3x") != std::string::npos)
        return 3;
    else if (modelname.find("x2") != std::string::npos || modelname.find("2x") != std::string::npos)
        return 2;
    else if (modelname.find("x1") != std::string::npos || modelname.find("1x") != std::string::npos)
        return 1;

    return 0;
}

int calculate_proportional_height(int input_width, int input_height, int target_width)
{
    return (input_height * target_width) / input_width;
}

int calculate_tile_count(int dimension, int tile_size)
{
    return (dimension + tile_size - 1) / tile_size;
}

float validate_and_round_compression(float compression)
{
    if (compression < 0 || compression > 100)
        return -1;

    return round(compression / 10.0) * 10;
}

unsigned char *grayscale_to_rgb(const unsigned char *pixeldata, int w, int h)
{
    unsigned char *rgbdata = (unsigned char *)malloc(w * h * 3);
    if (!rgbdata)
        return nullptr;

    for (int i = 0; i < w * h; i++)
    {
        unsigned char gray = pixeldata[i];
        rgbdata[i * 3 + 0] = gray;
        rgbdata[i * 3 + 1] = gray;
        rgbdata[i * 3 + 2] = gray;
    }

    return rgbdata;
}

unsigned char *grayscale_alpha_to_rgba(const unsigned char *pixeldata, int w, int h)
{
    unsigned char *rgbadata = (unsigned char *)malloc(w * h * 4);
    if (!rgbadata)
        return nullptr;

    for (int i = 0; i < w * h; i++)
    {
        unsigned char gray = pixeldata[i * 2];
        unsigned char alpha = pixeldata[i * 2 + 1];
        rgbadata[i * 4 + 0] = gray;
        rgbadata[i * 4 + 1] = gray;
        rgbadata[i * 4 + 2] = gray;
        rgbadata[i * 4 + 3] = alpha;
    }

    return rgbadata;
}
