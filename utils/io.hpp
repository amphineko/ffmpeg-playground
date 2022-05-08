#ifndef IO_H_
#define IO_H_

#include <iostream>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

void close_input(AVCodecContext *&codec_ctx, AVFormatContext *&fmt_ctx) {
    if (codec_ctx) {
        avcodec_close(codec_ctx);
        codec_ctx = nullptr;
    }

    if (fmt_ctx) {
        avformat_close_input(&fmt_ctx);
        fmt_ctx = nullptr;
    }
}

void close_output(AVCodecContext *&codec_ctx, AVFormatContext *&fmt_ctx, AVIOContext *&io_ctx) {
    if (codec_ctx) {
        avcodec_close(codec_ctx);
        codec_ctx = nullptr;
    }

    if (fmt_ctx) {
        avformat_free_context(fmt_ctx);
        fmt_ctx = nullptr;
    }

    if (io_ctx) {
        avio_close(io_ctx);
        io_ctx = nullptr;
    }
}

bool open_input(const std::string &filename,
                int &audio_stream_idx,
                AVCodecContext *&codec_ctx,
                AVFormatContext *&fmt_ctx) {
    int ret;

    if ((ret = avformat_open_input(&fmt_ctx, filename.c_str(), nullptr, nullptr)) < 0) {
        char msg[AV_ERROR_MAX_STRING_SIZE];
        std::cerr << "ERROR: Could not open input file " << filename << ": "
                  << av_make_error_string(msg, AV_ERROR_MAX_STRING_SIZE, ret) << std::endl;
        fmt_ctx = nullptr;
        return false;
    }

    if ((ret = avformat_find_stream_info(fmt_ctx, nullptr)) < 0) {
        char msg[AV_ERROR_MAX_STRING_SIZE];
        std::cerr << "ERROR: Could not find stream info: "
                  << av_make_error_string(msg, AV_ERROR_MAX_STRING_SIZE, ret) << std::endl;
        return false;
    }

    const AVCodec *codec;
    if ((ret = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_AUDIO, -1, -1, &codec, 0)) < 0) {
        char msg[AV_ERROR_MAX_STRING_SIZE];
        std::cerr << "ERROR: Could not find audio stream: "
                  << av_make_error_string(msg, AV_ERROR_MAX_STRING_SIZE, ret) << std::endl;
        return false;
    }
    audio_stream_idx = ret;

    codec_ctx = avcodec_alloc_context3(codec);
    if (!codec_ctx) {
        std::cerr << "ERROR: Could not allocate codec context" << std::endl;
        return false;
    }

    if ((ret = avcodec_parameters_to_context(codec_ctx, fmt_ctx->streams[audio_stream_idx]->codecpar)) < 0) {
        char msg[AV_ERROR_MAX_STRING_SIZE];
        std::cerr << "ERROR: Could not copy decoder parameters: "
                  << av_make_error_string(msg, AV_ERROR_MAX_STRING_SIZE, ret) << std::endl;
        return false;
    }

    if ((ret = avcodec_open2(codec_ctx, codec, nullptr)) < 0) {
        char msg[AV_ERROR_MAX_STRING_SIZE];
        std::cerr << "ERROR: Could not open decoder: "
                  << av_make_error_string(msg, AV_ERROR_MAX_STRING_SIZE, ret) << std::endl;
        return false;
    }

    return true;
}

bool open_output(const std::string &filename,
                 const AVCodecParameters *codec_par,
                 const AVCodec *encoder_codec,
                 int &out_stream_idx,
                 AVCodecContext *&decoder,
                 AVCodecContext *&encoder,
                 AVFormatContext *&fmt,
                 AVIOContext *&io) {
    int ret;

    // open output container

    if ((ret = avio_open2(&io, filename.c_str(), AVIO_FLAG_WRITE, nullptr, nullptr)) < 0) {
        char msg[AV_ERROR_MAX_STRING_SIZE];
        std::cerr << "ERROR: Could not open output file " << filename << ": "
                  << av_make_error_string(msg, AV_ERROR_MAX_STRING_SIZE, ret) << std::endl;
        return false;
    }

    if ((ret = avformat_alloc_output_context2(&fmt, nullptr, nullptr, filename.c_str())) < 0) {
        char msg[AV_ERROR_MAX_STRING_SIZE];
        std::cerr << "ERROR: Could not allocate output context: "
                  << av_make_error_string(msg, AV_ERROR_MAX_STRING_SIZE, ret) << std::endl;
        return false;
    }
    fmt->pb = io;

    // create output stream

    AVStream *stream = avformat_new_stream(fmt, nullptr);
    if (!stream) {
        std::cerr << "ERROR: Could not allocate stream" << std::endl;
        return false;
    }

    stream->time_base.den = codec_par->sample_rate;
    stream->time_base.num = 1;

    out_stream_idx = stream->index;

    // create encoder

    encoder = avcodec_alloc_context3(encoder_codec);
    if (!encoder) {
        std::cerr << "ERROR: Could not allocate codec context" << std::endl;
        return false;
    }

    encoder->bit_rate = codec_par->bit_rate;
    encoder->channels = codec_par->channels;
    encoder->channel_layout = codec_par->channel_layout;
    encoder->sample_fmt = static_cast<AVSampleFormat>(codec_par->format);
    encoder->sample_rate = codec_par->sample_rate;

    encoder->strict_std_compliance = FF_COMPLIANCE_EXPERIMENTAL;

    // enable global header

    if (fmt->oformat->flags & AVFMT_GLOBALHEADER) {
        encoder->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }

    // open encoder

    if ((ret = avcodec_open2(encoder, encoder_codec, nullptr)) < 0) {
        char msg[AV_ERROR_MAX_STRING_SIZE];
        std::cerr << "ERROR: Could not open encoder: "
                  << av_make_error_string(msg, AV_ERROR_MAX_STRING_SIZE, ret) << std::endl;
        return false;
    }

    // copy parameters to output stream

    if ((ret = avcodec_parameters_from_context(stream->codecpar, encoder)) < 0) {
        char msg[AV_ERROR_MAX_STRING_SIZE];
        std::cerr << "ERROR: Could not copy encoder parameters to stream: "
                  << av_make_error_string(msg, AV_ERROR_MAX_STRING_SIZE, ret) << std::endl;
        return false;
    }

    // write header

    if ((ret = avformat_write_header(fmt, nullptr)) < 0) {
        char msg[AV_ERROR_MAX_STRING_SIZE];
        std::cerr << "ERROR: Could not write header: "
                  << av_make_error_string(msg, AV_ERROR_MAX_STRING_SIZE, ret) << std::endl;
        return false;
    }

    return true;
}

#endif // IO_H_