#include "cli_options.h"
#include "image_utils.h"
#include "filesystem_utils.h"

#include <stdio.h>
#include <cstring>
#include <vector>

#if _WIN32
#include <wchar.h>

static wchar_t *cli_optarg = NULL;
static int cli_optind = 1;

static wchar_t cli_getopt(int argc, wchar_t *const argv[], const wchar_t *optstring)
{
    if (cli_optind >= argc || argv[cli_optind][0] != L'-')
        return -1;

    wchar_t opt = argv[cli_optind][1];
    const wchar_t *p = wcschr(optstring, opt);
    if (p == NULL)
        return L'?';

    cli_optarg = NULL;

    if (p[1] == L':')
    {
        cli_optind++;
        if (cli_optind >= argc)
            return L'?';

        cli_optarg = argv[cli_optind];
    }

    cli_optind++;

    return opt;
}

static std::vector<int> parse_optarg_int_array(const wchar_t *optarg)
{
    std::vector<int> array;
    array.push_back(_wtoi(optarg));

    const wchar_t *p = wcschr(optarg, L',');
    while (p)
    {
        p++;
        array.push_back(_wtoi(p));
        p = wcschr(p, L',');
    }

    return array;
}

static bool ascii_string_equals(const wchar_t *wide, const char *narrow)
{
    size_t widelen = wcslen(wide);
    size_t narrowlen = strlen(narrow);

    if (widelen != narrowlen)
        return false;

    for (size_t i = 0; i < widelen; i++)
    {
        if (wide[i] != narrow[i])
            return false;
    }

    return true;
}

static bool parse_optarg_resize(const wchar_t *optarg, int *width, int *height, int *mode, bool hasCustomWidth = false)
{
    *mode = 0; // default

    const wchar_t *colon = wcschr(optarg, L':');
    if (colon)
    {
        bool found = false;
        const wchar_t *modestr = colon + 1;
        for (int i = 0; i < resize_mode_count; i++)
        {
            if (ascii_string_equals(modestr, resize_mode_names[i]))
            {
                *mode = i;
                found = true;
                break;
            }
        }
        if (!found)
        {
            fwprintf(stderr, L"🚨 Error: Invalid resize mode '%s'\n", modestr);
            return false;
        }
    }

    if (hasCustomWidth)
    {
        return swscanf(optarg, L"%d", width) == 1;
    }

    return swscanf(optarg, L"%dx%d", width, height) == 2;
}

#else               // _WIN32
#include <unistd.h> // getopt()

static std::vector<int> parse_optarg_int_array(const char *optarg)
{
    return parse_int_array(optarg);
}

static bool parse_optarg_resize(const char *optarg, int *width, int *height, int *mode, bool hasCustomWidth = false)
{
    if (!parse_resize_spec(optarg, width, height, mode, hasCustomWidth))
    {
        if (!hasCustomWidth)
        {
            const char *colon = strchr(optarg, ':');
            if (colon)
                fprintf(stderr, "🚨 Error: Invalid resize mode '%s'\n", colon + 1);
        }
        return false;
    }
    return true;
}

#endif // _WIN32

static void print_usage()
{
    fprintf(stderr, "Usage: upscayl-bin -i infile -o outfile [options]...\n\n");
    fprintf(stderr, "  -h                   show this help\n");
    fprintf(stderr, "  -i input-path        input image path (jpg/png/webp) or directory\n");
    fprintf(stderr, "  -o output-path       output image path (jpg/png/webp) or directory\n");
    fprintf(stderr, "  -z model-scale       scale according to the model (can be 2, 3, 4. default=4)\n");
    fprintf(stderr, "  -s output-scale      custom output scale (can be 2, 3, 4. default=4)\n");
    fprintf(stderr, "  -r resize            resize output to dimension (default=WxH:default), use '-r help' for more details\n");
    fprintf(stderr, "  -w width             resize output to a width (default=W:default), use '-r help' for more details\n");
    fprintf(stderr, "  -c compress          compression of the output image, default 0 and varies to 100\n");
    fprintf(stderr, "  -t tile-size         tile size (>=32/0=auto, default=0) can be 0,0,0 for multi-gpu\n");
    fprintf(stderr, "  -m model-path        folder path to the pre-trained models. default=models\n");
    fprintf(stderr, "  -n model-name        model name (default=realesrgan-x4plus, can be realesr-animevideov3 | realesrgan-x4plus-anime | realesrnet-x4plus or any other model)\n");
    fprintf(stderr, "  -g gpu-id            gpu device to use (default=auto) can be 0,1,2 for multi-gpu\n");
    fprintf(stderr, "  -j load:proc:save    thread count for load/proc/save (default=1:2:2) can be 1:2,2,2:2 for multi-gpu\n");
    fprintf(stderr, "  -x                   enable tta mode\n");
    fprintf(stderr, "  -f format            output image format (jpg/png/webp, default=ext/png)\n");
    fprintf(stderr, "  -v                   verbose output\n");
}

