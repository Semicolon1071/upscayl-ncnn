#ifndef IMAGE_UTILS_H
#define IMAGE_UTILS_H

#include <string>
#include <vector>

// Resize filter mode names (matches stbir_filter enum order)
extern const char *const resize_mode_names[];
extern const int resize_mode_count;

// Parse a comma-separated string of integers, e.g. "100,200,300"
std::vector<int> parse_int_array(const char *input);

// Parse a resize specification like "1920x1080" or "1920x1080:catmullrom"
// Returns true on success, fills width/height/mode.
// When width_only is true, parses just a single integer for width.
bool parse_resize_spec(const char *input, int *width, int *height, int *mode,
                       bool width_only = false);

// Given a model name string, detect the scale factor from patterns
// like "x2", "2x", "x4plus", etc. Returns 0 if no scale found.
int detect_scale_from_model_name(const std::string &modelname);

// Given input dimensions and a target width, calculate the proportional height.
int calculate_proportional_height(int input_width, int input_height, int target_width);

// Given image dimension and tile size, return number of tiles needed (rounds up).
int calculate_tile_count(int dimension, int tile_size);

// Validate compression is in [0, 100] range, then round to nearest 10.
// Returns -1 if invalid.
float validate_and_round_compression(float compression);

// Convert 1-channel grayscale to 3-channel RGB.
// Caller must free the returned buffer. Returns nullptr on failure.
unsigned char *grayscale_to_rgb(const unsigned char *pixeldata, int w, int h);

// Convert 2-channel grayscale+alpha to 4-channel RGBA.
// Caller must free the returned buffer. Returns nullptr on failure.
unsigned char *grayscale_alpha_to_rgba(const unsigned char *pixeldata, int w, int h);

#endif // IMAGE_UTILS_H
