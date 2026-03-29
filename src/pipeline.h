#ifndef PIPELINE_H
#define PIPELINE_H

#include <vector>
#include "platform.h"
#include "task.h"
#include "cli_options.h"

// Collect input/output file paths based on CLI options.
// Returns 0 on success, -1 on error.
int collect_file_paths(const CliOptions &options,
                       std::vector<path_t> &input_files,
                       std::vector<path_t> &output_files);

// Orchestrates the three-stage upscaling pipeline (load -> process -> save).
class UpscalePipeline
{
public:
    // Run the full upscaling pipeline.
    // Returns 0 on success, non-zero on failure.
    int run(CliOptions &options,
            const std::vector<path_t> &input_files,
            const std::vector<path_t> &output_files);

private:
    TaskQueue toproc_;
    TaskQueue tosave_;
};

#endif // PIPELINE_H
