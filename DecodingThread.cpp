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
#include "AVPacketQueue.h"
#include "AudioDecoder.h"
#include "FrameQueueManager.h"
#include "DecodingThread.h"

#define WAIT_TIME 50

int DecodingThreadErrorCodeToInt(DecodingThreadErrorCode code)
{
    switch (code)
    {
    case DecodingThreadErrorCode::DecodingError:
        return -1;
    case DecodingThreadErrorCode::SeekError:
    default:
        return -2;
    }
}

void DecodingThread::ThreadFunc()
{
    while (true)
    {
        if (m_destroying)
            return;
        if(m_frameQueueManager->GetFreeFramesCount() == 0)
        {
            std::this_thread::sleep_for(std::chrono::duration<int, std::milli>(WAIT_TIME));
            continue;
        }
        switch (m_currentTask)
        {
            case Task::create:
                if (!DecodeFirstFrame())
                    return;
                break;
            case Task::play:
                DecodeFrame();
                break;
            case Task::pause:
                if (m_reportPause){
                    OnPaused();
                    m_reportPause = false;
                }
                std::this_thread::sleep_for(std::chrono::duration<int, std::milli>(WAIT_TIME));
                TakeNextTask();
                break;
            case Task::stop:
                if (!FindFirstFrame(0))
                    return;
                if (m_firstFrameDone)
                    OnStopped();
                break;
            case Task::seek:
                if (!FindFirstFrame(m_currentSeekPosition))
                    return;
                if (m_firstFrameDone)
                    OnSeekDone();
                break;
        }
    }
}

void DecodingThread::DecodeFrame()
{
    if (ReadNextPacket(true))
    {
        if (m_decodingStuff.frameFinished)
        {
            ScopedLock lock(m_mutex);
            m_frameQueueManager->SaveFrame(m_decodingStuff.pFrame, CurrentTimeBaseSeconds());
            //m_decodingStuff.pFrame = av_frame_alloc();
            OnFrameReady();
            TakeNextTask();
        }
    }
    else
    {
        m_currentTask = Task::pause;
        OnVideoEnd();
    }
}

bool DecodingThread::FindFirstFrame(const int64_t position)
{
    ScopedLock lock(m_mutex);
    bool found = false;
    int64_t curSeekPos = position;
    OnSeekStart();
    while (1)
    {
        bool prevFrameAvailable = false;
        if (SeekFrame(curSeekPos))
        {
            while (1)
            {
                if (ReadNextPacket(true))
                {
                    if (m_decodingStuff.frameFinished)
                    {
                        int64_t time = av_frame_get_best_effort_timestamp(m_decodingStuff.pFrame) * (CurrentTimeBaseSeconds() * 1000);
                        m_currentPTS = time;
                        if (time >= position)
                        {
                            if ((time - position) < (CurrentTimeBaseSeconds() * 1000))
                            {
                                m_frameQueueManager->SaveFirstFrame(m_decodingStuff.pFrame, CurrentTimeBaseSeconds());
                                //m_decodingStuff.pFrame = av_frame_alloc();
                                found = true;
                                break;
                            }
                            else if (prevFrameAvailable)
                            {
                                m_frameQueueManager->SaveFrame(m_decodingStuff.pFrame, CurrentTimeBaseSeconds());
                                //m_decodingStuff.pFrame = av_frame_alloc();
                                found = true;
                                break;
                            }
                            else if (curSeekPos == 0)
                            {
                                m_frameQueueManager->SaveFirstFrame(m_decodingStuff.pFrame, CurrentTimeBaseSeconds());
                                //m_decodingStuff.pFrame = av_frame_alloc();
                                found = true;
                                break;
                            }
                            else
                            {
                                curSeekPos = curSeekPos < 1000 ? 0 : curSeekPos - 1000;
                                break;
                            }
                        }
                        else
                        {
                            if ((position - time) < (CurrentTimeBaseSeconds() * 1000))
                            {
                                m_frameQueueManager->SaveFirstFrame(m_decodingStuff.pFrame, CurrentTimeBaseSeconds());
                                //m_decodingStuff.pFrame = av_frame_alloc();
                                found = true;
                                break;
                            }
                            else
                            {
                                prevFrameAvailable = true;
                                m_frameQueueManager->SaveFirstFrame(m_decodingStuff.pFrame, CurrentTimeBaseSeconds());
                                //m_decodingStuff.pFrame = av_frame_alloc();
                            }
                        }
                    }
                }
                else
                {
                    if (prevFrameAvailable)
                    {
                        found = true;
                        break;
                    }
                    else if (curSeekPos == 0)
                    {
                        OnError(DecodingThreadErrorCode::SeekError);
                        return false;
                    }
                    else
                    {
                        curSeekPos = curSeekPos < 1000 ? 0 : curSeekPos - 1000;
                        break;
                    }
                }
            }
        }
        else
        {
            OnError(DecodingThreadErrorCode::SeekError);
            return false;
        }
        if (found)
        {
            m_firstFrameDone = true;
            OnFirstFrameDone();
            TakeNextTask();
            return true;
        }
    }
}

