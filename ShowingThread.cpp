#include <cassert>
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
#include "AudioDecoder.h"
#include "DecodingThread.h"
#include "ShowingThread.h"

#define STANDARD_DELAY 40

ShowingThread::ShowingThread(DecodingThread* decodingThread, FrameQueueManager *frameQueueManager, AudioDecoder* audioDecoder) :
    m_currentFrameSize(0,0),
    m_isPlaying(false),
    m_decodingThreadPaused(true),
    m_frameReady(false),
    m_destroying(false),
    m_showFirstFrame(false),
    m_isSeeking(false),
    m_playBackTime(0),
    m_videoStartTime(0),
    m_currentFrame(NULL),
    m_decodingThread(decodingThread),
    m_frameQueueManager(frameQueueManager),
    m_audioDecoder(audioDecoder),
    m_audioBufferPos(m_audioBuffer),
    m_audioBufferNpos(m_audioBuffer),
    m_audioBufferPTS(0)
{
    m_decodingThread->AddListener(this);
}

ShowingThread::~ShowingThread()
{
    m_destroying = true;
    m_decodingThread->RemoveListener(this);
    m_thread.join();
}

void ShowingThread::Start()
{
    m_thread = std::thread([this] { this->ThreadFunction(); });
}



void ShowingThread::ThreadFunction()
{
    while (1)
    {
        if (m_destroying)
            return;
        m_mutex.lock();
        if (m_isSeeking)
        {
            m_frameMutex.lock();
            m_currentFrame = NULL;
            m_frameMutex.unlock();
            m_mutex.unlock();
            std::this_thread::sleep_for(std::chrono::duration<int, std::milli>(STANDARD_DELAY));
            continue;
        }
        if (m_showFirstFrame)
        {
            m_isPlaying = false;
            if (m_frameQueueManager->GetReadyFramesCount() < 1)
            {
                OnShowingError();
                m_mutex.unlock();
                return;
            }
            m_currentFrame = m_frameQueueManager->RequestReadyFrame();
            m_currentFrameSize = m_currentFrame->GetFrameSize();
            m_frameMutex.lock();
            m_frameReady = true;
            m_playBackTime = m_currentFrame->GetPresentationTime();
            m_lastFrameTimeStamp = std::chrono::system_clock::now();
            OnFirstFrameShown();
            m_showFirstFrame = false;
            m_frameMutex.unlock();
            m_mutex.unlock();
            continue;
        }
        if ((!m_isPlaying && m_frameQueueManager->GetReadyFramesCount() < 3))
        {
            m_mutex.unlock();
            std::this_thread::sleep_for(std::chrono::duration<int, std::milli>(STANDARD_DELAY));
            continue;
        }
        if (m_isPlaying && m_frameQueueManager->GetReadyFramesCount() < 1)
        {
            if (m_decodingThreadPaused)
            {
                m_isPlaying = false;
                OnNoMoreFrames();
            }
            m_mutex.unlock();
            std::this_thread::sleep_for(std::chrono::duration<int, std::milli>(STANDARD_DELAY));
            continue;
        }
        m_frameMutex.lock();
        m_frameQueueManager->FrameShown(m_currentFrame);
        m_currentFrame = m_frameQueueManager->RequestReadyFrame();
        m_currentFrameSize = m_currentFrame->GetFrameSize();
        if (!m_isPlaying)
        {
            ResetSound();
            m_isPlaying = true;
            m_videoStartTime = m_currentFrame->GetPresentationTime();
            m_startTime = std::chrono::steady_clock::now();
        }
        m_frameReady = true;
        m_lastFrameTimeStamp = std::chrono::system_clock::now();
        m_playBackTime = m_currentFrame->GetPresentationTime();
        OnFrameShown();
        m_frameMutex.unlock();
        while (1){
            if (m_frameQueueManager->GetReadyFramesCount() > 0)
            {
                int64_t timeLeft = std::chrono::duration_cast<std::chrono::duration<int64_t, std::milli>>(std::chrono::steady_clock::now() - m_startTime).count();
                int64_t nextFrameTimeStamp = m_frameQueueManager->GetFirstFrame()->GetPresentationTime();
                int64_t nextFrameShowTime = nextFrameTimeStamp - m_videoStartTime;
                int64_t nextFrameDelay = nextFrameShowTime - timeLeft;
                if (nextFrameDelay < 0){
                    InternalFrame* frame = m_frameQueueManager->RequestReadyFrame();
                    m_frameQueueManager->FrameShown(frame);
                    continue;
                }
                m_mutex.unlock();
                std::this_thread::sleep_for(std::chrono::duration<int64_t, std::milli>(nextFrameDelay));
                break;
            }
            else
            {
                m_mutex.unlock();
                std::this_thread::sleep_for(std::chrono::duration<int, std::milli>(STANDARD_DELAY));
                break;
            }
        }


    }
}

