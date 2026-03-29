#include "filesystem_utils.h"
#include <cstring>

static constexpr int MAX_PATH_LENGTH = 256;

bool is_image_file(const std::string &filename)
{
    const std::vector<std::string> extensions = {".jpg", ".jpeg", ".png", ".bmp", ".webp"};

    // Check if the filename contains a dot
    size_t last_dot = filename.find_last_of(".");
    if (last_dot == std::string::npos)
        return false; // No extension

    // Check if it's a hidden file without an extension
    if (filename[0] == '.' && last_dot == 0)
        return false; // No extension

    // Extract the extension and check if it's an image file extension
    std::string extension = filename.substr(last_dot);
    std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);
    return std::find(extensions.begin(), extensions.end(), extension) != extensions.end();
}

#if _WIN32
bool path_is_directory(const path_t &path)
{
    DWORD attr = GetFileAttributesW(path.c_str());
    return (attr != INVALID_FILE_ATTRIBUTES) && (attr & FILE_ATTRIBUTE_DIRECTORY);
}

int list_directory(const path_t &dirpath, std::vector<path_t> &imagepaths)
{
    imagepaths.clear();

    _WDIR *dir = _wopendir(dirpath.c_str());
    if (!dir)
    {
        fwprintf(stderr, L"🚨 Error: Failed to open directory - %ls\n", dirpath.c_str());
        return -1;
    }

    struct _wdirent *ent = 0;
    while ((ent = _wreaddir(dir)))
    {
        if (ent->d_type != DT_REG)
            continue;

        // Convert wide string to regular string
        std::wstring wfilename(ent->d_name);
        std::string filename(wfilename.begin(), wfilename.end());

        if (is_image_file(filename))
        {
            imagepaths.push_back(path_t(wfilename));
        }
    }

    _wclosedir(dir);
    std::sort(imagepaths.begin(), imagepaths.end());

    return 0;
}
#else  // _WIN32
bool path_is_directory(const path_t &path)
{
    struct stat s;
    if (stat(path.c_str(), &s) != 0)
        return false;
    return S_ISDIR(s.st_mode);
}

int list_directory(const path_t &dirpath, std::vector<path_t> &imagepaths)
{
    imagepaths.clear();

    DIR *dir = opendir(dirpath.c_str());
    if (!dir)
    {
        fprintf(stderr, "🚨 Error: Failed to open directory - %s\n", dirpath.c_str());
        return -1;
    }

    struct dirent *ent = 0;
    while ((ent = readdir(dir)))
    {
        if (ent->d_type != DT_REG && ent->d_type != DT_LNK && ent->d_type != DT_UNKNOWN)
            continue;

        // For symlinks and unknown types, verify it's a regular file via stat
        if (ent->d_type == DT_LNK || ent->d_type == DT_UNKNOWN)
        {
            std::string fullpath = std::string(dirpath) + "/" + ent->d_name;
            struct stat s;
            if (stat(fullpath.c_str(), &s) != 0 || !S_ISREG(s.st_mode))
                continue;
        }

        std::string filename(ent->d_name);

        if (is_image_file(filename))
        {
            imagepaths.push_back(path_t(filename));
        }
    }

    closedir(dir);
    std::sort(imagepaths.begin(), imagepaths.end());

    return 0;
}
#endif // _WIN32

path_t get_file_name_without_extension(const path_t &path)
{
    size_t dot = path.rfind(PATHSTR('.'));
    if (dot == path_t::npos)
        return path;

    return path.substr(0, dot);
}

path_t get_file_extension(const path_t &path)
{
    size_t dot = path.rfind(PATHSTR('.'));
    if (dot == path_t::npos)
        return path_t();

    return path.substr(dot + 1);
}

#if _WIN32
path_t get_executable_directory()
{
    wchar_t filepath[MAX_PATH_LENGTH];
    GetModuleFileNameW(NULL, filepath, MAX_PATH_LENGTH);

    wchar_t *backslash = wcsrchr(filepath, L'\\');
    if (!backslash)
        return path_t();
    backslash[1] = L'\0';

    return path_t(filepath);
}
#elif __APPLE__
path_t get_executable_directory()
{
    char filepath[MAX_PATH_LENGTH];
    uint32_t size = sizeof(filepath);
    _NSGetExecutablePath(filepath, &size);

    char *slash = strrchr(filepath, '/');
    if (!slash)
        return path_t();
    slash[1] = '\0';

    return path_t(filepath);
}
#else
path_t get_executable_directory()
{
    char filepath[MAX_PATH_LENGTH];
    ssize_t len = readlink("/proc/self/exe", filepath, sizeof(filepath) - 1);
    if (len == -1)
        return path_t();
    filepath[len] = '\0';

    char *slash = strrchr(filepath, '/');
    if (!slash)
        return path_t();
    slash[1] = '\0';

    return path_t(filepath);
}
#endif

bool filepath_is_readable(const path_t &path)
{
#if _WIN32
    FILE *fp = _wfopen(path.c_str(), L"rb");
#else  // _WIN32
    FILE *fp = fopen(path.c_str(), "rb");
#endif // _WIN32
    if (!fp)
        return false;

    fclose(fp);
    return true;
}

path_t sanitize_filepath(const path_t &path)
{
    if (filepath_is_readable(path))
        return path;

    return get_executable_directory() + path;
}
