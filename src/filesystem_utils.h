#ifndef FILESYSTEM_UTILS_H
#define FILESYSTEM_UTILS_H

#include <stdio.h>
#include <vector>
#include <string>
#include <algorithm>

#include "platform.h"

#if _WIN32
#include "win32dirent.h"
#else // _WIN32
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#endif // _WIN32

#if __APPLE__
#include <mach-o/dyld.h>
#endif

bool is_image_file(const std::string &filename);
bool path_is_directory(const path_t &path);
int list_directory(const path_t &dirpath, std::vector<path_t> &imagepaths);
path_t get_file_name_without_extension(const path_t &path);
path_t get_file_extension(const path_t &path);
path_t get_executable_directory();
bool filepath_is_readable(const path_t &path);
path_t sanitize_filepath(const path_t &path);

#endif // FILESYSTEM_UTILS_H
