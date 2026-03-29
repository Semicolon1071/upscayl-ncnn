#include "pipeline.h"

#include <iostream>
#include <stdio.h>
#include <algorithm>
#include <vector>
#include <filesystem>

namespace fs = std::filesystem;

#if _WIN32
#include "wic_image.h"
#else // _WIN32
#include "stb_image.h"
#include "stb_image_write.h"
#endif // _WIN32
#include "webp_image.h"
#include "stb_image_resize2.h"

#include "image_utils.h"

// ncnn
#include "cpu.h"
#include "gpu.h"
#include "platform.h"
#include "realesrgan.h"

#include "filesystem_utils.h"

static constexpr int MAX_PATH_LENGTH = 256;
static constexpr int MIN_TILE_SIZE = 32;

// ---------------------------------------------------------------------------
// collect_file_paths: resolve input/output paths from CLI options
// ---------------------------------------------------------------------------

int collect_file_paths(const CliOptions &options,
                       std::vector<path_t> &input_files,
                       std::vector<path_t> &output_files)
{
    if (path_is_directory(options.inputpath) && path_is_directory(options.outputpath))
    {
        std::vector<path_t> filenames;
        int lr = list_directory(options.inputpath, filenames);
        if (lr != 0)
            return -1;

        const int count = filenames.size();
        input_files.resize(count);
        output_files.resize(count);

        path_t last_filename;
        path_t last_filename_noext;
        for (int i = 0; i < count; i++)
        {
            path_t filename = filenames[i];
            path_t filename_noext = get_file_name_without_extension(filename);
            path_t output_filename = filename_noext + PATHSTR('.') + options.format;

            // filename list is sorted, check if output image path conflicts
            if (filename_noext == last_filename_noext)
            {
                path_t output_filename2 = filename + PATHSTR('.') + options.format;
#if _WIN32
                fwprintf(stderr, L"⚠️ Warning: both %s and %s output %s! %s will output %s\n", filename.c_str(), last_filename.c_str(), output_filename.c_str(), filename.c_str(), output_filename2.c_str());
#else
                fprintf(stderr, "⚠️ Warning: both %s and %s output %s! %s will output %s\n", filename.c_str(), last_filename.c_str(), output_filename.c_str(), filename.c_str(), output_filename2.c_str());
#endif
                output_filename = output_filename2;
            }
            else
            {
                last_filename = filename;
                last_filename_noext = filename_noext;
            }

            input_files[i] = options.inputpath + PATHSTR('/') + filename;
            output_files[i] = options.outputpath + PATHSTR('/') + output_filename;
        }
    }
    else if (!path_is_directory(options.inputpath) && !path_is_directory(options.outputpath))
    {
        input_files.push_back(options.inputpath);
        output_files.push_back(options.outputpath);
    }
    else
    {
        fprintf(stderr, "🚨 Error: Input path and Output path both must be either a file or a directory!\n");
        return -1;
    }

    return 0;
}

// ---------------------------------------------------------------------------
// Thread parameter structs
// ---------------------------------------------------------------------------

struct LoadThreadParams
{
    int scale;
    int jobs_load;
    std::vector<path_t> input_files;
    std::vector<path_t> output_files;
    TaskQueue *toproc;
};

struct ProcThreadParams
{
    const RealESRGAN *realesrgan;
    TaskQueue *toproc;
    TaskQueue *tosave;
};

struct SaveThreadParams
{
    int resizeWidth;
    int resizeHeight;
    int resizeMode;
    bool resizeProvided;
    int outputScale;
    bool hasOutputScale;
    bool hasCustomWidth;
    float compression;
    int verbose;
    TaskQueue *tosave;
};

// ---------------------------------------------------------------------------
// Load thread: reads images from disk, decodes, puts into toproc queue
// ---------------------------------------------------------------------------