bool DecodingThread::DecodeFirstFrame()
{
    if (ReadNextPacket(true))
    {
        if (m_decodingStuff.frameFinished)
        {
            ScopedLock lock(m_mutex);
            m_frameQueueManager->SaveFrame(m_decodingStuff.pFrame, CurrentTimeBaseSeconds());
            //m_decodingStuff.pFrame = av_frame_alloc();
            m_firstFrameDone = true;
            OnFirstFrameDone();
            TakeNextTask();
        }
        return true;
    }
    else
    {
        OnError(DecodingThreadErrorCode::DecodingError);
        return false;
    }
}

bool DecodingThread::SeekFrame(int64_t milliseconds)
{
    int seekFlags = milliseconds > m_currentPTS ? 0 : AVSEEK_FLAG_BACKWARD;
    AVRational timebase{ 1, 1000 };
    int64_t seekTime = av_rescale_q(milliseconds, timebase,
        m_decodingStuff.pFormatCtx->streams[m_decodingStuff.videoStreamIndex]->time_base);
    if (av_seek_frame(m_decodingStuff.pFormatCtx, m_decodingStuff.videoStreamIndex,
        seekTime, seekFlags) >= 0)
    {
        m_audioPacketQueue->ResetQueue();
        m_videoPacketQueue->ResetQueue();
        avcodec_flush_buffers(m_decodingStuff.pCodecCtx);
        m_audioDecoder->Reset();
        av_free(m_decodingStuff.pFrame);
        m_decodingStuff.pFrame = av_frame_alloc();
        return true;
    }
    else
    {
        OnError(DecodingThreadErrorCode::SeekError);
        return false;
    }
}

void DecodingThread::TakeNextTask()
{
    ScopedLock lock(m_taskMutex);
    bool wasPause = m_currentTask == Task::pause;
    if (m_taskQueue.empty())
    {
        switch (m_currentTask)
        {
        case Task::create:
        case Task::pause:
        case Task::seek:
        case Task::stop:
            m_currentTask = Task::pause;
            break;
        case Task::play:
            m_currentTask = Task::play;
            break;
        }
    }
    else
    {
        m_currentTask = m_taskQueue.front();
        if (m_currentTask == Task::stop || m_currentTask == Task::seek)
            m_seekDone = false;
        m_taskQueue.pop();
    }
    m_reportPause = m_currentTask == Task::pause && !wasPause;
}

bool DecodingThread::ReadNextPacket(bool fillBothQueues)
{
    bool no_more_packets = false;
    m_decodingStuff.frameFinished = 0;
    if (av_read_frame(m_decodingStuff.pFormatCtx, &m_decodingStuff.packet) >= 0)
    {
        if (m_decodingStuff.packet.stream_index == m_decodingStuff.videoStreamIndex)
        {
            m_videoPacketQueue->PutPacket(&m_decodingStuff.packet);
        }
        else if (m_decodingStuff.packet.stream_index == m_decodingStuff.audioStreamIndex)
        {
            m_audioPacketQueue->PutPacket(&m_decodingStuff.packet);
        }
        av_free_packet(&m_decodingStuff.packet);
    }
    else
    {
        no_more_packets = true;
    }

    if (!fillBothQueues || m_audioPacketQueue->GetSize() > 10 || m_decodingStuff.audioStreamIndex == -1 || no_more_packets)
    {
        ScopedLock lock(m_mutex);
        while (m_videoPacketQueue->GetSize() && !m_decodingStuff.frameFinished)
        {
            SmartAvPacket* currentPacket = m_videoPacketQueue->GetPacket();
            if (currentPacket)
            {
                avcodec_decode_video2(m_decodingStuff.pCodecCtx, m_decodingStuff.pFrame,
                    &m_decodingStuff.frameFinished, &currentPacket->m_packet);
                delete currentPacket;
            }
        }
        if (m_decodingStuff.frameFinished)
        {
            if (m_decodingStuff.packet.dts != AV_NOPTS_VALUE) {
                m_currentPTS = av_frame_get_best_effort_timestamp(m_decodingStuff.pFrame)
                    * (av_q2d(m_decodingStuff.pFormatCtx->streams[m_decodingStuff.videoStreamIndex]->time_base)
                    * 1000);
            }
            return true;
        }
    }
    return !no_more_packets;
}

