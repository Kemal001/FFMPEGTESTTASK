#ifndef SHOWING_THREAD_LISTENER_H
#define SHOWING_THREAD_LISTENER_H

class ShowingThreadListener
{
public:
    virtual void OnFirstFrameShown() = 0;
    virtual void OnNoMoreFrames() = 0;
    virtual void OnFrameShown() = 0;
    virtual void OnShowingError() = 0;
    virtual void OnStartPlaying() = 0;
};

#endif