static void *load_thread_func(void *args)
{
    const LoadThreadParams *ltp = (const LoadThreadParams *)args;
    const int count = ltp->input_files.size();
    const int scale = ltp->scale;

#pragma omp parallel for schedule(static, 1) num_threads(ltp->jobs_load)
    for (int i = 0; i < count; i++)
    {
        const path_t &imagepath = ltp->input_files[i];

        int webp = 0;

        unsigned char *pixeldata = 0;
        int w;
        int h;
        int c;

#if _WIN32
        FILE *fp = _wfopen(imagepath.c_str(), L"rb");
#else
        FILE *fp = fopen(imagepath.c_str(), "rb");
#endif
        if (fp)
        {
            // read whole file
            unsigned char *filedata = 0;
            long length = 0;
            {
                fseek(fp, 0, SEEK_END);
                length = ftell(fp);
                rewind(fp);
                if (length <= 0)
                {
                    fclose(fp);
                    continue;
                }
                filedata = (unsigned char *)malloc(length);
                if (filedata)
                {
                    size_t bytes_read = fread(filedata, 1, length, fp);
                    if ((long)bytes_read != length)
                    {
                        free(filedata);
                        filedata = 0;
                    }
                }
                fclose(fp);
            }

            if (filedata)
            {
                pixeldata = webp_load(filedata, length, &w, &h, &c);
                if (pixeldata)
                {
                    webp = 1;
                }
                else
                {
                    // not webp, try jpg png etc.
#if _WIN32
                    pixeldata = wic_decode_image(imagepath.c_str(), &w, &h, &c);
                    if (pixeldata)
                    {
                        // WIC channel conversion logic similar to stb_image
                        if (c == 1)
                        {
                            // grayscale -> rgb
                            unsigned char *rgbdata = (unsigned char *)malloc(w * h * 3);
                            if (rgbdata)
                            {
                                for (int i = 0; i < w * h; i++)
                                {
                                    unsigned char gray = pixeldata[i];
                                    rgbdata[i * 3 + 0] = gray; // B
                                    rgbdata[i * 3 + 1] = gray; // G
                                    rgbdata[i * 3 + 2] = gray; // R
                                }
                                free(pixeldata);
                                pixeldata = rgbdata;
                                c = 3;
                            }
                        }
                        else if (c == 2)
                        {
                            // grayscale + alpha -> rgba
                            unsigned char *rgbadata = (unsigned char *)malloc(w * h * 4);
                            if (rgbadata)
                            {
                                for (int i = 0; i < w * h; i++)
                                {
                                    unsigned char gray = pixeldata[i * 2];
                                    unsigned char alpha = pixeldata[i * 2 + 1];
                                    rgbadata[i * 4 + 0] = gray;  // B
                                    rgbadata[i * 4 + 1] = gray;  // G
                                    rgbadata[i * 4 + 2] = gray;  // R
                                    rgbadata[i * 4 + 3] = alpha; // A
                                }
                                free(pixeldata);
                                pixeldata = rgbadata;
                                c = 4;
                            }
                        }
                    }
#else  // _WIN32
                    pixeldata = stbi_load_from_memory(filedata, length, &w, &h, &c, 0);
                    if (pixeldata)
                    {
                        // stb_image auto channel
                        if (c == 1)
                        {
                            // grayscale -> rgb
                            stbi_image_free(pixeldata);
                            pixeldata = stbi_load_from_memory(filedata, length, &w, &h, &c, 3);
                            c = 3;
                        }
                        else if (c == 2)
                        {
                            // grayscale + alpha -> rgba
                            stbi_image_free(pixeldata);
                            pixeldata = stbi_load_from_memory(filedata, length, &w, &h, &c, 4);
                            c = 4;
                        }
                    }
#endif // _WIN32
                }

                free(filedata);
            }
        }
        if (pixeldata)
        {
            Task v;
            v.id = i;
            v.inpath = imagepath;
            v.outpath = ltp->output_files[i];
            // outimage is ncnn-managed initially; owned_output stays empty

            v.inimage = ncnn::Mat(w, h, (void *)pixeldata, (size_t)c, c);
            v.outimage = ncnn::Mat(w * scale, h * scale, (size_t)c, c);

            path_t ext = get_file_extension(v.outpath);
            if (c == 4 && (ext == PATHSTR("jpg") || ext == PATHSTR("JPG") || ext == PATHSTR("jpeg") || ext == PATHSTR("JPEG")))
            {
                path_t output_filename2 = get_file_name_without_extension(ltp->output_files[i]) + PATHSTR('.') + ext;
                v.outpath = output_filename2;
#if _WIN32
                fwprintf(stderr, L"ℹ️ Info: Image %s has alpha channel! Converting to RGB for JPEG output.\n", imagepath.c_str());
#else  // _WIN32
                fprintf(stderr, "ℹ️ Info: Image %s has alpha channel! Converting to RGB for JPEG output.\n", imagepath.c_str());
#endif // _WIN32
            }

            ltp->toproc->put(v);
        }
        else
        {
#if _WIN32
            fwprintf(stderr, L"🚨 Error: Couldn't read the image '%s'! (channels: %d)\n", imagepath.c_str(), c);
#else  // _WIN32
            fprintf(stderr, "🚨 Error: Couldn't read the image '%s'! (channels: %d)\n", imagepath.c_str(), c);
#endif // _WIN32
        }
    }

    return 0;
}

