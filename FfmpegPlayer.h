#ifndef FFMPEGPLAYER_H
#define FFMPEGPLAYER_H

#include "DecodingThreadListener.h"
#include "ShowingThreadListener.h"

class FfmpegPlayerListener
{
public:
    virtual void Stopped() = 0;
    virtual void Paused() = 0;
    virtual void Playing() = 0;
    virtual void Initialized() = 0;
    virtual void SeekDone(int64_t timeMilliceconds) = 0;
    virtual void NextFrameAvailable() = 0;
    virtual void FileEnded() = 0;
    virtual void Error(int64_t errorCode) = 0;
};

enum class FfmpegPlayerTaskType
{
    Initialize,
    Play,
    Pause,
    Stop,
    Seek,
    None
};

class FfmpegPlayerTask
{
public:
    FfmpegPlayerTaskType m_type;
    bool m_decodingThreadConfirmation;
    bool m_showingThreadConfirmation;
    bool m_reported;
    int64_t m_time;

    FfmpegPlayerTask(FfmpegPlayerTaskType type) : m_type(type),
        m_decodingThreadConfirmation(false),
        m_showingThreadConfirmation(false),
        m_reported(false),
        m_time(0){}

    FfmpegPlayerTask(const FfmpegPlayerTask & task) : m_type(task.m_type),
        m_decodingThreadConfirmation(task.m_decodingThreadConfirmation),
        m_showingThreadConfirmation(task.m_showingThreadConfirmation),
        m_reported(task.m_reported),
        m_time(task.m_time){}

    ~FfmpegPlayerTask(){}

    bool IsDone(){ return m_decodingThreadConfirmation && m_showingThreadConfirmation; }
};

enum class FfmpegPlayerEventType
{
    Initialized,
    Playing,
    Paused,
    Stopped,
    SeekDone,
    NextFrameAvailable,
    FileEnded,
    Error
};

class FfmpegPlayerEvent
{
public:
    FfmpegPlayerEventType m_type;
    int64_t m_data;
    FfmpegPlayerEvent(FfmpegPlayerEventType type, int64_t data) : m_type(type), m_data(data) {}
    FfmpegPlayerEvent(const FfmpegPlayerEvent & event) : m_type(event.m_type), m_data(event.m_data) {}
    ~FfmpegPlayerEvent(){}
};

class FrameQueueManager;
class DecodingThread;
class ShowingThread;
class AudioDecoder;
class AVPacketQueue;

class FfmpegPlayer : public DecodingThreadListener, public ShowingThreadListener
{

    static std::recursive_mutex s_globalContextGuard;
    static bool s_commonInitialized;
    std::thread m_workingThread;
    std::recursive_mutex m_mutex;
    DecodingThread *m_decodingThread;
    ShowingThread *m_showingThread;
    AVPacketQueue *m_audioPacketQueue;
    AVPacketQueue *m_videoPacketQueue;
    FrameQueueManager *m_frameQueueManager;
    FfmpegPlayerListener *m_listener;
    FfmpegPlayerTask m_currentTask;
    std::list<FfmpegPlayerTask> m_taskQueue;
    std::list<FfmpegPlayerEvent> m_eventQueue;
    std::list<FfmpegPlayerEvent> m_sendingEventQueue;
    bool m_Ok;
    bool m_decodingThreadPlaying;
    bool m_destroying;
    bool m_sendAsyncCallbacks;
    bool m_isLooped;
    bool m_decodingThreadReachedEOF;
    bool m_reportPlay;
    bool m_fileEnded;

public:
    //External Interface to interact with player.
    FfmpegPlayer(bool sendAsyncCallbacks = true);
    ~FfmpegPlayer();
    bool Initialize(const char* filePath, FfmpegPlayerListener* listener, AVPixelFormat format = PIX_FMT_RGBA);
    bool Initialize(uint8_t* buffer, int64_t bufferSize, FfmpegPlayerListener* listener, AVPixelFormat format = PIX_FMT_RGBA);
    void Stop();
    void Pause();
    void Play(bool loop = false);
    void Seek(int64_t timeMilliceconds);
    int64_t GetDuration() const;
    int64_t GetPlaybackTime() const;
    void GetFrameSize(int& width, int& height) const;
    bool GetAvailableFrame(uint8_t** buffer, int32_t& bufferSize);
    void GetSound(uint8_t* buffer, int32_t bufferSize);
    void GetAudioParams(int & channels, int & sampleRate, AVSampleFormat & format);
    void SendEvents();

    //Internal working function
    void WorkingThread();

    //DecodingThreadListener interface
    void OnError(DecodingThreadErrorCode error);
    void OnFrameReady();
    void OnPaused();
    void OnStopped();
    void OnSeekStart();
    void OnSeekDone();
    void OnVideoEnd();
    void OnFirstFrameDone();
    //ShowingThreadListener interface
    void OnFirstFrameShown();
    void OnNoMoreFrames();
    void OnFrameShown();
    void OnShowingError();
    void OnStartPlaying();
};

#endif//FFMPEGPLAYER_H
