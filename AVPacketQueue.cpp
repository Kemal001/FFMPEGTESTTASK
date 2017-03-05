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

SmartAvPacket::SmartAvPacket(const AVPacket* packet) : m_error(-1)
{
    m_error = av_copy_packet(&m_packet, packet);
}

SmartAvPacket::~SmartAvPacket()
{
    if (!m_error){
        av_free_packet(&m_packet);
    }
}

AVPacketQueue::AVPacketQueue(int sizeLimit /*= 100*/) : m_sizeLimit(sizeLimit)
{

}

AVPacketQueue::~AVPacketQueue()
{
    ResetQueue();
}

void AVPacketQueue::PutPacket(const AVPacket* packet)
{
    SmartAvPacket *newPack = new SmartAvPacket(packet);
    if (!newPack->m_error){
        m_mutex.lock();
        m_queue.push_back(newPack);
        if (m_queue.size() > m_sizeLimit){
            delete m_queue.front();
            m_queue.pop_front();
        }
        m_mutex.unlock();
    }
}

SmartAvPacket* AVPacketQueue::GetPacket()
{
    m_mutex.lock();
    SmartAvPacket* ret = NULL;
    if (!m_queue.empty()){
        ret = m_queue.front();
        m_queue.pop_front();
    }
    m_mutex.unlock();
    return ret;
}

void AVPacketQueue::ResetQueue()
{
    m_mutex.lock();
    for (auto pack : m_queue){
        delete pack;
    }
    m_queue.clear();
    m_mutex.unlock();
}

int AVPacketQueue::GetSize() const
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    return m_queue.size(); 
}