// ---------------------------------------------------------------------------
// Process thread: takes images from toproc, runs RealESRGAN, puts into tosave
// ---------------------------------------------------------------------------

static void *proc_thread_func(void *args)
{
    const ProcThreadParams *ptp = (const ProcThreadParams *)args;
    const RealESRGAN *realesrgan = ptp->realesrgan;

    for (;;)
    {
        Task v;

        ptp->toproc->get(v);

        if (v.id == TASK_SENTINEL_ID)
            break;

        int ret = realesrgan->process(v.inimage, v.outimage);
        if (ret != 0)
        {
            fprintf(stderr, "🚨 Error: Failed to process image (id=%d)\n", v.id);
            continue;
        }

        ptp->tosave->put(v);
    }

    return 0;
}

// ---------------------------------------------------------------------------
// Save helpers: resize and scale output images
// ---------------------------------------------------------------------------

static void resize_output_image(Task &v, const SaveThreadParams *stp)
{
    const int resizeWidth = stp->resizeWidth;
    int resizeHeight = stp->resizeHeight;
    const bool resizeProvided = stp->resizeProvided;
    const bool hasCustomWidth = stp->hasCustomWidth;

    if ((!resizeProvided && !hasCustomWidth) ||
        (v.outimage.w == resizeWidth && v.outimage.h == resizeHeight) || (!resizeHeight && hasCustomWidth && v.outimage.w == resizeWidth))
    {
#if _WIN32
        fwprintf(stderr, L"⏩ Skipping resize\n");
#else  // _WIN32
        fprintf(stderr, "⏩ Skipping resize\n");
#endif // _WIN32
        return;
    }

    // Calculate the resize height if not provided
    if (hasCustomWidth)
    {
        resizeHeight = calculate_proportional_height(v.inimage.w, v.inimage.h, resizeWidth);
#if _WIN32
        fwprintf(stderr, L"🧮 Calculated height from width: %d\n", resizeHeight);
#else  // _WIN32
        fprintf(stderr, "🧮 Calculated height from width: %d\n", resizeHeight);
#endif // _WIN32
    }

#if _WIN32
    fwprintf(stderr, L"🏞️ Resizing image according to desired resolution\n");
#else  // _WIN32
    fprintf(stderr, "🏞️ Resizing image according to desired resolution\n");
#endif // _WIN32

    int c = v.outimage.elempack;

    stbir_pixel_layout layout = (c == 4) ? STBIR_RGBA : STBIR_RGB;

    // Create a new buffer for the resized image
    unsigned char *resizedData = (unsigned char *)malloc((size_t)resizeWidth * resizeHeight * c);
    if (!resizedData)
    {
        fprintf(stderr, "🚨 Error: Failed to allocate memory for resized image (%dx%d)\n", resizeWidth, resizeHeight);
        return;
    }

    // Resize the image using stb_image_resize
    stbir_resize_uint8_srgb((unsigned char *)v.outimage.data, v.outimage.w, v.outimage.h, 0, resizedData, resizeWidth, resizeHeight, 0, layout);

    // RAII: owned_output frees the old buffer (if any) and takes ownership of the new one
    v.owned_output.reset(resizedData);
    v.outimage = ncnn::Mat(resizeWidth, resizeHeight, resizedData, (size_t)c, c);

#if _WIN32
    fwprintf(stderr, L"🏞️ Resized image from %dx%d to %dx%d\n", v.inimage.w, v.inimage.h, v.outimage.w, v.outimage.h);
#else  // _WIN32
    fprintf(stderr, "🏞️ Resized image from %dx%d to %dx%d\n", v.inimage.w, v.inimage.h, v.outimage.w, v.outimage.h);
#endif // _WIN32
}