void DecodingThread::InitializeDecodingStuff()
{
    m_decodingStuff.videoStreamIndex = -1;
    m_decodingStuff.audioStreamIndex = -1;
    m_decodingStuff.frameFinished = 0;
    m_decodingStuff.pFormatCtx = NULL;
    m_decodingStuff.pCodecCtx = NULL;
    m_decodingStuff.pAudioCodecCtx = NULL;
    m_decodingStuff.pFrame = NULL;
    m_decodingStuff.avio_ctx = NULL;
    m_decodingStuff.avio_ctx_buffer = NULL;
    m_decodingStuff.bufferData = NULL;
}

void DecodingThread::FreeDecodingStuff()
{
    if (m_decodingStuff.pFormatCtx != NULL)
    {
        avformat_close_input(&m_decodingStuff.pFormatCtx);
        m_decodingStuff.pFormatCtx = NULL;
    }
    if (m_decodingStuff.pCodecCtx != NULL)
    {
        avcodec_close(m_decodingStuff.pCodecCtx);
        m_decodingStuff.pCodecCtx = NULL;
    }
    if (m_decodingStuff.pAudioCodecCtx != NULL)
    {
        avcodec_close(m_decodingStuff.pAudioCodecCtx);
        m_decodingStuff.pAudioCodecCtx = NULL;
    }
    if (m_decodingStuff.pFrame != NULL)
    {
        av_free(m_decodingStuff.pFrame);
        m_decodingStuff.pFrame = NULL;
    }
    if (m_decodingStuff.avio_ctx != NULL)
    {
        av_free(m_decodingStuff.avio_ctx);
        m_decodingStuff.avio_ctx = NULL;
    }
    
    if (m_decodingStuff.bufferData != NULL)
    {
        delete m_decodingStuff.bufferData;
        m_decodingStuff.bufferData = NULL;
    }

    if (m_audioDecoder != NULL)
    {
        delete m_audioDecoder;
        m_audioDecoder = NULL;
    }
}

DecodingThread::DecodingThread(const char* filePath, FrameQueueManager* frameQueueManager, AVPacketQueue* audioPacketQueue, AVPacketQueue* videoPacketQueue) :
    m_frameQueueManager(frameQueueManager),
    m_audioPacketQueue(audioPacketQueue),
    m_videoPacketQueue(videoPacketQueue),
    m_currentTask(Task::create),
    m_destroying(false),
    m_firstFrameDone(false),
    m_seekDone(false),
    m_initialized(false),
    m_currentSeekPosition(0),
    m_currentPTS(0),
    m_frameSize(0, 0),
    m_reportPause(false)
{
    InitializeDecodingStuff();
    int err = 0;
    if ((err = avformat_open_input(&m_decodingStuff.pFormatCtx, filePath, NULL, 0)) != 0)
        return;
    if (avformat_find_stream_info(m_decodingStuff.pFormatCtx, NULL)<0)
        return;
    av_dump_format(m_decodingStuff.pFormatCtx, 0, filePath, 0);

    Initialize();

    m_audioDecoder = new AudioDecoder(m_decodingStuff.pAudioCodecCtx, m_audioPacketQueue, m_decodingStuff.pFormatCtx->streams[m_decodingStuff.audioStreamIndex]);
}

