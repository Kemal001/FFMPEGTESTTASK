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
#include "FrameQueueManager.h"
#include "DecodingThread.h"
#include "ShowingThread.h"
#include "FfmpegPlayer.h"

#define WORKING_THREAD_WAIT_TIME 40
//External Interface to interact with player.

std::recursive_mutex FfmpegPlayer::s_globalContextGuard;
bool FfmpegPlayer::s_commonInitialized = false;

FfmpegPlayer::FfmpegPlayer(bool sendAsyncCallbacks /* = true*/) : m_decodingThread(NULL),
    m_showingThread(NULL),
    m_audioPacketQueue(NULL),
    m_frameQueueManager(NULL),
    m_listener(NULL),
    m_currentTask(FfmpegPlayerTaskType::None),
    m_Ok(true),
    m_decodingThreadPlaying(false),
    m_destroying(false),
    m_sendAsyncCallbacks(sendAsyncCallbacks),
    m_isLooped(false),
    m_decodingThreadReachedEOF(false),
    m_fileEnded(false),
    m_reportPlay(false)
{

}

FfmpegPlayer::~FfmpegPlayer()
{
    m_mutex.lock();
    m_destroying = true;
    m_mutex.unlock();
    m_workingThread.join();

    if (m_showingThread != NULL)
        delete m_showingThread;
    if (m_decodingThread != NULL)
        delete m_decodingThread;
    if (m_frameQueueManager != NULL)
        delete m_frameQueueManager;
    if (m_audioPacketQueue != NULL)
        delete m_audioPacketQueue;
    if (m_videoPacketQueue != NULL)
        delete m_videoPacketQueue;
}

bool FfmpegPlayer::Initialize(const char* filePath, FfmpegPlayerListener* listener, AVPixelFormat format /*= PIX_FMT_RGBA*/)
{
    if (filePath == NULL || listener == NULL)
        return false;
    m_listener = listener;
    ScopedLock lock(s_globalContextGuard);
    if (!s_commonInitialized)
    {
        av_register_all();
        s_commonInitialized = true;
    }
    m_currentTask = FfmpegPlayerTask(FfmpegPlayerTaskType::Initialize);
    m_audioPacketQueue = new AVPacketQueue(1000);
    m_videoPacketQueue = new AVPacketQueue(10000);
    m_frameQueueManager = new FrameQueueManager(4, format);
    m_decodingThread = new DecodingThread(filePath, m_frameQueueManager, m_audioPacketQueue, m_videoPacketQueue);
    if (!m_decodingThread->InitializedSuccessful())
        return false;
    m_showingThread = new ShowingThread(m_decodingThread, m_frameQueueManager,m_decodingThread->GetAudioDecoder());
    m_decodingThread->AddListener(this);
    m_showingThread->AddListener(this);
    m_workingThread = std::thread([this] { this->WorkingThread(); });
    m_decodingThread->Start();
    m_showingThread->Start();
    return true;
}

bool FfmpegPlayer::Initialize(uint8_t* buffer, int64_t bufferSize, FfmpegPlayerListener* listener, AVPixelFormat format /*= PIX_FMT_RGBA*/)
{
    if (buffer == NULL || listener == NULL || bufferSize <= 0)
        return false;
    m_listener = listener;
    ScopedLock lock(s_globalContextGuard);
    if (!s_commonInitialized)
    {
        av_register_all();
        s_commonInitialized = true;
    }
    m_currentTask = FfmpegPlayerTask(FfmpegPlayerTaskType::Initialize);
    m_audioPacketQueue = new AVPacketQueue(1000);
    m_videoPacketQueue = new AVPacketQueue(10000);
    m_frameQueueManager = new FrameQueueManager(4, format);
    m_decodingThread = new DecodingThread(buffer, bufferSize, m_frameQueueManager, m_audioPacketQueue, m_videoPacketQueue);
    if (!m_decodingThread->InitializedSuccessful())
        return false;
    m_showingThread = new ShowingThread(m_decodingThread, m_frameQueueManager, m_decodingThread->GetAudioDecoder());
    m_decodingThread->AddListener(this);
    m_showingThread->AddListener(this);
    m_workingThread = std::thread([this] { this->WorkingThread(); });
    m_decodingThread->Start();
    m_showingThread->Start();
    return true;
}