static void scale_output_image(Task &v, const SaveThreadParams *stp)
{
    const int originalWidth = v.inimage.w;
    const int originalHeight = v.inimage.h;
    const bool hasOutputScale = stp->hasOutputScale;
    const int outputScale = stp->outputScale;
    const int outputWidth = originalWidth * outputScale;
    const int outputHeight = originalHeight * outputScale;
    const bool resizeProvided = stp->resizeProvided;
    const bool hasCustomWidth = stp->hasCustomWidth;

    if (!hasOutputScale || resizeProvided || hasCustomWidth)
        return;

    int c = v.outimage.elempack;

#if _WIN32
    fwprintf(stderr, L"🏞️ Resizing image according to output scale\n");
#else  // _WIN32
    fprintf(stderr, "🏞️ Resizing image according to output scale\n");
#endif // _WIN32

    stbir_pixel_layout layout = (c == 4) ? STBIR_RGBA : STBIR_RGB;
    // Create a new buffer for the resized image
    unsigned char *resizedData = (unsigned char *)malloc((size_t)outputWidth * outputHeight * c);
    if (!resizedData)
    {
        fprintf(stderr, "🚨 Error: Failed to allocate memory for scaled image (%dx%d)\n", outputWidth, outputHeight);
        return;
    }
    stbir_resize_uint8_srgb((unsigned char *)v.outimage.data, v.outimage.w, v.outimage.h, 0, resizedData, outputWidth, outputHeight, 0, layout);

    // RAII: owned_output frees the old buffer (if any) and takes ownership of the new one
    v.owned_output.reset(resizedData);
    v.outimage = ncnn::Mat(outputWidth, outputHeight, resizedData, (size_t)v.outimage.elempack, v.outimage.elempack);

#if _WIN32
    fwprintf(stderr, L"🏞️ Resized image from %dx%d to %dx%d\n", originalWidth, originalHeight, outputWidth, outputHeight);
#else  // _WIN32
    fprintf(stderr, "🏞️ Scaled image from %dx%d to %dx%d\n", originalWidth, originalHeight, outputWidth, outputHeight);
#endif // _WIN32
}

// ---------------------------------------------------------------------------
// Save thread: takes processed images from tosave, encodes and writes to disk
// ---------------------------------------------------------------------------

