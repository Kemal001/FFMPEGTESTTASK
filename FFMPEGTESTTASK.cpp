// FFMPEGTESTTASK.cpp : Defines the entry point for the console application.
//
#include "stdafx.h"
#include "targetver.h"

#include <stdio.h>
#include <tchar.h>
#include <map>
#include <list>
#include <queue>
#include <thread>
#include <mutex>
#include <chrono>
extern "C"{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
}
#include <SDL.h>
#include <SDL_thread.h>
#include "FfmpegPlayer.h"

const char* filePath1 = "tuborg.wmv";
const char* filePath2 = "part2.avi";
const char* filePath3 = "tuborg.wmv";

void audio_callback(void *userdata, Uint8 *stream, int len)
{
    SDL_memset(stream, 0, len);
    ((FfmpegPlayer*)userdata)->GetSound(stream, len);
}

class MainClass : public FfmpegPlayerListener
{
public:
    bool m_finished;
    bool m_rendererInitialized;
    uint8_t* buffer;
    int m_frameWidth;
    int m_frameHeight;
    int m_bufferSize;
    int m_x;
    int64_t m_duration;
    int m_y;
    int m_frameCounter;
    int seek_counter;
    std::thread m_thread;
    FfmpegPlayer *m_player;
    SDL_Window *screen;
    SDL_Renderer *renderer;
    SDL_Texture *sdlTexture;
    SDL_AudioSpec wanted_spec;
    SDL_AudioSpec spec;

    std::vector<char> buffer_f;
    bool m_paused;
    void Go(int x, int y, const char* filename)
    {
        m_paused = true;
        m_frameCounter = 0;
        seek_counter = 100;
        m_x = x;
        m_y = y;
        m_finished = false;
        m_rendererInitialized = false;
        buffer = NULL;
        m_player = new FfmpegPlayer();
        std::ifstream file(filename, std::ios::binary);
        file.seekg(0, std::ios::end);
        std::streamsize size = file.tellg();
        file.seekg(0, std::ios::beg);

        buffer_f = std::vector<char>(size);
        if (file.read(buffer_f.data(), size)){
            m_player->Initialize( (uint8_t*)buffer_f.data(), (int64_t)size, this);
        }
    }

    void seek(int time)
    {
        m_player->Seek(time);
    }

    void Stopped()
    {

    }
    void Paused()
    {
        m_paused = true;
    }
    void Playing()
    {
        m_paused = false;
        m_duration = m_player->GetDuration();
    }


    void Initialized()
    {
        int sample_rate = 0;
        int channels = 0;
        AVSampleFormat fmt;
        m_player->GetAudioParams(channels, sample_rate, fmt);
        int64_t bufferLength = 2048;
        int64_t bufferMillis = 2048 * 1000 / (2 * channels*sample_rate);
        m_player->Play(true);

        wanted_spec.freq = sample_rate;
        wanted_spec.format = AUDIO_F32;
        wanted_spec.channels = channels;
        wanted_spec.silence = 0;
        wanted_spec.samples = 2048;
        wanted_spec.callback = audio_callback;
        wanted_spec.userdata = m_player;

        if (SDL_OpenAudio(&wanted_spec, NULL) < 0) {
            fprintf(stderr, "SDL_OpenAudio: %s\n", SDL_GetError());
        }
        //m_thread = std::thread([this,bufferLength,bufferMillis] { this->SoundThreadFunction(bufferLength,bufferMillis); });
    }