void FfmpegPlayer::Stop()
{
    ScopedLock lock(m_mutex);
    m_isLooped = false;
    m_taskQueue.push_back(FfmpegPlayerTask(FfmpegPlayerTaskType::Stop));
}

void FfmpegPlayer::Pause()
{
    ScopedLock lock(m_mutex);
    if (!m_decodingThreadPlaying)
        return;
    m_isLooped = false;
    m_taskQueue.push_back(FfmpegPlayerTask(FfmpegPlayerTaskType::Pause));
}

void FfmpegPlayer::Play(bool loop/* = false*/)
{
    ScopedLock lock(m_mutex);
    m_reportPlay = true;
    if (m_fileEnded)
        m_taskQueue.push_back(FfmpegPlayerTask(FfmpegPlayerTaskType::Stop));
    m_isLooped = loop;
    m_taskQueue.push_back(FfmpegPlayerTask(FfmpegPlayerTaskType::Play));
}

void FfmpegPlayer::Seek(int64_t timeMilliceconds)
{
    ScopedLock lock(m_mutex);
    m_taskQueue.push_back(FfmpegPlayerTask(FfmpegPlayerTaskType::Seek));
    m_taskQueue.back().m_time = timeMilliceconds;
    if (m_decodingThreadPlaying && m_showingThread->IsPlaying())
        m_taskQueue.push_back(FfmpegPlayerTask(FfmpegPlayerTaskType::Play));
}

int64_t FfmpegPlayer::GetDuration() const
{
    return m_decodingThread->Duration();
}

int64_t FfmpegPlayer::GetPlaybackTime() const
{
    if(m_fileEnded)
        return m_decodingThread->Duration();
    return m_showingThread->GetPlayBackTime();
}

void FfmpegPlayer::GetFrameSize(int& width, int& height) const
{
    width = m_showingThread->GetCurrentFrameSize().first;
    height = m_showingThread->GetCurrentFrameSize().second;
}

void FfmpegPlayer::SendEvents()
{
    {
        ScopedLock lock(m_mutex);
        if (m_eventQueue.empty())
            return;
        m_sendingEventQueue.clear();
        m_sendingEventQueue.swap(m_eventQueue);
    }

    for (auto event : m_sendingEventQueue)
    {
        switch (event.m_type)
        {
        case FfmpegPlayerEventType::Initialized:
            m_listener->Initialized();
            break;
        case FfmpegPlayerEventType::Playing:
            if (m_reportPlay){
                m_listener->Playing();
                m_reportPlay = false;
            }
            break;
        case FfmpegPlayerEventType::Paused:
            if (!m_isLooped)
                m_listener->Paused();
            break;
        case FfmpegPlayerEventType::Stopped:
            if (!m_isLooped)
                m_listener->Stopped();
            break;
        case FfmpegPlayerEventType::SeekDone:
            if (!m_isLooped)
                m_listener->SeekDone(event.m_data);
            break;
        case FfmpegPlayerEventType::NextFrameAvailable:
            m_listener->NextFrameAvailable();
            break;
        case FfmpegPlayerEventType::FileEnded:
            if (!m_isLooped)
                m_listener->FileEnded();
            else{
                ScopedLock lock(m_mutex);
                m_taskQueue.push_back(FfmpegPlayerTask(FfmpegPlayerTaskType::Stop));
                m_taskQueue.push_back(FfmpegPlayerTask(FfmpegPlayerTaskType::Play));
            }
            break;
        case FfmpegPlayerEventType::Error:
            m_listener->Error(event.m_data);
            break;
        }
    }
}


