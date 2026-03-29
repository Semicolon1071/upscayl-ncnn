#ifndef PLATFORM_H
#define PLATFORM_H

#include <string>

#if _WIN32
#include <windows.h>
typedef std::wstring path_t;
#define PATHSTR(X) L##X
#else
typedef std::string path_t;
#define PATHSTR(X) X
#endif

#endif // PLATFORM_H
