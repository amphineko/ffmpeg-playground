extern "C" {
#include <libavutil/audio_fifo.h>
}

#include "../utils/io.hpp"
#include "../utils/resample.hpp"

int read_decode_frame(AVFrame *frame, int stream_idx, AVCodecContext *decoder, AVFormatContext *fmt) {
    int ret;

    while ((ret = avcodec_receive_frame(decoder, frame)) != 0) {
        if (ret == AVERROR_EOF) {
            return AVERROR_EOF;
        }

        if (ret != AVERROR(EAGAIN)) {
            char msg[AV_ERROR_MAX_STRING_SIZE];
            std::cerr << "ERROR: Could not receive frame: "
                      << av_make_error_string(msg, AV_ERROR_MAX_STRING_SIZE, ret);
            return ret;
        }

        AVPacket *packet = av_packet_alloc();
        if ((ret = av_read_frame(fmt, packet)) < 0) {
            if (ret != AVERROR_EOF) {
                char msg[AV_ERROR_MAX_STRING_SIZE];
                std::cerr << "ERROR: Could not read frame: "
                          << av_make_error_string(msg, AV_ERROR_MAX_STRING_SIZE, ret) << std::endl;
            }
            av_packet_free(&packet);
            return ret;
        }

        if (packet->stream_index != stream_idx) {
            av_packet_free(&packet);
            continue;
        }

        if ((ret = avcodec_send_packet(decoder, packet)) < 0) {
            char msg[AV_ERROR_MAX_STRING_SIZE];
            std::cerr << "ERROR: Could not send packet: "
                      << av_make_error_string(msg, AV_ERROR_MAX_STRING_SIZE, ret) << std::endl;
            av_packet_free(&packet);
            return ret;
        }

        av_packet_free(&packet);
    }

    return 0;
}

bool convert_samples(const AVFrame *in_frame, AVCodecContext *encoder, SwrContext *swr) {
    int ret;

    if ((ret = swr_convert_frame(swr, nullptr, in_frame)) < 0) {
        char msg[AV_ERROR_MAX_STRING_SIZE];
        std::cerr << "ERROR: Could not convert frame: "
                  << av_make_error_string(msg, AV_ERROR_MAX_STRING_SIZE, ret) << std::endl;
        return false;
    }

    return true;
}

int encode_samples(long &pts, AVCodecContext *encoder, SwrContext *swr) {
    int ret;

    // dequeue samples of a frame

    if (swr_get_delay(swr, encoder->frame_size) < encoder->frame_size) {
        return AVERROR(EAGAIN);
    }

    AVFrame *frame = av_frame_alloc();
    frame->channel_layout = encoder->channel_layout;
    frame->format = encoder->sample_fmt;
    frame->nb_samples = encoder->frame_size;
    if ((ret = av_frame_get_buffer(frame, 0)) < 0) {
        char msg[AV_ERROR_MAX_STRING_SIZE];
        std::cerr << "ERROR: Could not allocate frame buffer: "
                  << av_make_error_string(msg, AV_ERROR_MAX_STRING_SIZE, ret) << std::endl;
        av_frame_free(&frame);
        return ret;
    }

    if ((ret = swr_convert(swr, frame->extended_data, encoder->frame_size, nullptr, 0)) < 0) {
        char msg[AV_ERROR_MAX_STRING_SIZE];
        std::cerr << "ERROR: Could not dequeue frame samples: "
                  << av_make_error_string(msg, AV_ERROR_MAX_STRING_SIZE, ret) << std::endl;
        av_frame_free(&frame);
        return ret;
    }

    // encode frame

    frame->pts = pts;
    pts += frame->nb_samples;

    if ((ret = avcodec_send_frame(encoder, frame)) < 0) {
        char msg[AV_ERROR_MAX_STRING_SIZE];
        std::cerr << "ERROR: Could not send frame: "
                  << av_make_error_string(msg, AV_ERROR_MAX_STRING_SIZE, ret) << std::endl;
        av_frame_free(&frame);
        return ret;
    }

    av_frame_free(&frame);
    return 0;
}

int write_frame(int stream_idx, AVCodecContext *encoder, AVFormatContext *fmt) {
    int ret;

    AVPacket *packet = av_packet_alloc();
    while ((ret = avcodec_receive_packet(encoder, packet)) == 0) {
        packet->stream_index = stream_idx;

        if ((ret = av_interleaved_write_frame(fmt, packet)) < 0) {
            char msg[AV_ERROR_MAX_STRING_SIZE];
            std::cerr << "ERROR: Could not write frame: "
                      << av_make_error_string(msg, AV_ERROR_MAX_STRING_SIZE, ret) << std::endl;
            av_packet_free(&packet);
            return ret;
        }
    }
    av_packet_free(&packet);

    if (ret != AVERROR(EAGAIN) && ret != AVERROR_EOF) {
        char msg[AV_ERROR_MAX_STRING_SIZE];
        std::cerr << "ERROR: Could not receive packet: "
                  << av_make_error_string(msg, AV_ERROR_MAX_STRING_SIZE, ret) << std::endl;
        return ret;
    }

    return ret;
}

int encode_write_frame(long &pts,
                       int &out_frame_count,
                       int stream_idx,
                       AVCodecContext *encoder,
                       SwrContext *swr,
                       AVFormatContext *fmt) {
    int ret;

    if ((ret = encode_samples(pts, encoder, swr)) < 0) {
        if (ret != AVERROR(EAGAIN)) {
            char msg[AV_ERROR_MAX_STRING_SIZE];
            std::cerr << "ERROR: Could not encode samples: "
                      << av_make_error_string(msg, AV_ERROR_MAX_STRING_SIZE, ret);
        }
        return ret;
    }

    out_frame_count++;

    if ((ret = write_frame(stream_idx, encoder, fmt)) < 0) {
        if (ret != AVERROR(EAGAIN)) {
            char msg[AV_ERROR_MAX_STRING_SIZE];
            std::cerr << "ERROR: Could not write frame: "
                      << av_make_error_string(msg, AV_ERROR_MAX_STRING_SIZE, ret);
        }
        return ret;
    }

    return 0;
}

