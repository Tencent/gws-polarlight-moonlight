#include "VideoStream.h"
#include "Limelight-internal.h"
#include "VideoDepacketizer.h"
#include <vector>
#include <shared_mutex>

#include "../app/streaming/share_data.h"

#include "openssl/rsa.h"
#include "openssl/pem.h"
#include"openssl/md5.h"
#include"openssl/sha.h"
#include"openssl/aes.h"
#define FIRST_FRAME_MAX 1500
#define FIRST_FRAME_TIMEOUT_SEC 120

#define FIRST_FRAME_PORT 47996

#define RTP_RECV_BUFFER (512 * 1024)

bool aes_decryptEx(char * secert,int len, const char* key, std::vector<char>& out)
{
    if(len <= 0)
    {
        return false;
    }
    int length = len;
    out.resize(length);
    AES_KEY deckey;
    unsigned char ivec[16] = "1234567890ABCDE";
    ivec[15] = 'F';

    AES_set_decrypt_key((const unsigned char*)key, 128, &deckey); // 还是128位
    AES_cbc_encrypt((const unsigned char*)secert, (unsigned char*) out.data(), length, &deckey, ivec, AES_DECRYPT);
    return true;
}

void queueRtpPacket(int streamId, PRTP_VIDEO_DECODE_CONTEXT decContext, PRTPV_QUEUE_ENTRY queueEntry) {
    std::shared_lock<std::shared_mutex> lock;
    IVideoStream* pVS = getVideoStreamInstance(streamId, lock);
    if (pVS && pVS->VDepack() /*&& pVS->m_bInit.load()*/) {
        pVS->VDepack()->queueRtpPacket(decContext, queueEntry);
    }
}

void notifyFrameLost(int streamId, unsigned int frameNumber, bool speculative) {
    std::shared_lock<std::shared_mutex> lock;
    IVideoStream* pVS = getVideoStreamInstance(streamId, lock);
    if (pVS && pVS->VDepack() /*&& pVS->m_bInit.load()*/) {
        pVS->VDepack()->notifyFrameLost(frameNumber, speculative);
    }
}

bool LiWaitForNextVideoFrame(int streamId, VIDEO_FRAME_HANDLE* frameHandle, PDECODE_UNIT* decodeUnit) {
    std::shared_lock<std::shared_mutex> lock;
    IVideoStream* pVS = getVideoStreamInstance(streamId, lock);
    return (pVS && pVS->VDepack() && pVS->m_bInit.load()) ?
            pVS->VDepack()->LiWaitForNextVideoFrame(frameHandle, decodeUnit) : false;
}

bool LiPollNextVideoFrame(int streamId, VIDEO_FRAME_HANDLE* frameHandle, PDECODE_UNIT* decodeUnit) {
    std::shared_lock<std::shared_mutex> lock;
    IVideoStream* pVS = getVideoStreamInstance(streamId, lock);
    return (pVS && pVS->VDepack() && pVS->m_bInit.load()) ?
            pVS->VDepack()->LiPollNextVideoFrame(frameHandle, decodeUnit) : false;
}

void LiWakeWaitForVideoFrame(int streamId) {
    std::shared_lock<std::shared_mutex> lock;
    IVideoStream* pVS = getVideoStreamInstance(streamId, lock);
    if (pVS && pVS->VDepack() && pVS->m_bInit.load()) {
        pVS->VDepack()->LiWakeWaitForVideoFrame();
    }
}

void LiCompleteVideoFrame(int streamId, VIDEO_FRAME_HANDLE handle, int drStatus) {
    std::shared_lock<std::shared_mutex> lock;
    IVideoStream* pVS = getVideoStreamInstance(streamId, lock);
    if (pVS && pVS->VDepack() /*&& pVS->m_bInit.load()*/) {
        pVS->VDepack()->LiCompleteVideoFrame(handle, drStatus);
    }
}