static void *save_thread_func(void *args)
{
    const SaveThreadParams *stp = (const SaveThreadParams *)args;
    const int verbose = stp->verbose;

    for (;;)
    {
        Task v;

        stp->tosave->get(v);

        if (v.id == TASK_SENTINEL_ID)
            break;

        // free input pixel data
        {
            unsigned char *pixeldata = (unsigned char *)v.inimage.data;
            if (v.webp == 1)
            {
                free(pixeldata);
            }
            else
            {
#if _WIN32
                free(pixeldata);
#else
                stbi_image_free(pixeldata);
#endif
            }
        }

        if (stp->hasOutputScale)
        {
            scale_output_image(v, stp);
        }

        if ((stp->resizeProvided || stp->hasCustomWidth) && !stp->hasOutputScale)
        {
            resize_output_image(v, stp);
        }

        int success = 0;

        path_t ext = get_file_extension(v.outpath);

        /* ----------- Create folder if not exists -------------------*/
        fs::path fs_path = fs::absolute(v.outpath);
#if _WIN32
        std::wstring parent_path = fs_path.parent_path().wstring();
#else
        std::string parent_path = fs_path.parent_path().string();
#endif

        if (!fs::exists(parent_path))
        {
#if _WIN32
            fwprintf(stderr, L"📂 Creating directory: %ls\n", parent_path.c_str());
#else
            fprintf(stderr, "📂 Creating directory: %s\n", parent_path.c_str());
#endif
            std::error_code ec;
            fs::create_directories(parent_path, ec);
            if (ec)
            {
                fprintf(stderr, "🚨 Error: Failed to create directory: %s\n", ec.message().c_str());
            }
        }

        if (ext == PATHSTR("webp") || ext == PATHSTR("WEBP"))
        {
            success = webp_save(v.outpath.c_str(), v.outimage.w, v.outimage.h, v.outimage.elempack, (const unsigned char *)v.outimage.data, 100 - (int)stp->compression);
        }
        else if (ext == PATHSTR("png") || ext == PATHSTR("PNG"))
        {
#if _WIN32
            success = wic_encode_image(v.outpath.c_str(), v.outimage.w, v.outimage.h, v.outimage.elempack, v.outimage.data);
#else
            // if compression is more than 0 make stbi_write_png_compression_level = 9
            if (stp->compression > 0)
            {
                stbi_write_png_compression_level = stp->compression;
            }
            else
            {
                stbi_write_png_compression_level = 9;
            }
            success = stbi_write_png(v.outpath.c_str(), v.outimage.w, v.outimage.h, v.outimage.elempack, v.outimage.data, 0);
#endif
        }
        else if (ext == PATHSTR("jpg") || ext == PATHSTR("JPG") || ext == PATHSTR("jpeg") || ext == PATHSTR("JPEG"))
        {
#if _WIN32
            if (verbose)
            {
                fwprintf(stderr, L"🔧 Debug: Saving JPEG with %d channels, size %dx%d\n", v.outimage.elempack, v.outimage.w, v.outimage.h);
            }
            success = wic_encode_jpeg_image(v.outpath.c_str(), v.outimage.w, v.outimage.h, v.outimage.elempack, v.outimage.data);
#else
            success = stbi_write_jpg(v.outpath.c_str(), v.outimage.w, v.outimage.h, v.outimage.elempack, v.outimage.data, 100 - (int)stp->compression);
#endif
        }
        if (success)
        {
            fprintf(stderr, "100.00%%\n");
            fprintf(stderr, "\n🙌 Upscayled Successfully!\n");

            if (verbose)
            {
#if _WIN32
                fwprintf(stderr, L"✅ %ls -> %ls done\n", v.inpath.c_str(), v.outpath.c_str());
#else
                fprintf(stderr, "✅ %s -> %s done\n", v.inpath.c_str(), v.outpath.c_str());
#endif
            }
        }
        else
        {
#if _WIN32
            fwprintf(stderr, L"🚨 Error: Couldn't write the image %s\n", v.outpath.c_str());
#else
            fprintf(stderr, "🚨 Error: Couldn't write the image %s\n", v.outpath.c_str());
#endif
        }

        // owned_output (if set) is freed automatically when v goes out of scope
    }

    return 0;
}

// ---------------------------------------------------------------------------
// UpscalePipeline::run — GPU setup, model loading, thread orchestration
// ---------------------------------------------------------------------------

