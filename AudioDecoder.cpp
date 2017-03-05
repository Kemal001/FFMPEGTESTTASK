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
#include "DecodingThread.h"
#include "AudioDecoder.h"

AudioDecoder::AudioDecoder(AVCodecContext * audioCodecContext, AVPacketQueue * packetQueue, AVStream* audioStream) :
m_packetQueue(packetQueue),
m_currentPacket(NULL),
m_audioClock(0),
m_leftPacketSize(0),
m_pFrame(NULL),
m_CodecContext(audioCodecContext),
m_audioStream(audioStream)
{
    m_pFrame = av_frame_alloc();
}

AudioDecoder::~AudioDecoder()
{
    m_mutex.lock();
    if (m_currentPacket != NULL){
        delete m_currentPacket;
        m_currentPacket = NULL;
    }
   
    av_free(m_pFrame);
    m_mutex.unlock();
}

bool FrameDataForChannelsAvailable(uint8_t ** data, int channels)
{
    for (int i = 0; i < channels; ++i){
        if (data[i] == NULL)
            return false;
    }
    return true;
}

int AudioDecoder::GetNextFrameData(uint8_t *audio_buf, int buf_size, int64_t & framePts)
{
    if (!m_CodecContext){
        framePts = 0;
        return 0;
    }

    while (1){
        ScopedLock lock(m_mutex);
        while (m_leftPacketSize > 0){
            int gotFrame = 0;
            int len = avcodec_decode_audio4(m_CodecContext, m_pFrame, &gotFrame, &m_currentPacket->m_packet);
            if (len < 0){
                m_leftPacketSize = 0;
                break;
            }
            m_leftPacketSize -= len;
            if (gotFrame){
                int32_t sampleSize = av_samples_get_buffer_size(NULL, 1, 1, m_CodecContext->sample_fmt, 1);
                if (sampleSize * m_CodecContext->channels * m_pFrame->nb_samples > m_pFrame->linesize[0] ||
                    (GetSampleFormat() > AV_SAMPLE_FMT_DBL && FrameDataForChannelsAvailable(m_pFrame->data, m_CodecContext->channels)))
                {
                    for (int32_t sample = 0; sample < m_pFrame->nb_samples; ++sample){
                        for (int32_t channel = 0; channel < m_CodecContext->channels; ++channel){
                            memcpy(audio_buf, m_pFrame->data[channel], sampleSize);
                            m_pFrame->data[channel] += sampleSize;
                            audio_buf += sampleSize;
                        }
                    }
                }
                else{
                    memcpy(audio_buf, m_pFrame->data[0], sampleSize * m_pFrame->nb_samples * m_CodecContext->channels);
                }
                framePts = m_audioClock;
                m_audioClock += m_pFrame->nb_samples * 1000 / m_CodecContext->sample_rate;
                return sampleSize * m_pFrame->nb_samples * m_CodecContext->channels;
            }
        }
        if (m_currentPacket){
            delete m_currentPacket;
            m_currentPacket = NULL;
        }
        m_leftPacketSize = 0;
        m_currentPacket = m_packetQueue->GetPacket();
        if (!m_currentPacket){
            return 0;
        }
        m_leftPacketSize = m_currentPacket->m_packet.size;
        if (m_currentPacket->m_packet.pts != AV_NOPTS_VALUE){
            m_audioClock = av_q2d(m_audioStream->time_base) * 1000 * m_currentPacket->m_packet.pts;
        }
    }
}

void AudioDecoder::Reset()
{
    if (!m_CodecContext){
        return;
    }
    ScopedLock lock(m_mutex);
    avcodec_flush_buffers(m_CodecContext);
    av_free(m_pFrame);
    m_pFrame = av_frame_alloc();
    if (m_currentPacket != NULL){
        delete m_currentPacket;
        m_currentPacket = NULL;
    }
    m_audioClock = 0;
    m_leftPacketSize = 0;
}

int AudioDecoder::GetSampleRate() const
{
    return m_CodecContext ? m_CodecContext->sample_rate : 0;
}

int AudioDecoder::GetSampleSizeBytes() const
{
    return m_CodecContext ? av_samples_get_buffer_size(NULL, m_CodecContext->channels, 1, m_CodecContext->sample_fmt, 1) : 2;
}

int AudioDecoder::GetNumberOfChannels() const
{
    return m_CodecContext ? m_CodecContext->channels : 0;
}

AVSampleFormat AudioDecoder::GetSampleFormat() const
{
    return m_CodecContext ? m_CodecContext->sample_fmt : AV_SAMPLE_FMT_S16;
}