    void SoundThreadFunction(int64_t buffer_length, int64_t bufferMillis)
    {
        std::vector<uint8_t> buffer(buffer_length);
        while (!m_finished){
            int res = 0;
            m_player->GetSound(buffer.data(), buffer_length);
            /*for (auto i : buffer){
                res += i;
                if (res > 0)
                    break;
            }*/
            std::this_thread::sleep_for(std::chrono::duration<int, std::milli>(bufferMillis));
        }
    }
    void SeekDone(int64_t timeMilliceconds)
    {
        //m_player->Play(true);
    }
    void NextFrameAvailable()
    {
        if (!m_rendererInitialized)
        {
            m_player->GetFrameSize(m_frameWidth, m_frameHeight);
            screen = SDL_CreateWindow("My Game Window",
                m_x,
                m_y,
                m_frameWidth, m_frameHeight,
                0);
            if (!screen) {
                fprintf(stderr, "SDL: could not set video mode - exiting\n");
                exit(1);
            }

            renderer = SDL_CreateRenderer(screen, -1, 0);
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
            SDL_RenderClear(renderer);
            SDL_RenderPresent(renderer);
            sdlTexture = SDL_CreateTexture(renderer,
                SDL_PIXELFORMAT_BGR888,
                SDL_TEXTUREACCESS_STREAMING,
                m_frameWidth, m_frameHeight);
            m_bufferSize = m_frameWidth * m_frameHeight * 4 * sizeof(uint8_t);
            buffer = new uint8_t[m_bufferSize];
            m_rendererInitialized = true;
            SDL_PauseAudio(0);
        }
        //std::this_thread::sleep_for(std::chrono::duration<int, std::milli>(80));
        if (m_player->GetAvailableFrame(&buffer, m_bufferSize))
        {
            SDL_UpdateTexture(sdlTexture, NULL, buffer, m_frameWidth*sizeof(uint8_t)* 4);
            SDL_RenderClear(renderer);
            SDL_RenderCopy(renderer, sdlTexture, NULL, NULL);
            SDL_RenderPresent(renderer);
            //++m_frameCounter;
        }
        if (m_frameCounter == 100)
        {
            //std::this_thread::sleep_for(std::chrono::duration<int, std::milli>(1000));
            //m_player->Seek(5555);
            //m_frameCounter = 0;
        }
    }
    void FileEnded()
    {
        m_finished = true;
    }
    void Error(int64_t errorCode)
    {

    }
    void Free()
    {
        delete m_player;
    }
};