//Internal working function
void FfmpegPlayer::WorkingThread()
{
    while (1)
    {
        m_mutex.lock();
        if (m_destroying)
        {
            m_mutex.unlock();
            return;
        }
        if (m_sendAsyncCallbacks)
        {
            m_mutex.unlock();
            SendEvents();
            m_mutex.lock();
        }

        if (m_currentTask.m_type == FfmpegPlayerTaskType::None || m_currentTask.IsDone())
        {
            if (!m_currentTask.m_reported)
            {
                switch (m_currentTask.m_type)
                {
                case FfmpegPlayerTaskType::Initialize:
                    m_eventQueue.push_back(FfmpegPlayerEvent(FfmpegPlayerEventType::Initialized, 0));
                    //m_listener->Initialized();
                    break;
                case FfmpegPlayerTaskType::Play:
                    //m_listener->Playing();
                    m_eventQueue.push_back(FfmpegPlayerEvent(FfmpegPlayerEventType::Playing, 0));
                    break;
                case FfmpegPlayerTaskType::Pause:
                    m_eventQueue.push_back(FfmpegPlayerEvent(FfmpegPlayerEventType::Paused, 0));
                    //m_listener->Paused();
                    break;
                case FfmpegPlayerTaskType::Stop:
                    //m_listener->Stopped();
                    m_eventQueue.push_back(FfmpegPlayerEvent(FfmpegPlayerEventType::Paused, 0));
                    break;
                case FfmpegPlayerTaskType::Seek:
                    m_eventQueue.push_back(FfmpegPlayerEvent(FfmpegPlayerEventType::SeekDone, m_showingThread->GetPlayBackTime()));
                    m_eventQueue.push_back(FfmpegPlayerEvent(FfmpegPlayerEventType::Paused, 0));
                    //m_listener->SeekDone(m_currentTask.m_time);
                    break;
                }
                m_currentTask.m_reported = true;
            }
            if (!m_taskQueue.empty())
            {
                m_currentTask = m_taskQueue.front();
                m_taskQueue.pop_front();
                switch (m_currentTask.m_type)
                {
                case FfmpegPlayerTaskType::Play:
                    m_decodingThread->Play();
                    break;
                case FfmpegPlayerTaskType::Pause:
                    m_decodingThread->Pause();
                    break;
                case FfmpegPlayerTaskType::Stop:
                    m_decodingThread->Stop();
                    break;
                case FfmpegPlayerTaskType::Seek:
                    m_decodingThread->Seek(m_currentTask.m_time);
                    break;
                }
            }
        }
        m_mutex.unlock();
        std::this_thread::sleep_for(std::chrono::duration<int, std::milli>(WORKING_THREAD_WAIT_TIME));
    }
}

bool FfmpegPlayer::GetAvailableFrame(uint8_t** buffer, int32_t& bufferSize)
{
    return m_showingThread->GetCurrentFrame(buffer, bufferSize);
}

void FfmpegPlayer::GetSound(uint8_t* buffer, int32_t bufferSize)
{
    m_showingThread->GetSound(buffer,bufferSize);
}
void FfmpegPlayer::GetAudioParams(int & channels, int & sampleRate, AVSampleFormat & format)
{
    m_showingThread->GetAudioParams(channels, sampleRate, format);
}



//DecodingThreadListener interface
void FfmpegPlayer::OnError(DecodingThreadErrorCode error)
{
    ScopedLock lock(m_mutex);
    m_decodingThreadPlaying = false;
    m_Ok = false;
    m_eventQueue.push_back(FfmpegPlayerEvent(FfmpegPlayerEventType::Error, DecodingThreadErrorCodeToInt(error)));
}

void FfmpegPlayer::OnFrameReady()
{
    ScopedLock lock(m_mutex);
    m_decodingThreadPlaying = true;
    if (m_currentTask.m_type == FfmpegPlayerTaskType::Play)
        m_currentTask.m_decodingThreadConfirmation = true;
}

void FfmpegPlayer::OnPaused()
{
    ScopedLock lock(m_mutex);
    m_decodingThreadPlaying = false;
    if (m_currentTask.m_type == FfmpegPlayerTaskType::Pause){
        m_currentTask.m_decodingThreadConfirmation = true;
        m_currentTask.m_showingThreadConfirmation = !m_showingThread->IsPlaying();
    }
}

void FfmpegPlayer::OnStopped()
{
    ScopedLock lock(m_mutex);
    m_decodingThreadPlaying = false;
    m_fileEnded = false;
    if (m_currentTask.m_type == FfmpegPlayerTaskType::Stop)
    {
        m_currentTask.m_decodingThreadConfirmation = true;
        if (!m_showingThread->IsPlaying())
            m_currentTask.m_showingThreadConfirmation = true;
    }
}

void FfmpegPlayer::OnSeekStart()
{
    ScopedLock lock(m_mutex);
    m_fileEnded = false;
    m_decodingThreadPlaying = false;
}

void FfmpegPlayer::OnSeekDone()
{
    ScopedLock lock(m_mutex);
    m_decodingThreadPlaying = false;
    m_fileEnded = false;
    if (m_currentTask.m_type == FfmpegPlayerTaskType::Seek)
        m_currentTask.m_decodingThreadConfirmation = true;
}

