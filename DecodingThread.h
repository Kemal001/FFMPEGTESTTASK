#ifndef DECODINGTHREAD_H
#define DECODINGTHREAD_H

#include "DecodingThreadListener.h"

enum class DecodingThreadErrorCode
{
    DecodingError,
    SeekError
};

int DecodingThreadErrorCodeToInt(DecodingThreadErrorCode code);

struct buffer_data {
    uint8_t *ptr;
    const int64_t size; ///< size left in the buffer
    int64_t currentPos;
    uint8_t *currentPosPtr;
};

class AudioDecoder;
class AVPacketQueue;

class DecodingThread : public DecodingThreadListener
{

    enum class Task
    {
        create,
        play,
        pause,
        stop,
        seek
    };

    struct DecodingStuff
    {
        AVFormatContext *pFormatCtx;
        int videoStreamIndex;
        int audioStreamIndex;
        AVCodecContext *pCodecCtx;
        AVCodecContext *pAudioCodecCtx;
        int frameFinished;
        AVPacket packet;
        AVFrame *pFrame;
        AVIOContext *avio_ctx;
        uint8_t *avio_ctx_buffer;
        buffer_data * bufferData;
    };

    bool m_firstFrameDone;
    std::list<DecodingThreadListener*> m_listeners;
    std::queue<Task> m_taskQueue;
    Task m_currentTask;
    std::pair<int, int> m_frameSize;
    std::thread m_thread;
    std::recursive_mutex m_taskMutex;
    std::recursive_mutex m_eventMutex;
    mutable std::recursive_mutex m_mutex;
    AVPacketQueue *m_audioPacketQueue;
    AVPacketQueue *m_videoPacketQueue;
    AudioDecoder* m_audioDecoder;
    FrameQueueManager* m_frameQueueManager;
    DecodingStuff m_decodingStuff;
    int64_t m_currentSeekPosition;
    int64_t m_currentPTS;
    bool m_destroying;
    bool m_seekDone;
    bool m_initialized;
    bool m_reportPause;

    bool FindFirstFrame(int64_t position);
    void TakeNextTask();
    bool ReadNextPacket(bool fillBothQueues);
    void DecodeFrame();
    bool DecodeFirstFrame();
    bool SeekFrame(int64_t milliseconds);
    void Initialize();
    void InitializeDecodingStuff();
    void FreeDecodingStuff();
public:
    DecodingThread(const char* filePath, FrameQueueManager* frameQueueManager, AVPacketQueue* audioPacketQueue, AVPacketQueue* videoPacketQueue);
    DecodingThread(uint8_t* buffer, int64_t bufferSize, FrameQueueManager* frameQueueManager, AVPacketQueue* audioPacketQueue, AVPacketQueue* videoPacketQueue);
    ~DecodingThread();
    void ThreadFunc();
    bool InitializedSuccessful()const { return m_initialized; }
    void Start();
    void AddListener(DecodingThreadListener* listener);
    void RemoveListener(DecodingThreadListener* listener);
    void Play();
    void Pause();
    void Stop();
    void Seek(int64_t timeInMilliseconds);
    double CurrentTimeBaseSeconds() const;
    int64_t Duration() const;
    AudioDecoder* GetAudioDecoder() const { return m_audioDecoder; }

    //DecodingThreadListener interface
    void OnError(DecodingThreadErrorCode error);
    void OnFrameReady();
    void OnPaused();
    void OnStopped();
    void OnSeekStart();
    void OnSeekDone();
    void OnVideoEnd();
    void OnFirstFrameDone();
};

#endif//DECODINGTHREAD_H