void DecodingThread::Initialize()
{
    for (unsigned i = 0; i < m_decodingStuff.pFormatCtx->nb_streams; ++i)
    {
        switch (m_decodingStuff.pFormatCtx->streams[i]->codec->codec_type ) 
        {
            case AVMEDIA_TYPE_VIDEO:
                if (m_decodingStuff.videoStreamIndex == -1)
                    m_decodingStuff.videoStreamIndex = i;
                break;
            case AVMEDIA_TYPE_AUDIO:
                if (m_decodingStuff.audioStreamIndex == -1)
                    m_decodingStuff.audioStreamIndex = i;
                break;
        }
    }
    if (m_decodingStuff.videoStreamIndex == -1)
        return; // Didn't find a video stream

    // Get a pointer to the codec context for the video stream
    AVCodecContext *pCodecCtxOrig = m_decodingStuff.pFormatCtx->streams[m_decodingStuff.videoStreamIndex]->codec;

    AVCodec *pCodec = NULL;

    // Find the decoder for the video stream
    pCodec = avcodec_find_decoder(pCodecCtxOrig->codec_id);
    if (pCodec == NULL) {
        return;
    }
    // Copy context
    m_decodingStuff.pCodecCtx = avcodec_alloc_context3(pCodec);
    if (avcodec_copy_context(m_decodingStuff.pCodecCtx, pCodecCtxOrig) != 0) {
        return;
    }
    // Open codec
    if (avcodec_open2(m_decodingStuff.pCodecCtx, pCodec, NULL)<0)
        return; // Could not open codec
    m_decodingStuff.pFrame = av_frame_alloc();
    m_initialized = true;
    if (m_decodingStuff.audioStreamIndex != -1){//If we have audio stream
        pCodecCtxOrig = m_decodingStuff.pFormatCtx->streams[m_decodingStuff.audioStreamIndex]->codec;

        pCodec = NULL;

        // Find the decoder for the video stream
        pCodec = avcodec_find_decoder(pCodecCtxOrig->codec_id);
        if (pCodec == NULL) {
            return;
        }
        // Copy context
        m_decodingStuff.pAudioCodecCtx = avcodec_alloc_context3(pCodec);
        if (avcodec_copy_context(m_decodingStuff.pAudioCodecCtx, pCodecCtxOrig) != 0) {
            m_decodingStuff.pAudioCodecCtx = NULL;
            return;
        }
        // Open codec
        if (avcodec_open2(m_decodingStuff.pAudioCodecCtx, pCodec, NULL) < 0){
            m_decodingStuff.pAudioCodecCtx = NULL;
            return; // Could not open codec
        }
    }
}

int read_packet(void *opaque, uint8_t *buf, int buf_size)
{
    buffer_data *bd = (struct buffer_data *)opaque;
    buf_size = FFMIN(buf_size, bd->size - bd->currentPos);
    
    memcpy(buf, bd->currentPosPtr, buf_size);
    bd->currentPosPtr += buf_size;
    bd->currentPos += buf_size;
    if (buf_size <= 0)
        return 0;
    return buf_size;
}

int64_t seek(void *opaque, int64_t offset, int whence)
{
    struct buffer_data *bd = (struct buffer_data *)opaque;
    int64_t res_pos = 0;
    switch (whence)
    {
    case SEEK_SET:
        res_pos = offset;
        break;
    case SEEK_CUR:
        res_pos = bd->currentPos + offset;
        break;
    case SEEK_END:
        res_pos = bd->size + offset;
        break;
    case AVSEEK_SIZE:
        return bd->size;
    }
    if (res_pos < bd->size && res_pos > 0)
    {
        bd->currentPos = res_pos;
        bd->currentPosPtr = bd->ptr + res_pos;
        return res_pos;
    }
    return -1;
}

DecodingThread::DecodingThread(uint8_t* buffer, int64_t bufferSize, FrameQueueManager* frameQueueManager, AVPacketQueue* audioPacketQueue, AVPacketQueue* videoPacketQueue) :
    m_frameQueueManager(frameQueueManager),
    m_audioPacketQueue(audioPacketQueue),
    m_videoPacketQueue(videoPacketQueue),
    m_currentTask(Task::create),
    m_destroying(false),
    m_firstFrameDone(false),
    m_seekDone(false),
    m_initialized(false),
    m_currentSeekPosition(0),
    m_currentPTS(0),
    m_frameSize(0, 0),
    m_reportPause(false)
{
    InitializeDecodingStuff();
    size_t avio_ctx_buffer_size = 4096;
    int ret = 0;
    /* fill opaque structure used by the AVIOContext read callback */
    m_decodingStuff.bufferData = new buffer_data{ buffer, bufferSize, 0, buffer };
    
    m_decodingStuff.pFormatCtx = avformat_alloc_context();
    m_decodingStuff.avio_ctx_buffer = (uint8_t*)av_malloc(avio_ctx_buffer_size);
    
    m_decodingStuff.avio_ctx = avio_alloc_context(m_decodingStuff.avio_ctx_buffer, avio_ctx_buffer_size,
        0, m_decodingStuff.bufferData, &read_packet, NULL, &seek);
    
    m_decodingStuff.pFormatCtx->pb = m_decodingStuff.avio_ctx;
    ret = avformat_open_input(&m_decodingStuff.pFormatCtx, NULL, NULL, NULL);
    
    if (avformat_find_stream_info(m_decodingStuff.pFormatCtx, NULL)<0)
        return;
    av_dump_format(m_decodingStuff.pFormatCtx, 0, NULL, 0);

    Initialize();

    m_audioDecoder = new AudioDecoder(m_decodingStuff.pAudioCodecCtx, m_audioPacketQueue, m_decodingStuff.pFormatCtx->streams[m_decodingStuff.audioStreamIndex]);
}

