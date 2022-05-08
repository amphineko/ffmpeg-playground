#ifndef RESAMPLE_H_
#define RESAMPLE_H_

#include <iostream>

extern "C" {
#include "libavcodec/avcodec.h"
#include "libswresample/swresample.h"
};

void close_resample(SwrContext *swr_ctx) {
    swr_free(&swr_ctx);
}

bool open_resample(SwrContext *&ctx, const AVCodecContext *encoder, const AVCodecContext *decoder) {
    int ret;

    ctx = swr_alloc_set_opts(nullptr,
                             encoder->channel_layout,
                             encoder->sample_fmt,
                             encoder->sample_rate,
                             decoder->channel_layout,
                             decoder->sample_fmt,
                             decoder->sample_rate,
                             0, nullptr);
    if (!ctx) {
        std::cerr << "ERROR: Failed to allocate resample context" << std::endl;
        return false;
    }

    if ((ret = swr_init(ctx)) < 0) {
        char msg[AV_ERROR_MAX_STRING_SIZE];
        std::cerr << "ERROR: Failed to initialize resample context: "
                  << av_make_error_string(msg, AV_ERROR_MAX_STRING_SIZE, ret) << std::endl;
        return false;
    }

    return true;
}

#endif // RESAMPLE_H_