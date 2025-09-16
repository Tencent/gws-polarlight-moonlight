#pragma once
#include "Video.h"
#include <mutex>
#include <atomic>
#include <shared_mutex>

class IVideoDepacketizer;
class IVideoStream
{
public:
    virtual void initializeVideoStream(void) = 0;
    virtual int startVideoStream(void* rendererContext, int drFlags) = 0;
    virtual void stopVideoStream(void) = 0;
    virtual void destroyVideoStream(void) = 0;
    virtual void notifyKeyFrameReceived(void) = 0;
    virtual unsigned short GetPortNumber() = 0;
    virtual void SetPortNumber(unsigned short port) = 0;
    virtual void UpdatePingPayload(char* strPayload) = 0;
    virtual void CopyPingPayload(const char* strPayload) = 0;
    virtual const char* GetVideoPingPayload() = 0;
    virtual IVideoDepacketizer* VDepack() = 0;
    std::atomic_bool m_bInit = false;
    std::atomic_bool m_bStart = false;
};

IVideoStream* MakeVideoStream(const int streamId);
void ReleaseVideoStream(IVideoStream* pVS);

extern IVideoStream* VideoStreamInstance;
extern IVideoStream* VideoStreamInstance2;
IVideoStream* getVideoStreamInstance(int streamId, std::shared_lock<std::shared_mutex>& lock);
IVideoStream* getVideoStreamInstance(int streamId, std::unique_lock<std::shared_mutex>& lock);