void ShowingThread::AddListener(ShowingThreadListener *listener)
{
    ScopedLock lock(m_mutex);
    m_listeners.push_back(listener);
}

void ShowingThread::RemoveListener(ShowingThreadListener *listener)
{
    ScopedLock lock(m_mutex);
    m_listeners.remove(listener);
}

bool ShowingThread::GetCurrentFrame(uint8_t** buffer, int32_t& bufferSize) const
{
    ScopedLock lock(m_frameMutex);
    if (m_destroying || !m_frameReady || m_currentFrame == NULL)
        return false;
    m_currentFrame->CopyFrame(buffer, bufferSize);
    return true;
}

int64_t ShowingThread::GetPlayBackTime() const
{
    ScopedLock lock(m_frameMutex);
    return m_playBackTime;
}

int64_t ShowingThread::GetPresizePlayBackTime() const
{
    ScopedLock lock(m_frameMutex);
    return m_isPlaying ? std::chrono::duration_cast<std::chrono::duration<int64_t, std::milli>>(std::chrono::steady_clock::now() - m_lastFrameTimeStamp).count() + m_playBackTime : m_playBackTime;
}

int32_t min(int32_t a, int32_t b)
{
    return a < b ? a : b;
}

void ShowingThread::GetSound(uint8_t* buffer, int32_t bufferSize)
{
    if (!IsPlaying() || !m_audioDecoder->GetSampleRate()){
        memset(buffer, 0, bufferSize);
        return;
    }
    uint8_t* bufferNpos = buffer + bufferSize;
    ScopedLock lock(m_frameMutex);
    int64_t currentPlayTime = GetPresizePlayBackTime();
    int32_t sampleSize = m_audioDecoder->GetSampleSizeBytes();
    int32_t multiplyFirstSampleTimes = 0;
    int32_t requiredShift = 0;
    while (bufferSize > 0){
        multiplyFirstSampleTimes = 0;
        requiredShift = ((currentPlayTime - m_audioBufferPTS) * m_audioDecoder->GetSampleRate() / 1000) * sampleSize;
        if (abs(requiredShift) < sampleSize * m_audioDecoder->GetSampleRate() / 10)
            requiredShift = 0;
        if (requiredShift < 0){
            multiplyFirstSampleTimes = min(bufferSize, abs(requiredShift))/sampleSize;
            requiredShift = 0;
        }
        //printf("\n requiredShift:%d ", requiredShift);
        //printf("m_audioBufferPTS:%d ", m_audioBufferPTS);
        //printf("currentPlayTime:%d ", currentPlayTime);

        if ((m_audioBufferNpos - m_audioBufferPos) > requiredShift)
        {
            if (requiredShift){
                m_audioBufferPTS += (requiredShift / sampleSize) * 1000 / m_audioDecoder->GetSampleRate();
                m_audioBufferPos += requiredShift;
            }
            for (int32_t i = 0; i < multiplyFirstSampleTimes; ++i){
                memcpy(buffer, m_audioBufferPos, sampleSize);
                buffer += sampleSize;
                bufferSize -= sampleSize;
                if (bufferSize < sampleSize)
                    return;
            }
            currentPlayTime += multiplyFirstSampleTimes * 1000 / m_audioDecoder->GetSampleRate();
            int32_t leftData = m_audioBufferNpos - m_audioBufferPos;
            int32_t copySize = min(leftData,bufferSize);
            memcpy(buffer, m_audioBufferPos, copySize);
            int32_t copiedTime = (copySize / sampleSize) * 1000 / m_audioDecoder->GetSampleRate();
            m_audioBufferPTS += copiedTime;
            currentPlayTime += copiedTime;
            m_audioBufferPos += copySize;
            buffer += copySize;
            bufferSize -= copySize;
            if (bufferSize < sampleSize)
                return;
        }
        m_audioBufferPos = m_audioBuffer;
        int32_t gotSize = m_audioDecoder->GetNextFrameData(m_audioBuffer, sizeof(m_audioBuffer), m_audioBufferPTS);
        if (gotSize <= 0){
            memset(buffer, 0, bufferSize);
            m_audioBufferNpos = m_audioBuffer;
            return;
        }
        m_audioBufferNpos = m_audioBufferPos + gotSize;
    }
}