int _tmain(int argc, _TCHAR* argv[])
{
    int counter=0;
    MainClass cl1, cl2, cl3, cl4, cl5, cl6, cl7, cl8, cl9, cl10, cl11, cl12, cl13, cl14, cl15, cl16;
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER)) {
        fprintf(stderr, "Could not initialize SDL - %s\n", SDL_GetError());
        exit(1);
    }
    cl1.Go(50,50,filePath1);
    //cl2.Go(100,100,filePath2);
    //cl3.Go(150,150,filePath3);
    /*cl4.Go(75, 75);
    cl5.Go(100, 100);*/
    bool finished = false;
    while (!finished)
    {
        std::this_thread::sleep_for(std::chrono::duration<int, std::milli>(40));
        finished = cl1.m_finished;
    }
    cl1.Free();
}
/*
    av_register_all();
    AVFormatContext *pFormatCtx = NULL;
    int errorcode = 0;
    if ((errorcode = avformat_open_input(&pFormatCtx, filePath, NULL, 0)) != 0)
        return -1;
    if ((errorcode = avformat_find_stream_info(pFormatCtx, NULL))<0)
        return -1;
    av_dump_format(pFormatCtx, 0, filePath, 0);

    int i;
    AVCodecContext *pCodecCtxOrig = NULL;
    AVCodecContext *pCodecCtx = NULL;

    // Find the first video stream
    int videoStream = -1;
    for (i = 0; i<pFormatCtx->nb_streams; i++)
    if (pFormatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
        videoStream = i;
        break;
    }
    if (videoStream == -1)
        return -1; // Didn't find a video stream

    // Get a pointer to the codec context for the video stream
    pCodecCtxOrig = pFormatCtx->streams[videoStream]->codec;

    AVCodec *pCodec = NULL;

    // Find the decoder for the video stream
    pCodec = avodec_find_decoder(pCodecCtxOrig->codec_id);
    if (pCodec == NULL) {
        fprintf(stderr, "Unsupported codec!\n");
        return -1; // Codec not found
    }
    // Copy context
    pCodecCtx = avcodec_alloc_context3(pCodec);
    if (avcodec_copy_context(pCodecCtx, pCodecCtxOrig) != 0) {
        fprintf(stderr, "Couldn't copy codec context");
        return -1; // Error copying codec context
    }
    // Open codec
    if ((errorcode = avcodec_open2(pCodecCtx, pCodec, NULL))<0)
        return -1; // Could not open codec

    AVFrame *pFrame = NULL;

    // Allocate video frame
    pFrame = av_frame_alloc();

    // Allocate an AVFrame structure
    AVFrame *pFrameRGB = av_frame_alloc();
    if (pFrameRGB == NULL)
        return -1;

    uint8_t *buffer = NULL;
    int numBytes;
    // Determine required buffer size and allocate buffer
    numBytes = avpicture_get_size(PIX_FMT_RGB24, pCodecCtx->width,
        pCodecCtx->height);
    buffer = (uint8_t *)av_malloc(numBytes*sizeof(uint8_t));

    // Assign appropriate parts of buffer to image planes in pFrameRGB
    // Note that pFrameRGB is an AVFrame, but AVFrame is a superset
    // of AVPicture
    avpicture_fill((AVPicture *)pFrameRGB, buffer, PIX_FMT_RGB24,
        pCodecCtx->width, pCodecCtx->height);

    struct SwsContext *sws_ctx = NULL;
    int frameFinished;
    AVPacket packet;
    // initialize SWS context for software scaling
    sws_ctx = sws_getContext(pCodecCtx->width,
        pCodecCtx->height,
        pCodecCtx->pix_fmt,
        pCodecCtx->width,
        pCodecCtx->height,
        PIX_FMT_RGB24,
        SWS_BILINEAR,
        NULL,
        NULL,
        NULL
        );

    i = 0;

    SDL_Window *screen = SDL_CreateWindow("My Game Window",
        SDL_WINDOWPOS_UNDEFINED,
        SDL_WINDOWPOS_UNDEFINED,
        pCodecCtx->width, pCodecCtx->height,
        0);
    if (!screen) {
        fprintf(stderr, "SDL: could not set video mode - exiting\n");
        exit(1);
    }

    SDL_Renderer *renderer = SDL_CreateRenderer(screen, -1, 0);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    SDL_RenderPresent(renderer);
    SDL_Texture *sdlTexture = SDL_CreateTexture(renderer,
        SDL_PIXELFORMAT_RGB24,
        SDL_TEXTUREACCESS_STREAMING,
        pCodecCtx->width, pCodecCtx->height);

    while (av_read_frame(pFormatCtx, &packet) >= 0) {
        // Is this a packet from the video stream?
        if (packet.stream_index == videoStream) {
            // Decode video frame
            avcodec_decode_video2(pCodecCtx, pFrame, &frameFinished, &packet);

            // Did we get a video frame?
            if (frameFinished) {
                // Convert the image from its native format to RGB
                sws_scale(sws_ctx, (uint8_t const * const *)pFrame->data,
                    pFrame->linesize, 0, pCodecCtx->height,
                    pFrameRGB->data, pFrameRGB->linesize);

                // Save the frame to disk
                SDL_UpdateTexture(sdlTexture, NULL, pFrameRGB->data[0], pCodecCtx->width*sizeof(uint8_t)*3);
                SDL_RenderClear(renderer);
                SDL_RenderCopy(renderer, sdlTexture, NULL, NULL);
                SDL_RenderPresent(renderer);
                std::this_thread::sleep_for(std::chrono::duration<int, std::milli>(40));
                if (++i <= 5)
                    SaveFrame(pFrameRGB, pCodecCtx->width,
                    pCodecCtx->height, i);
            }
        }

        // Free the packet that was allocated by av_read_frame
        av_free_packet(&packet);
    }

    // Free the RGB image
    av_free(buffer);
    av_free(pFrameRGB);

    // Free the YUV frame
    av_free(pFrame);

    // Close the codecs
    avcodec_close(pCodecCtx);
    avcodec_close(pCodecCtxOrig);

    // Close the video file
    avformat_close_input(&pFormatCtx);

	return 0;
}

*/