#ifndef SHOWINGTHREAD_H
#define SHOWINGTHREAD_H

#include "ShowingThreadListener.h"

#define MAX_AUDIO_FRAME_SIZE 192000

class ShowingThread : public ShowingThreadListener, public DecodingThreadListener
{
    std::thread m_thread;
    std::recursive_mutex m_mutex;
    mutable std::recursive_mutex m_frameMutex;
    std::list<ShowingThreadListener*> m_listeners;
    bool m_isPlaying;
    bool m_decodingThreadPaused;
    bool m_frameReady;
    bool m_destroying;
    bool m_showFirstFrame;
    bool m_isSeeking;
    int64_t m_videoStartTime;
    int64_t m_playBackTime;
    std::chrono::system_clock::time_point m_lastFrameTimeStamp;
    std::chrono::steady_clock::time_point m_startTime;
    InternalFrame *m_currentFrame;
    DecodingThread *m_decodingThread;
    FrameQueueManager *m_frameQueueManager;
    AudioDecoder* m_audioDecoder;
    FrameSize m_currentFrameSize;
    uint8_t m_audioBuffer[MAX_AUDIO_FRAME_SIZE * 3 / 2];
    uint8_t* m_audioBufferPos;
    uint8_t* m_audioBufferNpos;
    int64_t m_audioBufferPTS;

    void ResetSound();


public:
    ShowingThread(DecodingThread* decodingThread, FrameQueueManager *frameQueueManager, AudioDecoder* audioDecoder);
    ~ShowingThread();
    void ThreadFunction();
    void Start();
    
    void AddListener(ShowingThreadListener *listener);
    void RemoveListener(ShowingThreadListener *listener);
    bool GetCurrentFrame(uint8_t** buffer, int32_t& bufferSize) const;
    FrameSize GetCurrentFrameSize() const;
    int64_t GetPlayBackTime() const;
    int64_t GetPresizePlayBackTime() const;
    void GetSound(uint8_t* buffer, int32_t bufferSize);
    void GetAudioParams(int & channels, int & sampleRate, AVSampleFormat & format);
    bool IsPlaying() const { return m_isPlaying; };
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

#endif//SHOWINGTHREAD_H