static void print_resize_usage()
{
    printf("'-r widthxheight:filter' argument usage:\n\n");

    printf("For example '-r 1920x1080' or '-r 1920x1080:default' will force all output images to be\n");
    printf("resized to 1920x1080 with the default filter if they aren't already.\n");
    printf("Similarly, '-w 1920' will force all output images to be resized to a width of 1920.\n\n");

    printf("Avaliable filters:\n");
    printf("  default       - Automatically decide\n");
    printf("  box           - A trapezoid w/1-pixel wide ramps, same result as box for integer scale ratios\n");
    printf("  triangle      - On upsampling, produces same results as bilinear texture filtering\n");
    printf("  cubicbspline  - The cubic b-spline (aka Mitchell-Netrevalli with B=1,C=0), gaussian-esque\n");
    printf("  catmullrom    - An interpolating cubic spline\n");
    printf("  mitchell      - Mitchell-Netrevalli filter with B=1/3, C=1/3\n");
    printf("  pointsample   - Simple point sampling\n");
}

#if _WIN32
int parse_command_line(int argc, wchar_t **argv, CliOptions &options)
{
    wchar_t opt;
    while ((opt = cli_getopt(argc, argv, L"i:o:z:s:r:w:t:c:m:n:g:j:f:vxh")) != (wchar_t)-1)
    {
        switch (opt)
        {
        case L'i':
            options.inputpath = cli_optarg;
            break;
        case L'o':
            options.outputpath = cli_optarg;
            break;
        case L'z':
            options.scale = _wtoi(cli_optarg);
            break;
        case L's':
            options.outputScale = _wtoi(cli_optarg);
            options.hasOutputScale = true;
            break;
        case L'c':
            options.compression = validate_and_round_compression(_wtof(cli_optarg));
            if (options.compression < 0)
            {
                fwprintf(stderr, L"🚨 Error: Invalid compression value, it should be between 0 and 100!\n");
                return -1;
            }
            break;
        case L'r':
            if (wcscmp(cli_optarg, L"help") == 0)
            {
                print_resize_usage();
                return -1;
            }
            if (!parse_optarg_resize(cli_optarg, &options.resizeWidth, &options.resizeHeight, &options.resizeMode))
            {
                fwprintf(stderr, L"🚨 Error: Invalid resize value!\n");
                return -1;
            }
            options.resizeProvided = true;
            break;
        case L'w':
            if (wcscmp(cli_optarg, L"help") == 0)
            {
                print_resize_usage();
                return -1;
            }
            if (!parse_optarg_resize(cli_optarg, &options.resizeWidth, &options.resizeHeight, &options.resizeMode, true))
            {
                fwprintf(stderr, L"🚨 Error: Invalid resize value!\n");
                return -1;
            }
            options.hasCustomWidth = true;
            break;
        case L't':
            options.tilesize = parse_optarg_int_array(cli_optarg);
            break;
        case L'm':
            options.model = cli_optarg;
            break;
        case L'n':
            options.modelname = cli_optarg;
            break;
        case L'g':
            options.gpuid = parse_optarg_int_array(cli_optarg);
            break;
        case L'j':
        {
            const wchar_t *colon = wcschr(cli_optarg, L':');
            if (!colon)
            {
                fwprintf(stderr, L"🚨 Error: Invalid -j format. Expected load:proc:save (e.g. 1:2:2)\n");
                return -1;
            }
            swscanf(cli_optarg, L"%d:%*[^:]:%d", &options.jobs_load, &options.jobs_save);
            options.jobs_proc = parse_optarg_int_array(colon + 1);
            break;
        }
        case L'f':
            options.format = cli_optarg;
            break;
        case L'v':
            options.verbose = 1;
            break;
        case L'x':
            options.tta_mode = 1;
            break;
        case L'h':
        default:
            print_usage();
            return -1;
        }
    }
#else  // _WIN32
int parse_command_line(int argc, char **argv, CliOptions &options)
{
    int opt;
    fprintf(stderr, "🚀 Starting Upscayl - Copyright © 2024\n");
    while ((opt = getopt(argc, argv, "i:o:z:s:r:w:t:c:m:n:g:j:f:vxh")) != -1)
    {
        switch (opt)
        {
        case 'i':
            options.inputpath = optarg;
            break;
        case 'o':
            options.outputpath = optarg;
            break;
        case 'z':
            options.scale = atoi(optarg);
            break;
        case 's':
            options.outputScale = atoi(optarg);
            options.hasOutputScale = true;
            break;
        case 'c':
            options.compression = validate_and_round_compression(atof(optarg));
            if (options.compression < 0)
            {
                fprintf(stderr, "🚨 Error: Invalid compression value, it should be between 0 and 100!\n");
                return -1;
            }
            break;
        case 'r':
            if (strcmp(optarg, "help") == 0)
            {
                print_resize_usage();
                return -1;
            }
            if (!parse_optarg_resize(optarg, &options.resizeWidth, &options.resizeHeight, &options.resizeMode))
            {
                fprintf(stderr, "🚨 Error: Invalid resize value!\n");
                return -1;
            }
            options.resizeProvided = true;
            break;
        case 'w':
            if (strcmp(optarg, "help") == 0)
            {
                print_resize_usage();
                return -1;
            }
            if (!parse_optarg_resize(optarg, &options.resizeWidth, &options.resizeHeight, &options.resizeMode, true))
            {
                fprintf(stderr, "🚨 Error: Invalid resize value!\n");
                return -1;
            }
            options.hasCustomWidth = true;
            break;
        case 't':
            options.tilesize = parse_optarg_int_array(optarg);
            break;
        case 'm':
            options.model = optarg;
            break;
        case 'n':
            options.modelname = optarg;
            break;
        case 'g':
            options.gpuid = parse_optarg_int_array(optarg);
            break;
        case 'j':
        {
            const char *colon = strchr(optarg, ':');
            if (!colon)
            {
                fprintf(stderr, "🚨 Error: Invalid -j format. Expected load:proc:save (e.g. 1:2:2)\n");
                return -1;
            }
            sscanf(optarg, "%d:%*[^:]:%d", &options.jobs_load, &options.jobs_save);
            options.jobs_proc = parse_optarg_int_array(colon + 1);
            break;
        }
        case 'f':
            options.format = optarg;
            break;
        case 'v':
            options.verbose = 1;
            break;
        case 'x':
            options.tta_mode = 1;
            break;
        case 'h':
        default:
            print_usage();
            return -1;
        }
    }
#endif // _WIN32

    if (options.inputpath.empty() || options.outputpath.empty())
    {
        print_usage();
        return -1;
    }

    if (options.tilesize.size() != (options.gpuid.empty() ? 1 : options.gpuid.size()) && !options.tilesize.empty())
    {
        fprintf(stderr, "🚨 Error: Invalid tile size!\n");
        return -1;
    }

    for (int i = 0; i < (int)options.tilesize.size(); i++)
    {
        if (options.tilesize[i] != 0 && options.tilesize[i] < 32)
        {
            fprintf(stderr, "🚨 Error: Invalid tile size!\n");
            return -1;
        }
    }

    if (options.jobs_load < 1 || options.jobs_save < 1)
    {
        fprintf(stderr, "🚨 Error: Invalid thread count!\n");
        return -1;
    }

    if (options.jobs_proc.size() != (options.gpuid.empty() ? 1 : options.gpuid.size()) && !options.jobs_proc.empty())
    {
        fprintf(stderr, "🚨 Error: invalid jobs_proc thread count!\n");
        return -1;
    }

    for (int i = 0; i < (int)options.jobs_proc.size(); i++)
    {
        if (options.jobs_proc[i] < 1)
        {
            fprintf(stderr, "🚨 Error: Invalid jobs_proc thread count argument!\n");
            return -1;
        }
    }

    if (!path_is_directory(options.outputpath))
    {
        path_t ext = options.format;

        if (ext == PATHSTR("png") || ext == PATHSTR("PNG"))
        {
            options.format = PATHSTR("png");
        }
        else if (ext == PATHSTR("webp") || ext == PATHSTR("WEBP"))
        {
            options.format = PATHSTR("webp");
        }
        else if (ext == PATHSTR("jpg") || ext == PATHSTR("JPG") || ext == PATHSTR("jpeg") || ext == PATHSTR("JPEG"))
        {
            options.format = PATHSTR("jpg");
        }
        else
        {
            fprintf(stderr, "🚨 Error: Invalid output path extension or type!\n");
            return -1;
        }
    }

    if (options.format != PATHSTR("png") && options.format != PATHSTR("webp") && options.format != PATHSTR("jpg"))
    {
        fprintf(stderr, "🚨 Error: Invalid format provided!\n");
        return -1;
    }

    return 0;
}
