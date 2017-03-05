#ifndef FRAMEQUEUEMANAGER_H
#define FRAMEQUEUEMANAGER_H

typedef std::lock_guard<std::recursive_mutex> ScopedLock;
typedef std::pair<int, int> FrameSize;

class InternalFrame
{
    AVFrame* m_frame;
    int64_t m_presentationTime;
    uint8_t * m_buffer;
    FrameSize m_frameSize;
    int32_t m_bufferSize;
    AVPixelFormat m_format;
    SwsContext *m_context;
    void FreeStuff();
public:
    InternalFrame(AVPixelFormat format);
    ~InternalFrame();
    void SaveFrame(AVFrame *frame, SwsContext* ctx, double timeBase);
    void CopyFrame(uint8_t** buffer, int32_t & bufferSize) const;
    int64_t GetPresentationTime() const
    {
        return m_presentationTime;
    }
    FrameSize GetFrameSize() const
    {
        return m_frameSize;
    }
};
class FrameQueueManager
{
    typedef std::list<InternalFrame*> FrameList;
    
    FrameList m_FullFrameList;
    FrameList m_ReadyFrames;
    FrameList m_FreeFrames;
    mutable std::recursive_mutex m_mutex;
    SwsContext *m_swsCtx;
    FrameSize m_frameSize;
    AVPixelFormat m_format;
public:
    FrameQueueManager(int frameNumberLimit, AVPixelFormat format);
    ~FrameQueueManager();
    void SaveFirstFrame(AVFrame *frame, double timeBase);
    void SaveFrame(AVFrame *frame, double timeBase);
    InternalFrame* RequestReadyFrame();
    InternalFrame* GetFirstFrame();
    void FrameShown(InternalFrame* frame);
    void ResetFrames();
    int GetFreeFramesCount()const;
    int GetReadyFramesCount()const;
};

#endif//FRAMEQUEUEMANAGER_H
