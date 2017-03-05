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
#include "FrameQueueManager.h"

InternalFrame::InternalFrame(AVPixelFormat format) : 
    m_frame(NULL),
    m_presentationTime(0),
    m_buffer(NULL),
    m_frameSize(0,0),
    m_bufferSize(0),
    m_format(format)
{

}

InternalFrame::~InternalFrame()
{
    FreeStuff();
}

void InternalFrame::FreeStuff()
{
    if (m_buffer != NULL)
    {
        av_free(m_buffer);
        m_buffer = NULL;
        m_bufferSize = 0;
    }
    if (m_frame != NULL)
    {
        av_free(m_frame);
        m_frame = NULL;
        m_frameSize =  FrameSize(0,0);
    }
}

void InternalFrame::SaveFrame(AVFrame *frame, SwsContext* ctx, double timeBase)
{
    if (m_frame == NULL){
        m_frame = av_frame_alloc();
        int buff_size = avpicture_get_size((AVPixelFormat)frame->format, frame->width, frame->height);
        if (m_bufferSize != buff_size){
            if (m_buffer != NULL)
                av_free(m_buffer);
            m_buffer = (uint8_t *)av_malloc(buff_size);
            m_bufferSize = buff_size;
            for (int i = 0; i < 8; ++i){
                m_frame->linesize[i] = frame->linesize[i];
            }
            avpicture_fill((AVPicture *)m_frame, m_buffer, (AVPixelFormat)frame->format, frame->width, frame->height);
            m_frame->format = frame->format;
            m_frame->width = frame->width;
            m_frame->height = frame->height;
        }
    }
    int err = av_frame_copy(m_frame, frame);
    m_presentationTime = av_frame_get_best_effort_timestamp(frame) * (timeBase * 1000);
    m_frameSize = FrameSize(m_frame->width, m_frame->height);
    m_context = ctx;
}

/*void InternalFrame::SaveFrame(AVFrame *frame, SwsContext* ctx, double timeBase)
{
    if (m_frameSize.first != frame->width || m_frameSize.second != frame->height)
    {
        FreeStuff();
        m_frameSize = FrameSize(frame->width, frame->height);
        m_bufferSize = avpicture_get_size(m_format, m_frameSize.first, m_frameSize.second);
        m_buffer = (uint8_t *)av_malloc(m_bufferSize);
        m_frame = av_frame_alloc();
        avpicture_fill((AVPicture *)m_frame, m_buffer, PIX_FMT_RGBA, m_frameSize.first, m_frameSize.second);
    }
    else
    {
        m_presentationTime = 0;
    }
    sws_scale(ctx, (uint8_t const * const *)frame->data,
        frame->linesize, 0, frame->height,
        m_frame->data, m_frame->linesize);
    m_presentationTime = av_frame_get_best_effort_timestamp(frame) * (timeBase * 1000);
}*/

void InternalFrame::CopyFrame(uint8_t** buffer, int32_t & bufferSize) const
{
    int buff_Size = avpicture_get_size(m_format, m_frame->width, m_frame->height);   

    if (bufferSize != buff_Size)
    {
        if (*buffer != NULL)
            delete[](*buffer);
        *buffer = new uint8_t[buff_Size];
        bufferSize = buff_Size;
    }
    AVFrame *frame = av_frame_alloc();
    avpicture_fill((AVPicture *)frame, *buffer, PIX_FMT_RGBA, m_frame->width, m_frame->height);
    sws_scale(m_context, (uint8_t const * const *)m_frame->data,
        m_frame->linesize, 0, m_frame->height,
        frame->data, frame->linesize);
    av_frame_free(&frame);
}

FrameQueueManager::FrameQueueManager(int frameNumberLimit, AVPixelFormat format) : 
    m_swsCtx(NULL),
    m_frameSize(0,0),
    m_format(format)
{
    for (int i = 0; i < frameNumberLimit; ++i)
    {
        InternalFrame * frame = new InternalFrame(m_format);
        m_FullFrameList.push_back(frame);
        m_FreeFrames.push_back(frame);
    }
}

