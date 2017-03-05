#ifndef AUDIOPACKETQUEUE_H
#define AUDIOPACKETQUEUE_H

class SmartAvPacket
{
public:
    AVPacket m_packet;
    int m_error;
    SmartAvPacket(const AVPacket* packet);
    ~SmartAvPacket();
};

class AVPacketQueue
{
private:
    std::list<SmartAvPacket*> m_queue;
    int m_sizeLimit;
    mutable std::recursive_mutex m_mutex;
public:
    AVPacketQueue(int sizeLimit = 100);
    ~AVPacketQueue();
    void PutPacket(const AVPacket* packet);
    SmartAvPacket* GetPacket();
    int GetSize() const;
    void ResetQueue();
};
#endif //AUDIOPACKETQUEUE_H