void FfmpegPlayer::OnVideoEnd()
{
    ScopedLock lock(m_mutex);
    if (m_currentTask.m_type == FfmpegPlayerTaskType::Play)
    {
        m_currentTask.m_showingThreadConfirmation = true;
        m_currentTask.m_decodingThreadConfirmation = true;
        if (m_currentTask.IsDone() && !m_currentTask.m_reported)
        {
            //m_listener->Playing();
            m_eventQueue.push_back(FfmpegPlayerEvent(FfmpegPlayerEventType::Playing, 0));
            m_currentTask.m_reported = true;
        }
    }
    m_decodingThreadPlaying = false;
    m_decodingThreadReachedEOF = true;
    m_fileEnded = true;
    if (!m_showingThread->IsPlaying()){
        m_eventQueue.push_back(FfmpegPlayerEvent(FfmpegPlayerEventType::FileEnded, 0));
        m_decodingThreadReachedEOF = false;
    }
}

void FfmpegPlayer::OnFirstFrameDone()
{
    ScopedLock lock(m_mutex);
    m_fileEnded = false;
    if (m_currentTask.m_type == FfmpegPlayerTaskType::Initialize)
    {
        m_currentTask.m_decodingThreadConfirmation = true;
    }
}

//ShowingThreadListener interface
void FfmpegPlayer::OnFirstFrameShown()
{
    ScopedLock lock(m_mutex);
    if (m_currentTask.m_type == FfmpegPlayerTaskType::Seek)
    {
        m_currentTask.m_showingThreadConfirmation = true;
        if (m_currentTask.IsDone() && !m_currentTask.m_reported)
        {
            //m_listener->SeekDone(m_currentTask.m_time);
            m_eventQueue.push_back(FfmpegPlayerEvent(FfmpegPlayerEventType::SeekDone, m_showingThread->GetPlayBackTime()));
            m_eventQueue.push_back(FfmpegPlayerEvent(FfmpegPlayerEventType::Paused, 0));
            m_currentTask.m_reported = true;
        }
    }
    if (m_currentTask.m_type == FfmpegPlayerTaskType::Initialize)
    {
        m_currentTask.m_showingThreadConfirmation = true;
        if (m_currentTask.IsDone() && !m_currentTask.m_reported)
        {
            //m_listener->Initialized();
            m_eventQueue.push_back(FfmpegPlayerEvent(FfmpegPlayerEventType::Initialized,0));
            m_currentTask.m_reported = true;
        }
    }
    //m_listener->NextFrameAvailable();
    m_eventQueue.push_back(FfmpegPlayerEvent(FfmpegPlayerEventType::NextFrameAvailable, 0));
}

void FfmpegPlayer::OnNoMoreFrames()
{
    ScopedLock lock(m_mutex);
    if (m_decodingThreadReachedEOF){
        m_eventQueue.push_back(FfmpegPlayerEvent(FfmpegPlayerEventType::FileEnded, 0));
        m_decodingThreadReachedEOF = false;
    }
    if (m_currentTask.m_type == FfmpegPlayerTaskType::Pause || m_currentTask.m_type == FfmpegPlayerTaskType::Stop)
    {
        m_currentTask.m_showingThreadConfirmation = true;
    }
    else if (!m_decodingThreadPlaying)
    {
        m_eventQueue.push_back(FfmpegPlayerEvent(FfmpegPlayerEventType::Paused, 0));
    }
}

void FfmpegPlayer::OnFrameShown()
{
    ScopedLock lock(m_mutex);
    if (m_currentTask.m_type == FfmpegPlayerTaskType::Play)
    {
        m_currentTask.m_showingThreadConfirmation = true;
        if (m_currentTask.IsDone() && !m_currentTask.m_reported)
        {
            //m_listener->Playing();
            m_eventQueue.push_back(FfmpegPlayerEvent(FfmpegPlayerEventType::Playing, 0));
            m_currentTask.m_reported = true;
        }
    }
    m_eventQueue.push_back(FfmpegPlayerEvent(FfmpegPlayerEventType::NextFrameAvailable, 0));
    //m_listener->NextFrameAvailable();
}

void FfmpegPlayer::OnShowingError()
{
    ScopedLock lock(m_mutex);
    m_decodingThread->Pause();
    m_Ok = false;
    m_eventQueue.push_back(FfmpegPlayerEvent(FfmpegPlayerEventType::Error, -3));
}

void FfmpegPlayer::OnStartPlaying()
{

}
