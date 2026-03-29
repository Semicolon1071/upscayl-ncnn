// realesrgan implemented with ncnn library

#ifndef REALESRGAN_H
#define REALESRGAN_H

#include <string>

// ncnn
#include "net.h"
#include "gpu.h"
#include "layer.h"

class RealESRGAN
{
public:
    RealESRGAN(int gpuid, bool tta_mode = false);
    ~RealESRGAN();

#if _WIN32
    int load(const std::wstring &parampath, const std::wstring &modelpath);
#else
    int load(const std::string &parampath, const std::string &modelpath);
#endif

    int process(const ncnn::Mat &inimage, ncnn::Mat &outimage) const;

public:
    // realesrgan parameters
    int scale;
    int tilesize;
    int prepadding;

private:
    // Create a bicubic interpolation layer for the given scale factor.
    ncnn::Layer *create_bicubic_layer(float scale_factor);

    // Scale the alpha channel using the appropriate bicubic layer.
    void scale_alpha_channel(const ncnn::VkMat &in_alpha, ncnn::VkMat &out_alpha,
                             ncnn::VkCompute &cmd, const ncnn::Option &opt) const;

    ncnn::Net net;
    ncnn::Pipeline *realesrgan_preproc;
    ncnn::Pipeline *realesrgan_postproc;
    ncnn::Layer *bicubic_2x;
    ncnn::Layer *bicubic_3x;
    ncnn::Layer *bicubic_4x;
    bool tta_mode;
};

#endif // REALESRGAN_H