LINKED_BLOCKING_QUEUE* g_decodeUnitQueue = nullptr;
class VideoStream:
    public IVideoStream
{
    int streamId = 1;
    uint16_t PortNumber = 0;
    SS_PING VideoPingPayload;

    RTP_VIDEO_QUEUE rtpQueue;

    SOCKET rtpSocket = INVALID_SOCKET;
    SOCKET firstFrameSocket = INVALID_SOCKET;

    PLT_THREAD udpPingThread;
    PLT_THREAD receiveThread;
    PLT_THREAD decoderThread;

    bool receivedDataFromPeer;
    uint64_t firstDataTimeMs;
    bool receivedFullFrame;

    //for decoding
    IVideoDepacketizer* videoDepacketizer = nullptr;

    // We can't request an IDR frame until the depacketizer knows
    // that a packet was lost. This timeout bounds the time that
    // the RTP queue will wait for missing/reordered packets.
#define RTP_QUEUE_DELAY 10

    inline virtual unsigned short GetPortNumber() {
        return PortNumber;
    }
    inline virtual void SetPortNumber(unsigned short port) {
        PortNumber = port;
    }
// Initialize the video stream
    virtual void initializeVideoStream(void) override {
        videoDepacketizer->initializeVideoDepacketizer(StreamConfig.packetSize);
        RtpvInitializeQueue(&rtpQueue);
        receivedDataFromPeer = false;
        firstDataTimeMs = 0;
        receivedFullFrame = false;
        m_bInit.store(true);
    }

    // Clean up the video stream
    void destroyVideoStream(void) {
        bool expected = true;
        if (m_bInit.compare_exchange_strong(expected, false))
        {
            videoDepacketizer->destroyVideoDepacketizer();
            RtpvCleanupQueue(&rtpQueue);
        }
    }
    static void VideoPingThreadProc(void* context){
        VideoStream* pThis = (VideoStream*)context;
        pThis->VideoPingThread();
    }
    static void VideoReceiveThreadProc(void* context){
        VideoStream* pThis = (VideoStream*)context;
        pThis->VideoReceiveThread();
    }
    static void VideoDecoderThreadProc(void* context) {
        VideoStream* pThis = (VideoStream*)context;
        Limelog("VideoDecoderThreadProc pThis->VideoDecoderThread = %d = \n", pThis);
        pThis->VideoDecoderThread();
    }
public:
    VideoStream(int id) {
        streamId = id;
        videoDepacketizer = MakeVideoDepacketizer(id);
    }
    ~VideoStream()
    {
        // stopVideoStream();
        // destroyVideoStream();
        // ReleaseVideoStream();
        ReleaseVideoDepacketizer(videoDepacketizer);
    }
    int getStreamId() { return streamId; }
    const char* GetVideoPingPayload() override
    {
        return VideoPingPayload.payload;
    }
    virtual IVideoDepacketizer* VDepack() override
    {
        return videoDepacketizer;
    }
    // UDP Ping proc
    void VideoPingThread() {
        char legacyPingData[] = { 0x50, 0x49, 0x4E, 0x47 };
        LC_SOCKADDR saddr;

        LC_ASSERT(PortNumber != 0);
        if (streamId == 2)
        {//used to debug with break point set
            int x = 2;
            streamId = 2;
        }
        memcpy(&saddr, &RemoteAddr, sizeof(saddr));
        SET_PORT(&saddr, PortNumber);

        // We do not check for errors here. Socket errors will be handled
        // on the read-side in ReceiveThreadProc(). This avoids potential
        // issues related to receiving ICMP port unreachable messages due
        // to sending a packet prior to the host PC binding to that port.
        int pingCount = 0;
        while (!PltIsThreadInterrupted(&udpPingThread)) {
            if (VideoPingPayload.payload[0] != 0) {
                pingCount++;
                VideoPingPayload.sequenceNumber = BE32(pingCount);

                sendto(rtpSocket, (char*)&VideoPingPayload, sizeof(VideoPingPayload), 0, (struct sockaddr*)&saddr, RemoteAddrLen);
            }
            else {
                sendto(rtpSocket, legacyPingData, sizeof(legacyPingData), 0, (struct sockaddr*)&saddr, RemoteAddrLen);
            }

            PltSleepMsInterruptible(&udpPingThread, 500);
        }
        Limelog("End VideoPingThread streamId = %d\n", streamId);
    }

    // Receive thread proc
    void VideoReceiveThread() {
        int err;
        int bufferSize, receiveSize;
        char* buffer;
        int queueStatus;
        bool useSelect;
        int waitingForVideoMs;
        PRTP_VIDEO_DECODE_CONTEXT decContext;
        decContext = (RTP_VIDEO_DECODE_CONTEXT*)malloc(sizeof(RTP_VIDEO_DECODE_CONTEXT));
        decContext->streamId = 0;
        receiveSize = StreamConfig.packetSize + MAX_RTP_HEADER_SIZE;
        if (receiveSize % 16 != 0)
        {
            receiveSize = ((receiveSize / 16) + 1) * 16;
        }

        receiveSize = ((receiveSize + 8) / 16) * 16;  //recv data max size
        bufferSize = receiveSize + sizeof(RTPV_QUEUE_ENTRY);
        buffer = NULL;

        if (setNonFatalRecvTimeoutMs(rtpSocket, UDP_RECV_POLL_TIMEOUT_MS) < 0) {
            // SO_RCVTIMEO failed, so use select() to wait
            useSelect = true;
        }
        else {
            // SO_RCVTIMEO timeout set for recv()
            useSelect = false;
        }
        Limelog("VideoReceiveThread Begin\n");
        waitingForVideoMs = 0;
        while (!PltIsThreadInterrupted(&receiveThread)) {
            PRTP_PACKET packet;

            if (buffer == NULL) {
                buffer = (char*)malloc(bufferSize);
                if (buffer == NULL) {
                    Limelog("Video Receive: malloc() failed\n");
                    ListenerCallbacks.connectionTerminated(-1);
                    return;
                }
            }
            err = recvUdpSocket(rtpSocket, buffer, receiveSize, useSelect);

            if(g_Encrypted_AES_Key.size() > 0 && err > 0)
            {
                std::vector<char> outvec;
                aes_decryptEx(buffer, receiveSize, g_Encrypted_AES_Key.data(), outvec);
                memcpy(buffer, outvec.data(), receiveSize);
            }
            // if (streamId == 2 && err == 0)
            // {
            //     Limelog("recvUdpSocket bufferSize =  %d   err = %d\n", bufferSize, err);
            //     Limelog("recvUdpSocket receiveSize =  %d\n", receiveSize);
            // }
            if (err < 0) {
                Limelog("Video Receive: recvUdpSocket() failed: %d\n", (int)LastSocketError());
                ListenerCallbacks.connectionTerminated(LastSocketFail());
                break;
            }
            else if (err == 0) {
                if (!receivedDataFromPeer) {
                    // If we wait many seconds without ever receiving a video packet,
                    // assume something is broken and terminate the connection.
                    waitingForVideoMs += UDP_RECV_POLL_TIMEOUT_MS;
                    if (waitingForVideoMs >= FIRST_FRAME_TIMEOUT_SEC * 1000) {
                        Limelog("Terminating connection due to lack of video traffic\n");
                        ListenerCallbacks.connectionTerminated(ML_ERROR_NO_VIDEO_TRAFFIC);
                        break;
                    }
                }

                // Receive timed out; try again
                continue;
            }

            if (!receivedDataFromPeer) {
                receivedDataFromPeer = true;
                Limelog("Received first video packet after %d ms\n", waitingForVideoMs);

                firstDataTimeMs = PltGetMillis();
            }

            if (!receivedFullFrame) {
                uint64_t now = PltGetMillis();

                if ((now - firstDataTimeMs) >= (FIRST_FRAME_TIMEOUT_SEC * 100000)) {
                    Limelog("Terminating connection due to lack of a successful video frame\n");
                    ListenerCallbacks.connectionTerminated(ML_ERROR_NO_VIDEO_FRAME);
                    break;
                }
            }
            if (streamId == 2)
            {//used to debug with break point set
                int x = 2;
                streamId = 2;
            }
            // Convert fields to host byte-order
            packet = (PRTP_PACKET)&buffer[0];
            packet->sequenceNumber = BE16(packet->sequenceNumber);
            packet->timestamp = BE32(packet->timestamp);
            packet->ssrc = BE32(packet->ssrc);

            queueStatus = RtpvAddPacket(streamId,decContext, &rtpQueue, packet, err, (PRTPV_QUEUE_ENTRY)&buffer[receiveSize]);

            if (queueStatus == RTPF_RET_QUEUED) {
                // The queue owns the buffer
                buffer = NULL;
            }
        }
        Limelog("VideoReceiveThread End\n");
        if (buffer != NULL) {
            free(buffer);
        }
        if (decContext != NULL) {
            free(decContext);
        }
    }
    virtual void UpdatePingPayload(char* strPayload) override {
        memset(&VideoPingPayload, 0, sizeof(VideoPingPayload));
        if (strPayload != NULL && strlen(strPayload) == sizeof(VideoPingPayload.payload)) {
            memcpy(VideoPingPayload.payload, strPayload, sizeof(VideoPingPayload.payload));
        }
    }
    //strPayload need to be sizeof(VideoPingPayload.payload)
    virtual void CopyPingPayload(const char* strPayload) override {
        memset(&VideoPingPayload, 0, sizeof(VideoPingPayload));
        memcpy(VideoPingPayload.payload, strPayload, sizeof(VideoPingPayload.payload));
    }
    virtual void notifyKeyFrameReceived(void) override {
        // Remember that we got a full frame successfully
        receivedFullFrame = true;
    }

    // Decoder thread proc
    void VideoDecoderThread() {
        while (!PltIsThreadInterrupted(&decoderThread)) {
            VIDEO_FRAME_HANDLE frameHandle;
            PDECODE_UNIT decodeUnit;

            if (!videoDepacketizer->LiWaitForNextVideoFrame(&frameHandle, &decodeUnit)) {
                return;
            }

            videoDepacketizer->LiCompleteVideoFrame(frameHandle, VideoCallbacks.submitDecodeUnit(decodeUnit));
        }
    }

    // Read the first frame of the video stream
    int readFirstFrame(void) {
        // All that matters is that we close this socket.
        // This starts the flow of video on Gen 3 servers.

        closeSocket(firstFrameSocket);
        firstFrameSocket = INVALID_SOCKET;

        return 0;
    }

    // Terminate the video stream
    virtual void stopVideoStream(void) override {
        if (!m_bStart.load())
            return;
        if (!receivedDataFromPeer) {
            Limelog("No video traffic was ever received from the host!\n");
        }

        VideoCallbacks.stop();

        // Wake up client code that may be waiting on the decode unit queue
        if (m_bInit.load())
            videoDepacketizer->stopVideoDepacketizer();

        PltInterruptThread(&udpPingThread);
        PltInterruptThread(&receiveThread);
        if ((VideoCallbacks.capabilities & (CAPABILITY_DIRECT_SUBMIT | CAPABILITY_PULL_RENDERER)) == 0) {
            PltInterruptThread(&decoderThread);
        }

        if (firstFrameSocket != INVALID_SOCKET) {
            shutdownTcpSocket(firstFrameSocket);
        }

        PltJoinThread(&udpPingThread);
        PltJoinThread(&receiveThread);

        if ((VideoCallbacks.capabilities & (CAPABILITY_DIRECT_SUBMIT | CAPABILITY_PULL_RENDERER)) == 0)
        {
            PltJoinThread(&decoderThread);
        }
        else
        {
        }

        PltCloseThread(&udpPingThread);
        PltCloseThread(&receiveThread);
        if ((VideoCallbacks.capabilities & (CAPABILITY_DIRECT_SUBMIT | CAPABILITY_PULL_RENDERER)) == 0) {
            PltCloseThread(&decoderThread);
        }

        if (firstFrameSocket != INVALID_SOCKET) {
            closeSocket(firstFrameSocket);
            firstFrameSocket = INVALID_SOCKET;
        }
        if (rtpSocket != INVALID_SOCKET) {
            closeSocket(rtpSocket);
            rtpSocket = INVALID_SOCKET;
        }

        VideoCallbacks.cleanup();
        m_bStart.store(false);
    }

    // Start the video stream
    virtual int startVideoStream(void* rendererContext, int drFlags) override {
        int err;

        firstFrameSocket = INVALID_SOCKET;

        // This must be called before the decoder thread starts submitting
        // decode units
        LC_ASSERT(NegotiatedVideoFormat != 0);
        err = VideoCallbacks.setup(NegotiatedVideoFormat, StreamConfig.width,
            StreamConfig.height, StreamConfig.fps, rendererContext, drFlags);
        if (err != 0) {
            return err;
        }

        rtpSocket = bindUdpSocket(RemoteAddr.ss_family, RTP_RECV_BUFFER);
        if (rtpSocket == INVALID_SOCKET) {
            VideoCallbacks.cleanup();
            return LastSocketError();
        }

        VideoCallbacks.start();

        err = PltCreateThread("VideoRecv", VideoReceiveThreadProc, this, &receiveThread);
        if (err != 0) {
            VideoCallbacks.stop();
            closeSocket(rtpSocket);
            VideoCallbacks.cleanup();
            return err;
        }
        Limelog("startVideoStream VideoCallbacks.capabilities =  %d \n", VideoCallbacks.capabilities);
        if ((VideoCallbacks.capabilities & (CAPABILITY_DIRECT_SUBMIT | CAPABILITY_PULL_RENDERER)) == 0) {
            Limelog("PltCreateThread %d = \n", VideoCallbacks.capabilities);
            err = PltCreateThread("VideoDec", VideoDecoderThreadProc, NULL, &decoderThread);
            if (err != 0) {
                VideoCallbacks.stop();
                PltInterruptThread(&receiveThread);
                PltJoinThread(&receiveThread);
                PltCloseThread(&receiveThread);
                closeSocket(rtpSocket);
                VideoCallbacks.cleanup();
                return err;
            }
        }

        if (AppVersionQuad[0] == 3) {
            // Connect this socket to open port 47998 for our ping thread
            firstFrameSocket = connectTcpSocket(&RemoteAddr, RemoteAddrLen,
                FIRST_FRAME_PORT, FIRST_FRAME_TIMEOUT_SEC);
            if (firstFrameSocket == INVALID_SOCKET) {
                VideoCallbacks.stop();
                videoDepacketizer->stopVideoDepacketizer();
                PltInterruptThread(&receiveThread);
                if ((VideoCallbacks.capabilities & (CAPABILITY_DIRECT_SUBMIT | CAPABILITY_PULL_RENDERER)) == 0) {
                    PltInterruptThread(&decoderThread);
                }
                PltJoinThread(&receiveThread);
                if ((VideoCallbacks.capabilities & (CAPABILITY_DIRECT_SUBMIT | CAPABILITY_PULL_RENDERER)) == 0) {
                    PltJoinThread(&decoderThread);
                }
                PltCloseThread(&receiveThread);
                if ((VideoCallbacks.capabilities & (CAPABILITY_DIRECT_SUBMIT | CAPABILITY_PULL_RENDERER)) == 0) {
                    PltCloseThread(&decoderThread);
                }
                closeSocket(rtpSocket);
                VideoCallbacks.cleanup();
                return LastSocketError();
            }
        }

        // Start pinging before reading the first frame so GFE knows where
        // to send UDP data
        err = PltCreateThread("VideoPing", VideoPingThreadProc, this, &udpPingThread);
        if (err != 0) {
            VideoCallbacks.stop();
            videoDepacketizer->stopVideoDepacketizer();
            PltInterruptThread(&receiveThread);
            if ((VideoCallbacks.capabilities & (CAPABILITY_DIRECT_SUBMIT | CAPABILITY_PULL_RENDERER)) == 0) {
                PltInterruptThread(&decoderThread);
            }
            PltJoinThread(&receiveThread);
            if ((VideoCallbacks.capabilities & (CAPABILITY_DIRECT_SUBMIT | CAPABILITY_PULL_RENDERER)) == 0) {
                PltJoinThread(&decoderThread);
            }
            PltCloseThread(&receiveThread);
            if ((VideoCallbacks.capabilities & (CAPABILITY_DIRECT_SUBMIT | CAPABILITY_PULL_RENDERER)) == 0) {
                PltCloseThread(&decoderThread);
            }
            closeSocket(rtpSocket);
            if (firstFrameSocket != INVALID_SOCKET) {
                closeSocket(firstFrameSocket);
                firstFrameSocket = INVALID_SOCKET;
            }
            VideoCallbacks.cleanup();
            return err;
        }

        if (AppVersionQuad[0] == 3) {
            // Read the first frame to start the flow of video
            err = readFirstFrame();
            if (err != 0) {
                stopVideoStream();
                return err;
            }
        }
        m_bStart.store(true);
        return 0;
    }

};

IVideoStream* MakeVideoStream(const int streamId)
{
    return (IVideoStream*)new VideoStream(streamId);
}

void ReleaseVideoStream(IVideoStream* pVS)
{
    int id = ((VideoStream*)pVS)->getStreamId();
    delete (VideoStream*)pVS;
}