int main(int argc, char *argv[]) {
    int ret;
    int in_stream_idx = -1, out_stream_idx = -1;
    AVCodecContext *encoder = nullptr, *decoder = nullptr;
    AVFormatContext *out_fmt = nullptr, *in_fmt = nullptr;
    AVIOContext *out_io = nullptr;
    SwrContext *swr = nullptr;

    std::string in_filename, out_filename, output_codec_name;
    long output_sample_rate = 0;

    // parse command line arguments

    if (argc < 5) {
        std::cerr << "Usage: " << argv[0] << " <input file> <output file> <codec> <sample rate>" << std::endl;
        return 1;
    }

    in_filename = argv[1];
    out_filename = argv[2];
    output_codec_name = argv[3];
    output_sample_rate = strtol(argv[4], nullptr, 10);

    // open input

    if (!open_input(in_filename, in_stream_idx, decoder, in_fmt)) {
        close_input(decoder, in_fmt);
        return 1;
    }
    std::cout << "INFO: Input file opened, decoder " << decoder->codec->name << std::endl;

    // open output

    const AVCodec *encoder_codec = avcodec_find_encoder_by_name(output_codec_name.c_str());
    if (!encoder_codec) {
        std::cerr << "ERROR: Could not find encoder " << output_codec_name << std::endl;
        close_input(decoder, in_fmt);
        return 1;
    }

    {
        size_t i;
        for (i = 0; encoder_codec->supported_samplerates[i] != AV_SAMPLE_FMT_NONE; i++) {
            if (encoder_codec->supported_samplerates[i] == output_sample_rate) {
                break;
            }
        }

        if (encoder_codec->supported_samplerates[i] == AV_SAMPLE_FMT_NONE) {
            std::cerr << "ERROR: Encoder " << encoder_codec->name << " does not support sample rate "
                      << output_sample_rate
                      << std::endl;
        }
    }

    AVCodecParameters *encoder_params = avcodec_parameters_alloc();
    encoder_params->bit_rate = 128000;
    encoder_params->channels = decoder->channels;
    encoder_params->channel_layout = decoder->channel_layout;
    encoder_params->format = encoder_codec->sample_fmts[0];
    encoder_params->sample_rate = 48000;

    if (!open_output(out_filename, encoder_params, encoder_codec, out_stream_idx, decoder, encoder, out_fmt, out_io)) {
        close_output(encoder, out_fmt, out_io);
        close_input(decoder, in_fmt);
        return 1;
    }
    std::cout << "INFO: Output file opened, encoder " << encoder->codec->name << std::endl;

    // open swr

    if (!open_resample(swr, encoder, decoder)) {
        close_resample(swr);
        close_output(encoder, out_fmt, out_io);
        close_input(decoder, in_fmt);
        return 1;
    }

    // transcode

    AVFrame *in_frame = av_frame_alloc();
    int in_frame_count = 0, out_frame_count = 0;
    long pts = 0;
    while (true) {
        if ((ret = read_decode_frame(in_frame, in_stream_idx, decoder, in_fmt)) < 0) {
            if (ret == AVERROR_EOF) {
                break;
            }

            char msg[AV_ERROR_MAX_STRING_SIZE];
            std::cerr << "ERROR: Could not decode frame: "
                      << av_make_error_string(msg, AV_ERROR_MAX_STRING_SIZE, ret);
            break;
        }

        in_frame_count++;

        convert_samples(in_frame, encoder, swr);

        encode_write_frame(pts, out_frame_count, out_stream_idx, encoder, swr, out_fmt);
    }
    av_frame_free(&in_frame);

    // flush swr

    while ((ret = encode_write_frame(pts, out_frame_count, out_stream_idx, encoder, swr, out_fmt)) == 0);
    if (ret != AVERROR(EAGAIN)) {
        char msg[AV_ERROR_MAX_STRING_SIZE];
        std::cerr << "ERROR: Could not flush encoder: "
                  << av_make_error_string(msg, AV_ERROR_MAX_STRING_SIZE, ret);

    }

    // flush encoder

    if ((ret = avcodec_send_frame(encoder, nullptr)) < 0) {
        char msg[AV_ERROR_MAX_STRING_SIZE];
        std::cerr << "ERROR: Could not flush encoder: "
                  << av_make_error_string(msg, AV_ERROR_MAX_STRING_SIZE, ret);
    }

    while ((ret = write_frame(out_stream_idx, encoder, out_fmt)) == 0);
    if (ret != AVERROR_EOF) {
        char msg[AV_ERROR_MAX_STRING_SIZE];
        std::cerr << "ERROR: Could not write buffered frames: "
                  << av_make_error_string(msg, AV_ERROR_MAX_STRING_SIZE, ret);
    }

    std::cout << "INFO: Decoded " << in_frame_count << " frames" << std::endl;
    std::cout << "INFO: Encoded " << out_frame_count << " frames" << std::endl;

    // close output

    if ((ret = av_write_trailer(out_fmt)) < 0) {
        char msg[AV_ERROR_MAX_STRING_SIZE];
        std::cerr << "ERROR: Could not write trailer: "
                  << av_make_error_string(msg, AV_ERROR_MAX_STRING_SIZE, ret) << std::endl;
    }

    close_resample(swr);
    close_output(encoder, out_fmt, out_io);
    close_input(decoder, in_fmt);

    return ret;
}