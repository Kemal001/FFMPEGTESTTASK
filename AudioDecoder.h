#ifndef AUDIODECODER_H
#define AUDIODECODER_H

#include "AVPacketQueue.h"

class AudioDecoder
{
    AVPacketQueue* m_packetQueue;
    SmartAvPacket*  m_currentPacket;
    int64_t m_audioClock; //milliseconds
    int m_leftPacketSize;
    AVFrame *m_pFrame;
    AVCodecContext* m_CodecContext;
    AVStream* m_audioStream;
    std::recursive_mutex m_mutex;
public:
    AudioDecoder(AVCodecContext * audioCodecContext, AVPacketQueue * packetQueue, AVStream* audioStream);
    ~AudioDecoder();
    int GetNextFrameData(uint8_t *audio_buf, int buf_size, int64_t & framePts);
    void Reset();
    int GetSampleRate() const;
    int GetSampleSizeBytes() const;
    int GetNumberOfChannels() const;
    AVSampleFormat GetSampleFormat() const;
};

#endif //AUDIODECODER_H