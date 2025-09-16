#pragma once
#include "Limelight-internal.h"

class IVideoDepacketizer
{
public:
    virtual void initializeVideoDepacketizer(int pktSize) = 0;
    virtual void destroyVideoDepacketizer(void) = 0;
    virtual void stopVideoDepacketizer(void) = 0;
    virtual void queueRtpPacket(PRTP_VIDEO_DECODE_CONTEXT decContext,
                                PRTPV_QUEUE_ENTRY queueEntryPtr) = 0;
    virtual void notifyFrameLost(unsigned int frameNumber, bool speculative) = 0;
    virtual bool LiWaitForNextVideoFrame(VIDEO_FRAME_HANDLE* frameHandle, PDECODE_UNIT* decodeUnit) = 0;
    virtual void LiCompleteVideoFrame(VIDEO_FRAME_HANDLE handle, int drStatus) = 0;
    virtual void LiWakeWaitForVideoFrame() = 0;
    virtual bool LiPollNextVideoFrame(VIDEO_FRAME_HANDLE* frameHandle, PDECODE_UNIT* decodeUnit) = 0;

};

IVideoDepacketizer* MakeVideoDepacketizer(const int streamId);
void ReleaseVideoDepacketizer(IVideoDepacketizer* pInstace);