int UpscalePipeline::run(CliOptions &options,
                         const std::vector<path_t> &input_files,
                         const std::vector<path_t> &output_files)
{
    int prepadding = 0;

    if (options.model.find(PATHSTR("models")) != path_t::npos || options.model.find(PATHSTR("models2")) != path_t::npos)
    {
        prepadding = 10;
    }
    else
    {
        fprintf(stderr, "🚨 Error: Unknown model dir type. Make sure that the model directory is called 'models' with *.param and *.bin files inside it.\n");
        return -1;
    }

#if _WIN32
    wchar_t parampath[MAX_PATH_LENGTH];
    wchar_t modelpath[MAX_PATH_LENGTH];

    // Detect scale from model name (convert wstring to string for detection)
    {
        std::string narrow_modelname(options.modelname.begin(), options.modelname.end());
        int detected_scale = detect_scale_from_model_name(narrow_modelname);
        if (detected_scale != 0)
        {
            options.scale = detected_scale;
            fwprintf(stderr, L"✨ Detected scale x%d\n", options.scale);
        }

        if (options.scale == 4)
        {
            fwprintf(stderr, L"✨ Using the default scale x4\n");
        }
    }

    if (options.modelname == PATHSTR("realesr-animevideov3"))
    {
        swprintf(parampath, MAX_PATH_LENGTH, L"%s/%s-x%s.param", options.model.c_str(), options.modelname.c_str(), std::to_wstring(options.scale).c_str());
        swprintf(modelpath, MAX_PATH_LENGTH, L"%s/%s-x%s.bin", options.model.c_str(), options.modelname.c_str(), std::to_wstring(options.scale).c_str());
    }
    else
    {
        swprintf(parampath, MAX_PATH_LENGTH, L"%s/%s.param", options.model.c_str(), options.modelname.c_str());
        swprintf(modelpath, MAX_PATH_LENGTH, L"%s/%s.bin", options.model.c_str(), options.modelname.c_str());
    }

#else
    char parampath[MAX_PATH_LENGTH];
    char modelpath[MAX_PATH_LENGTH];

    // Check if modelname includes scale
    int detected_scale = detect_scale_from_model_name(options.modelname);
    if (detected_scale != 0)
    {
        options.scale = detected_scale;
        fprintf(stderr, "✨ Detected scale x%d\n", options.scale);
    }

    if (options.scale == 4)
    {
        fprintf(stderr, "✨ Using the default scale x4\n");
    }

    if (options.modelname == PATHSTR("realesr-animevideov3"))
    {
        snprintf(parampath, sizeof(parampath), "%s/%s-x%s.param", options.model.c_str(), options.modelname.c_str(), std::to_string(options.scale).c_str());
        snprintf(modelpath, sizeof(modelpath), "%s/%s-x%s.bin", options.model.c_str(), options.modelname.c_str(), std::to_string(options.scale).c_str());
    }
    else
    {
        snprintf(parampath, sizeof(parampath), "%s/%s.param", options.model.c_str(), options.modelname.c_str());
        snprintf(modelpath, sizeof(modelpath), "%s/%s.bin", options.model.c_str(), options.modelname.c_str());
    }
#endif

    path_t paramfullpath = sanitize_filepath(parampath);
    path_t modelfullpath = sanitize_filepath(modelpath);

#if _WIN32
    CoInitializeEx(NULL, COINIT_MULTITHREADED);
#endif

    ncnn::create_gpu_instance();

    if (options.gpuid.empty())
    {
        options.gpuid.push_back(ncnn::get_default_gpu_index());
    }

    const int use_gpu_count = (int)options.gpuid.size();

    if (options.jobs_proc.empty())
    {
        options.jobs_proc.resize(use_gpu_count, 2);
    }

    if (options.tilesize.empty())
    {
        options.tilesize.resize(use_gpu_count, 0);
    }

    int cpu_count = std::max(1, ncnn::get_cpu_count());
    options.jobs_load = std::min(options.jobs_load, cpu_count);
    options.jobs_save = std::min(options.jobs_save, cpu_count);

    int gpu_count = ncnn::get_gpu_count();
    for (int i = 0; i < use_gpu_count; i++)
    {
        if (options.gpuid[i] < 0 || options.gpuid[i] >= gpu_count)
        {
            fprintf(stderr, "🚨 Error: Invalid GPU Device\n");

            ncnn::destroy_gpu_instance();
            return -1;
        }
    }

    int total_jobs_proc = 0;
    for (int i = 0; i < use_gpu_count; i++)
    {
        int gpu_queue_count = ncnn::get_gpu_info(options.gpuid[i]).compute_queue_count();
        options.jobs_proc[i] = std::min(options.jobs_proc[i], gpu_queue_count);
        total_jobs_proc += options.jobs_proc[i];
    }

    for (int i = 0; i < use_gpu_count; i++)
    {
        if (options.tilesize[i] != 0)
            continue;

        uint32_t heap_budget = ncnn::get_gpu_device(options.gpuid[i])->get_heap_budget();

        // more fine-grained tilesize policy here
        if (options.model.find(PATHSTR("models")) != path_t::npos)
        {
            if (heap_budget > 1900)
                options.tilesize[i] = 200;
            else if (heap_budget > 550)
                options.tilesize[i] = 100;
            else if (heap_budget > 190)
                options.tilesize[i] = 64;
            else
                options.tilesize[i] = MIN_TILE_SIZE;
        }
    }

    {
        std::vector<RealESRGAN *> realesrgan(use_gpu_count);

        for (int i = 0; i < use_gpu_count; i++)
        {
            realesrgan[i] = new RealESRGAN(options.gpuid[i], options.tta_mode);

            int load_ret = realesrgan[i]->load(paramfullpath, modelfullpath);
            if (load_ret != 0)
            {
                fprintf(stderr, "🚨 Error: Failed to load model on GPU %d\n", options.gpuid[i]);
                // cleanup already-created instances
                for (int j = 0; j <= i; j++)
                    delete realesrgan[j];
                ncnn::destroy_gpu_instance();
                return -1;
            }

            realesrgan[i]->scale = options.scale;
            realesrgan[i]->tilesize = options.tilesize[i];
            realesrgan[i]->prepadding = prepadding;
        }

        // main routine
        {
            // load image
            LoadThreadParams ltp;
            ltp.scale = options.scale;
            ltp.jobs_load = options.jobs_load;
            ltp.input_files = input_files;
            ltp.output_files = output_files;
            ltp.toproc = &toproc_;

            ncnn::Thread load_thread(load_thread_func, (void *)&ltp);

            // realesrgan proc
            std::vector<ProcThreadParams> ptp(use_gpu_count);
            for (int i = 0; i < use_gpu_count; i++)
            {
                ptp[i].realesrgan = realesrgan[i];
                ptp[i].toproc = &toproc_;
                ptp[i].tosave = &tosave_;
            }

            std::vector<ncnn::Thread *> proc_threads(total_jobs_proc);
            {
                int total_jobs_proc_id = 0;
                for (int i = 0; i < use_gpu_count; i++)
                {
                    for (int j = 0; j < options.jobs_proc[i]; j++)
                    {
                        proc_threads[total_jobs_proc_id++] = new ncnn::Thread(proc_thread_func, (void *)&ptp[i]);
                    }
                }
            }

            // save image
            SaveThreadParams stp;
            stp.resizeWidth = options.resizeWidth;
            stp.resizeHeight = options.resizeHeight;
            stp.resizeMode = options.resizeMode;
            stp.resizeProvided = options.resizeProvided;
            stp.verbose = options.verbose;
            stp.compression = options.compression;
            stp.outputScale = options.outputScale;
            stp.hasOutputScale = options.hasOutputScale;
            stp.hasCustomWidth = options.hasCustomWidth;
            stp.tosave = &tosave_;

            std::vector<ncnn::Thread *> save_threads(options.jobs_save);
            for (int i = 0; i < options.jobs_save; i++)
            {
                save_threads[i] = new ncnn::Thread(save_thread_func, (void *)&stp);
            }

            // end
            load_thread.join();

            Task end;
            end.id = TASK_SENTINEL_ID;

            for (int i = 0; i < total_jobs_proc; i++)
            {
                toproc_.put(end);
            }

            for (int i = 0; i < total_jobs_proc; i++)
            {
                proc_threads[i]->join();
                delete proc_threads[i];
            }

            for (int i = 0; i < options.jobs_save; i++)
            {
                tosave_.put(end);
            }

            for (int i = 0; i < options.jobs_save; i++)
            {
                save_threads[i]->join();
                delete save_threads[i];
            }
        }

        for (int i = 0; i < use_gpu_count; i++)
        {
            delete realesrgan[i];
        }
        realesrgan.clear();
    }

    ncnn::destroy_gpu_instance();

    return 0;
}
