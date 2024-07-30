#pragma once
#include "Video.h"

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
    virtual IVideoDepacketizer* VDepack() = 0;
};

IVideoStream* MakeVideoStream(const int streamId);
void ReleaseVideoStream(IVideoStream* pVS);

extern IVideoStream* VideoStreamInstance;
extern IVideoStream* VideoStreamInstance2;
