#ifndef DECODING_THREAD_LISTENER_H
#define DECODING_THREAD_LISTENER_H

enum class DecodingThreadErrorCode;

class DecodingThreadListener
{
public:
    virtual void OnError(DecodingThreadErrorCode error) = 0;
    virtual void OnFrameReady() = 0;
    virtual void OnPaused() = 0;
    virtual void OnStopped() = 0;
    virtual void OnSeekDone() = 0;
    virtual void OnVideoEnd() = 0;
    virtual void OnFirstFrameDone() = 0;
    virtual void OnSeekStart() = 0;
};

#endif//DECODING_THREAD_LISTENER_H