FrameQueueManager::~FrameQueueManager()
{
    m_mutex.lock();
    if (m_swsCtx != NULL)
    {
        sws_freeContext(m_swsCtx);
        m_swsCtx = NULL;
    }
    for (auto frame : m_FullFrameList)
    {
        delete frame;
    }
    m_FullFrameList.clear();
    m_FreeFrames.clear();
    m_ReadyFrames.clear();
    m_mutex.unlock();
}

void FrameQueueManager::SaveFirstFrame(AVFrame *frame, double timeBase)
{
    ResetFrames();
    SaveFrame(frame, timeBase);
}

/*void FrameQueueManager::SaveFrame(AVFrame *frame, double timeBase)
{
    ScopedLock lock(m_mutex);
    if (m_frameSize.first != frame->width || m_frameSize.second != frame->height || m_swsCtx == NULL)
    {
        if (m_swsCtx != NULL)
        {
            av_free(m_swsCtx);
            m_swsCtx = NULL;
        }
        m_swsCtx = sws_getContext(frame->width,
            frame->height,
            (AVPixelFormat)frame->format,
            frame->width,
            frame->height,
            m_format,
            SWS_BILINEAR,
            NULL,
            NULL,
            NULL
            );
        m_frameSize = FrameSize(frame->width, frame->height);
    }
    if (!m_FreeFrames.empty())
    {
        InternalFrame* fr = m_FreeFrames.front();
        m_FreeFrames.pop_front();
        fr->SaveFrame(frame, m_swsCtx, timeBase);
        m_ReadyFrames.push_back(fr);
    }
}*/

void FrameQueueManager::SaveFrame(AVFrame *frame, double timeBase)
{
    ScopedLock lock(m_mutex);
    if (!m_FreeFrames.empty())
    {
        if (m_frameSize.first != frame->width || m_frameSize.second != frame->height || m_swsCtx == NULL)
        {
            if (m_swsCtx != NULL)
            {
                sws_freeContext(m_swsCtx);
                m_swsCtx = NULL;
            }
            m_swsCtx = sws_getContext(frame->width,
                frame->height,
                (AVPixelFormat)frame->format,
                frame->width,
                frame->height,
                m_format,
                SWS_BILINEAR,
                NULL,
                NULL,
                NULL
                );
            m_frameSize = FrameSize(frame->width, frame->height);
        }
        InternalFrame* fr = m_FreeFrames.front();
        m_FreeFrames.pop_front();
        fr->SaveFrame(frame, m_swsCtx, timeBase);
        m_ReadyFrames.push_back(fr);
    }
}

InternalFrame* FrameQueueManager::RequestReadyFrame()
{
    InternalFrame* ret = NULL;
    ScopedLock lock(m_mutex);
    if (!m_ReadyFrames.empty())
    {
        ret = m_ReadyFrames.front();
        m_ReadyFrames.pop_front();
    }
    return ret;
}

InternalFrame* FrameQueueManager::GetFirstFrame()
{
    InternalFrame* ret = NULL;
    ScopedLock lock(m_mutex);
    if (!m_ReadyFrames.empty())
    {
        ret = m_ReadyFrames.front();
    }
    return ret;
}

void FrameQueueManager::FrameShown(InternalFrame* frame)
{
    if (frame == NULL)
        return;
    ScopedLock lock(m_mutex);
    m_FreeFrames.push_back(frame);
}

void FrameQueueManager::ResetFrames()
{
    ScopedLock lock(m_mutex);
    m_ReadyFrames.clear();
    m_FreeFrames.assign(m_FullFrameList.begin(), m_FullFrameList.end());
}

int FrameQueueManager::GetFreeFramesCount()const
{
    ScopedLock lock(m_mutex);
    return m_FreeFrames.size();
}

int FrameQueueManager::GetReadyFramesCount()const
{
    ScopedLock lock(m_mutex);
    return m_ReadyFrames.size();
}