void ShowingThread::GetAudioParams(int & channels, int & sampleRate, AVSampleFormat & format)
{
    channels = m_audioDecoder->GetNumberOfChannels();
    sampleRate = m_audioDecoder->GetSampleRate();
    format = m_audioDecoder->GetSampleFormat();
}

void ShowingThread::ResetSound()
{
    m_audioBufferNpos = m_audioBufferPos = m_audioBuffer;
    m_audioBufferPTS = 0;
}

FrameSize ShowingThread::GetCurrentFrameSize() const
{
    ScopedLock lock(m_frameMutex);
    return m_currentFrameSize;
}

//DecodingThreadListener interface
void ShowingThread::OnError(DecodingThreadErrorCode error)
{
    ScopedLock lock(m_mutex);
    m_decodingThreadPaused = true;
}

void ShowingThread::OnFrameReady()
{
    ScopedLock lock(m_mutex);
    m_decodingThreadPaused = false;
}

void ShowingThread::OnPaused()
{
    ScopedLock lock(m_mutex);
    m_decodingThreadPaused = true;
}

void ShowingThread::OnStopped()
{
    ScopedLock lock(m_mutex);
    m_isSeeking = false;
    m_decodingThreadPaused = true;
}

void ShowingThread::OnSeekStart()
{
    ScopedLock lock(m_mutex);
    m_isSeeking = true;
}

void ShowingThread::OnSeekDone()
{
    ScopedLock lock(m_mutex);
    m_isSeeking = false;
}

void ShowingThread::OnVideoEnd()
{
    ScopedLock lock(m_mutex);
    m_decodingThreadPaused = true;
}

void ShowingThread::OnFirstFrameDone()
{
    ScopedLock lock(m_mutex);
    m_showFirstFrame = true;
}

//ShowingThreadListener interface
void ShowingThread::OnFirstFrameShown()
{
    ScopedLock lock(m_mutex);
    for (auto listener : m_listeners)
        listener->OnFirstFrameShown();
}

void ShowingThread::OnNoMoreFrames()
{
    ScopedLock lock(m_mutex);
    for (auto listener : m_listeners)
        listener->OnNoMoreFrames();
}

void ShowingThread::OnFrameShown()
{
    //std::this_thread::sleep_for(std::chrono::duration<int, std::milli>(35));
    ScopedLock lock(m_mutex);
    for (auto listener : m_listeners)
        listener->OnFrameShown();
}

void ShowingThread::OnShowingError()
{
    ScopedLock lock(m_mutex);
    for (auto listener : m_listeners)
        listener->OnShowingError();
}

void ShowingThread::OnStartPlaying()
{
    ScopedLock lock(m_mutex);
    for (auto listener : m_listeners)
        listener->OnStartPlaying();
}