void DecodingThread::Start()
{
    m_thread = std::thread([this] { this->ThreadFunc(); });
}

DecodingThread::~DecodingThread()
{
    m_destroying = true;
    m_thread.join();
    avformat_close_input(&m_decodingStuff.pFormatCtx);
    FreeDecodingStuff();
}

void DecodingThread::AddListener(DecodingThreadListener* listener)
{
    if (m_destroying)
        return;
    ScopedLock lock(m_eventMutex);
    m_listeners.push_back(listener);
}

void DecodingThread::RemoveListener(DecodingThreadListener* listener)
{
    if (m_destroying)
        return;
    ScopedLock lock(m_eventMutex);
    m_listeners.remove(listener);
}

void DecodingThread::Play()
{
    if (m_destroying)
        return;
    ScopedLock lock(m_taskMutex);
    m_taskQueue.push(Task::play);
}

void DecodingThread::Pause()
{
    if (m_destroying)
        return;
    ScopedLock lock(m_taskMutex);
    m_taskQueue.push(Task::pause);
}

void DecodingThread::Stop()
{
    if (m_destroying)
        return;
    ScopedLock lock(m_taskMutex);
    m_taskQueue.push(Task::stop);
}

void DecodingThread::Seek(int64_t timeInMilliseconds)
{
    if (m_destroying)
        return;
    ScopedLock lock(m_taskMutex);
    m_currentSeekPosition = timeInMilliseconds;
    m_taskQueue.push(Task::seek);
}

double DecodingThread::CurrentTimeBaseSeconds() const
{
    //ScopedLock lock(m_mutex);
    return av_q2d(m_decodingStuff.pFormatCtx->streams[m_decodingStuff.videoStreamIndex]->time_base);
}

int64_t DecodingThread::Duration() const
{
    //ScopedLock lock(m_mutex);
    return m_decodingStuff.pFormatCtx->streams[m_decodingStuff.videoStreamIndex]->duration *
        (av_q2d(m_decodingStuff.pFormatCtx->streams[m_decodingStuff.videoStreamIndex]->time_base) * 1000);
}

void DecodingThread::OnError(DecodingThreadErrorCode error)
{
    if (m_destroying)
        return;
    ScopedLock lock(m_eventMutex);
    for (auto listener : m_listeners)
        listener->OnError(error);

}
void DecodingThread::OnFrameReady()
{
    if (m_destroying)
        return;
    ScopedLock lock(m_eventMutex);
    for (auto listener : m_listeners)
        listener->OnFrameReady();
}

void DecodingThread::OnPaused()
{
    if (m_destroying)
        return;
    ScopedLock lock(m_eventMutex);
    for (auto listener : m_listeners)
        listener->OnPaused();
}

void DecodingThread::OnStopped()
{
    if (m_destroying)
        return;
    ScopedLock lock(m_eventMutex);
    for (auto listener : m_listeners)
        listener->OnStopped();
}

void DecodingThread::OnSeekStart()
{
    if (m_destroying)
        return;
    ScopedLock lock(m_eventMutex);
    for (auto listener : m_listeners)
        listener->OnSeekStart();
}

void DecodingThread::OnSeekDone()
{
    if (m_destroying)
        return;
    ScopedLock lock(m_eventMutex);
    for (auto listener : m_listeners)
        listener->OnSeekDone();
}

void DecodingThread::OnVideoEnd()
{
    if (m_destroying)
        return;
    ScopedLock lock(m_eventMutex);
    for (auto listener : m_listeners)
        listener->OnVideoEnd();
}

void DecodingThread::OnFirstFrameDone()
{
    if (m_destroying)
        return;
    ScopedLock lock(m_eventMutex);
    for (auto listener : m_listeners)
        listener->OnFirstFrameDone();
}