#ifndef CLI_OPTIONS_H
#define CLI_OPTIONS_H

#include <vector>
#include "platform.h"

struct CliOptions
{
    path_t inputpath;
    path_t outputpath;
    int scale = 4;
    int outputScale = 4;
    bool hasOutputScale = false;
    float compression = 0.00f;
    int resizeWidth = 0;
    int resizeHeight = 0;
    int resizeMode = 0;
    bool resizeProvided = false;
    bool hasCustomWidth = false;
    std::vector<int> tilesize;
    path_t model = PATHSTR("models");
    path_t modelname = PATHSTR("realesrgan-x4plus");
    std::vector<int> gpuid;
    int jobs_load = 1;
    std::vector<int> jobs_proc;
    int jobs_save = 2;
    int verbose = 0;
    int tta_mode = 0;
    path_t format = PATHSTR("png");
};

// Parse command-line arguments into a CliOptions struct.
// Returns 0 on success, -1 on error (prints usage/error to stderr).
#if _WIN32
int parse_command_line(int argc, wchar_t **argv, CliOptions &options);
#else
int parse_command_line(int argc, char **argv, CliOptions &options);
#endif

#endif // CLI_OPTIONS_H
