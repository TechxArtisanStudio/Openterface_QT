#include <iostream>
#include <cstdlib>

// Test FFmpeg functionality
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavdevice/avdevice.h>
}

int main() {
    std::cout << "Testing FFmpeg functionality..." << std::endl;
    
    // Initialize FFmpeg
    av_log_set_level(AV_LOG_INFO);
    avdevice_register_all();
    
    std::cout << "FFmpeg version: " << av_version_info() << std::endl;
    
    // Test finding V4L2 input format
    const AVInputFormat* inputFormat = av_find_input_format("v4l2");
    if (inputFormat) {
        std::cout << "V4L2 input format found: " << inputFormat->name << std::endl;
    } else {
        std::cout << "ERROR: V4L2 input format not found!" << std::endl;
        return 1;
    }
    
    // Test MJPEG decoder
    const AVCodec* mjpegDecoder = avcodec_find_decoder(AV_CODEC_ID_MJPEG);
    if (mjpegDecoder) {
        std::cout << "MJPEG decoder found: " << mjpegDecoder->name << std::endl;
    } else {
        std::cout << "ERROR: MJPEG decoder not found!" << std::endl;
        return 1;
    }
    
    // List video devices
    std::cout << "Testing video device access..." << std::endl;
    
    // Try to open /dev/video0
    AVFormatContext* formatContext = avformat_alloc_context();
    if (formatContext) {
        AVDictionary* options = nullptr;
        av_dict_set(&options, "video_size", "640x480", 0);
        av_dict_set(&options, "framerate", "15", 0);
        
        int ret = avformat_open_input(&formatContext, "/dev/video0", inputFormat, &options);
        av_dict_free(&options);
        
        if (ret == 0) {
            std::cout << "Successfully opened /dev/video0" << std::endl;
            avformat_close_input(&formatContext);
        } else {
            char errbuf[AV_ERROR_MAX_STRING_SIZE];
            av_strerror(ret, errbuf, AV_ERROR_MAX_STRING_SIZE);
            std::cout << "Failed to open /dev/video0: " << errbuf << std::endl;
        }
        
        avformat_free_context(formatContext);
    }
    
    std::cout << "FFmpeg test completed." << std::endl;
    return 0;
}
