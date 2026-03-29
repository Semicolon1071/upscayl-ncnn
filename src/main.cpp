// realesrgan implemented with ncnn library

#include <clocale>
#include <vector>

#include "platform.h"
#include "cli_options.h"
#include "pipeline.h"

#if _WIN32
int wmain(int argc, wchar_t **argv)
#else
int main(int argc, char **argv)
#endif
{
    setlocale(LC_ALL, "");

    CliOptions options;
    if (parse_command_line(argc, argv, options) != 0)
        return -1;

    std::vector<path_t> input_files;
    std::vector<path_t> output_files;
    if (collect_file_paths(options, input_files, output_files) != 0)
        return -1;

    UpscalePipeline pipeline;
    return pipeline.run(options, input_files, output_files);
}
