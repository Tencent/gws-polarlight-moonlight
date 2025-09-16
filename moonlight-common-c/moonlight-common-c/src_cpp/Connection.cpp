#include "Limelight-internal.h"
#include "VideoStream.h"
#include <shared_mutex>
#include <thread>

static int stage = STAGE_NONE;
static ConnListenerConnectionTerminated originalTerminationCallback;
static bool alreadyTerminated;
static PLT_THREAD terminationCallbackThread;
static int terminationCallbackErrorCode;

// Common globals
char *RemoteAddrString;
struct sockaddr_storage RemoteAddr;
SOCKADDR_LEN RemoteAddrLen;
int AppVersionQuad[4];
STREAM_CONFIGURATION StreamConfig;
CONNECTION_LISTENER_CALLBACKS ListenerCallbacks;
DECODER_RENDERER_CALLBACKS VideoCallbacks;
AUDIO_RENDERER_CALLBACKS AudioCallbacks;
int NegotiatedVideoFormat;
volatile bool ConnectionInterrupted;
bool HighQualitySurroundSupported;
bool HighQualitySurroundEnabled;
OPUS_MULTISTREAM_CONFIGURATION NormalQualityOpusConfig;
OPUS_MULTISTREAM_CONFIGURATION HighQualityOpusConfig;
int OriginalVideoBitrate;
int AudioPacketDuration;
bool AudioEncryptionEnabled = false;
bool ReferenceFrameInvalidationSupported;
uint16_t RtspPortNumber;
uint16_t ControlPortNumber;
uint16_t AudioPortNumber;
SS_PING AudioPingPayload;

IVideoStream *VideoStreamInstance = nullptr;
IVideoStream *VideoStreamInstance2 = nullptr;
std::shared_mutex videoStreamMutex1;
std::shared_mutex videoStreamMutex2;

IVideoStream* getVideoStreamInstance(int streamId, std::shared_lock<std::shared_mutex>& lock) {
    if (streamId == 1) {
        lock = std::shared_lock<std::shared_mutex>(videoStreamMutex1);
        return VideoStreamInstance;
    }
    else if(streamId == 2) {
        lock = std::shared_lock<std::shared_mutex>(videoStreamMutex2);
        return VideoStreamInstance2;
    }
    return nullptr;
}
IVideoStream* getVideoStreamInstance(int streamId, std::unique_lock<std::shared_mutex>& lock) {
    while (true) 
    {
        if (streamId == 1)
            lock = std::unique_lock<std::shared_mutex>(videoStreamMutex1, std::try_to_lock);
        else if (streamId == 2)
            lock = std::unique_lock<std::shared_mutex>(videoStreamMutex2, std::try_to_lock);
        if (lock.owns_lock()) 
        {
            if (streamId == 1) {
                return VideoStreamInstance;
            } else if (streamId == 2) {
                return VideoStreamInstance2;
            }
        }
        else
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return nullptr;
}
// Connection stages
static const char *stageNames[STAGE_MAX] = {"none",
                                            "platform initialization",
                                            "name resolution",
                                            "audio stream initialization",
                                            "RTSP handshake",
                                            "control stream initialization",
                                            "video stream initialization",
                                            "input stream initialization",
                                            "control stream establishment",
                                            "video stream establishment",
                                            "audio stream establishment",
                                            "input stream establishment"};

// Get the name of the current stage based on its number
const char *LiGetStageName(int stage) { return stageNames[stage]; }

static const char *stageError[STAGE_MAX] = {"none",
                                            "platform initialization error",
                                            "name resolution error",
                                            "audio stream initialization error",
                                            "RTSP handshake error",
                                            "control stream initialization error",
                                            "video stream initialization error",
                                            "input stream initialization error",
                                            "control stream establishment error",
                                            "video stream establishment error",
                                            "audio stream establishment error",
                                            "input stream establishment error"};
const char *LiGetStageError(int stage) { return stageError[stage]; }
// Interrupt a pending connection attempt. This interruption happens asynchronously
// so it is not safe to start another connection before LiStartConnection() returns.
void LiInterruptConnection(void) {
    // Signal anyone waiting on the global interrupted flag
    ConnectionInterrupted = true;
}

// Stop the connection by undoing the step at the current stage and those before it
void LiStopConnection(int streamNum) {
    //Limelog("$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$ LiStopConnection streamId = %d \n", streamNum);
    // Disable termination callbacks now
    alreadyTerminated = true;

    // Set the interrupted flag
    LiInterruptConnection();

    if (stage == STAGE_INPUT_STREAM_START) {
        Limelog("Stopping input stream...");
        stopInputStream();
        stage--;
        Limelog("done\n");
    }
    if (stage == STAGE_AUDIO_STREAM_START) {
        Limelog("Stopping audio stream...");
        stopAudioStream();
        stage--;
        Limelog("done\n");
    }
    if (stage == STAGE_VIDEO_STREAM_START) {
        Limelog("Stopping video stream = %d", streamNum);
        std::shared_lock<std::shared_mutex> lock;
        IVideoStream* pVS = getVideoStreamInstance(1, lock);
        if (pVS) {
            Limelog("Stopping video stream1");
            pVS->stopVideoStream();
        }
        pVS = getVideoStreamInstance(2, lock);
        if (pVS) {
            Limelog("Stopping video stream2");
            pVS->stopVideoStream();
        }
        stage--;
        Limelog("done\n");
    }
    if (stage == STAGE_CONTROL_STREAM_START) {
        Limelog("Stopping control stream...");
        stopControlStream(streamNum);
        stage--;
        Limelog("done\n");
    }
    if (stage == STAGE_INPUT_STREAM_INIT) {
        Limelog("Cleaning up input stream...");
        destroyInputStream();
        stage--;
        Limelog("done\n");
    }
    if (stage == STAGE_VIDEO_STREAM_INIT) {
        Limelog("Cleaning up video stream...");
        std::unique_lock<std::shared_mutex> lock;
        IVideoStream* pVS = getVideoStreamInstance(1, lock);
        if (pVS) {
            pVS->destroyVideoStream();
            ReleaseVideoStream(pVS);
            VideoStreamInstance = nullptr;

        }
        pVS = getVideoStreamInstance(2, lock);
        if (pVS) {
            pVS->destroyVideoStream();
            ReleaseVideoStream(pVS);
            VideoStreamInstance2 = nullptr;
        }
        stage--;
        Limelog("done\n");
    }
    if (stage == STAGE_CONTROL_STREAM_INIT) {
        Limelog("Cleaning up control stream...");
        destroyControlStream();
        stage--;
        Limelog("done\n");
    }
    if (stage == STAGE_RTSP_HANDSHAKE) {
        // Nothing to do
        stage--;
    }
    if (stage == STAGE_AUDIO_STREAM_INIT) {
        Limelog("Cleaning up audio stream...");
        destroyAudioStream();
        stage--;
        Limelog("done\n");
    }
    if (stage == STAGE_NAME_RESOLUTION) {
        // Nothing to do
        stage--;
    }
    if (stage == STAGE_PLATFORM_INIT) {
        Limelog("Cleaning up platform...");
        cleanupPlatform();
        stage--;
        Limelog("done\n");
    }
    LC_ASSERT(stage == STAGE_NONE);

    if (RemoteAddrString != NULL) {
        free(RemoteAddrString);
        RemoteAddrString = NULL;
    }

}
void LiStopStream(void) {
    Limelog("Stopping video stream...");
    if (VideoStreamInstance) {
        VideoStreamInstance->stopVideoStream();
        Limelog("Cleaning up video stream...");
        std::unique_lock<std::shared_mutex> lock(videoStreamMutex1);
        VideoStreamInstance->destroyVideoStream();
        ReleaseVideoStream(VideoStreamInstance);
        VideoStreamInstance = nullptr;
    }
}
void LiStopStream2(void) {
    Limelog("Stopping video stream2...");
    if (VideoStreamInstance2) {
        VideoStreamInstance2->stopVideoStream();
        Limelog("Cleaning up video stream2...");
        std::unique_lock<std::shared_mutex> lock(videoStreamMutex2);
        VideoStreamInstance2->destroyVideoStream();
        ReleaseVideoStream(VideoStreamInstance2);
        VideoStreamInstance2 = nullptr;
    }
}
static void terminationCallbackThreadFunc(void *context) {
    // Invoke the client's termination callback
    originalTerminationCallback(terminationCallbackErrorCode);
}

// This shim callback runs the client's connectionTerminated() callback on a
// separate thread. This is neccessary because other internal threads directly
// invoke this callback. That can result in a deadlock if the client
// calls LiStopConnection() in the callback when the cleanup code
// attempts to join the thread that the termination callback (and LiStopConnection)
// is running on.
static void ClInternalConnectionTerminated(int errorCode) {
    int err;
    Limelog("connect.cpp  ClInternalConnectionTerminated: %d\n", (int)errorCode);
    Limelog("connect.cpp  alreadyTerminated: %d\n", (int)alreadyTerminated);
    Limelog("connect.cpp  ConnectionInterrupted: %d\n", (int)ConnectionInterrupted);
    // Avoid recursion and issuing multiple callbacks
    if (alreadyTerminated || ConnectionInterrupted) {
        return;
    }

    terminationCallbackErrorCode = errorCode;
    alreadyTerminated = true;

    // Invoke the termination callback on a separate thread
    err = PltCreateThread("AsyncTerm", terminationCallbackThreadFunc, NULL, &terminationCallbackThread);
    if (err != 0) {
        // Nothing we can safely do here, so we'll just assert on debug builds
        Limelog("Failed to create termination thread: %d\n", err);
        LC_ASSERT(err == 0);
    }

    // Close the thread handle since we can never wait on it
    PltCloseThread(&terminationCallbackThread);
}

static bool parseRtspPortNumberFromUrl(const char *rtspSessionUrl, uint16_t *port) {
    // If the session URL is not present, we will just use the well known port
    if (rtspSessionUrl == NULL) {
        return false;
    }

    // Pick the last colon in the string to match the port number
    const char *portNumberStart = strrchr(rtspSessionUrl, ':');
    if (portNumberStart == NULL) {
        return false;
    }

    // Skip the colon
    portNumberStart++;

    // Validate the port number
    long int rawPort = strtol(portNumberStart, NULL, 10);
    if (rawPort <= 0 || rawPort > 65535) {
        return false;
    }

    *port = (uint16_t)rawPort;
    return true;
}

void SetStremNum(int StreamNum) { _StreamNum_.store(StreamNum); }

void SetTempStreamNum(int StreamNum)
{
    nTempStreamNum.store(StreamNum);
}
bool LiStartSecondVideoStream()
{
    std::unique_lock<std::shared_mutex> lock(videoStreamMutex2);
    if (VideoStreamInstance2) {
        VideoStreamInstance2->stopVideoStream();
        VideoStreamInstance2->destroyVideoStream();
        ReleaseVideoStream(VideoStreamInstance2);
        VideoStreamInstance2 = nullptr;
        Limelog("free VideoStreamInstance2\n");
    }
    if (VideoStreamInstance2 == nullptr)
    {
        VideoStreamInstance2 = MakeVideoStream(2);
        Limelog("VideoStreamInstance2 MakeVideoStream  VideoStreamInstance = %d,", VideoStreamInstance);
    }
    auto VideoPortNumber2 = LiGetPortFromPortFlagIndex(ML_PORT_INDEX_UDP_48001);
    VideoStreamInstance2->SetPortNumber(VideoPortNumber2);
    VideoStreamInstance2->initializeVideoStream();
    if (VideoStreamInstance)
    {
        const char* payload = VideoStreamInstance->GetVideoPingPayload();
        VideoStreamInstance2->CopyPingPayload(payload);
    }
    auto err = VideoStreamInstance2->startVideoStream(nullptr, 0);
    if (err != 0) {
        return false;
    }
    return true;
}
// Starts the connection to the streaming machine
int LiStartConnection(int streamNum, PSERVER_INFORMATION serverInfo, PSTREAM_CONFIGURATION streamConfig,
                      PCONNECTION_LISTENER_CALLBACKS clCallbacks, PDECODER_RENDERER_CALLBACKS drCallbacks,
                      PAUDIO_RENDERER_CALLBACKS arCallbacks, void *renderContext, int drFlags, void *audioContext,
                      int arFlags) {
    int err;

    if (drCallbacks != NULL && (drCallbacks->capabilities & CAPABILITY_PULL_RENDERER) &&
        drCallbacks->submitDecodeUnit) {
        Limelog("CAPABILITY_PULL_RENDERER cannot be set with a submitDecodeUnit callback\n");
        err = -1;
        goto Cleanup;
    }

    if (drCallbacks != NULL && (drCallbacks->capabilities & CAPABILITY_PULL_RENDERER) &&
        (drCallbacks->capabilities & CAPABILITY_DIRECT_SUBMIT)) {
        Limelog("CAPABILITY_PULL_RENDERER and CAPABILITY_DIRECT_SUBMIT cannot be set together\n");
        err = -1;
        goto Cleanup;
    }

    // Replace missing callbacks with placeholders
    fixupMissingCallbacks(&drCallbacks, &arCallbacks, &clCallbacks);


    memcpy(&VideoCallbacks, drCallbacks, sizeof(VideoCallbacks));
    memcpy(&AudioCallbacks, arCallbacks, sizeof(AudioCallbacks));
#ifdef LC_DEBUG_RECORD_MODE
    // Install the pass-through recorder callbacks
    setRecorderCallbacks(&VideoCallbacks, &AudioCallbacks);
#endif

    // Hook the termination callback so we can avoid issuing a termination callback
    // after LiStopConnection() is called.
    //
    // Initialize ListenerCallbacks before anything that could call Limelog().
    originalTerminationCallback = clCallbacks->connectionTerminated;
    memcpy(&ListenerCallbacks, clCallbacks, sizeof(ListenerCallbacks));
    ListenerCallbacks.connectionTerminated = ClInternalConnectionTerminated;

    Limelog("LiStartConnection VideoStreamInstance = %d,", VideoStreamInstance);
    Limelog("LiStartConnection VideoStreamInstance2 = %d,", VideoStreamInstance2);
    {
        std::unique_lock<std::shared_mutex> lock;
        IVideoStream* pVS = getVideoStreamInstance(1, lock);
        if (pVS) {
            pVS->stopVideoStream();
            pVS->destroyVideoStream();
            ReleaseVideoStream(pVS);
            VideoStreamInstance = nullptr;
            Limelog("free VideoStreamInstance\n");
        }
        VideoStreamInstance = MakeVideoStream(1);

        pVS = getVideoStreamInstance(2, lock);
        if (pVS) {
            pVS->stopVideoStream();
            pVS->destroyVideoStream();
            ReleaseVideoStream(pVS);
            VideoStreamInstance2 = nullptr;
            Limelog("free VideoStreamInstance2\n");
        }
        if (streamNum > 1)
        {
            VideoStreamInstance2 = MakeVideoStream(2);
        }
    }

    NegotiatedVideoFormat = 0;
    memcpy(&StreamConfig, streamConfig, sizeof(StreamConfig));
    OriginalVideoBitrate = streamConfig->bitrate;
    RemoteAddrString = strdup(serverInfo->address);

    // The values in RTSP SETUP will be used to populate these.
    ControlPortNumber = 0;
    AudioPortNumber = 0;

    // Parse RTSP port number from RTSP session URL
    // if (!parseRtspPortNumberFromUrl(serverInfo->rtspSessionUrl, &RtspPortNumber)) {
    //     // Use the well known port if parsing fails
    //     RtspPortNumber = LiGetPortFromPortFlagIndex(ML_PORT_INDEX_TCP_48010); // 48010;

    //     Limelog("RTSP port: %u (RTSP URL parsing failed)\n", RtspPortNumber);
    // } else {
    //     Limelog("RTSP port: %u\n", RtspPortNumber);
    // }
    Limelog("init originalTerminationCallback = %d\n", originalTerminationCallback);

    Limelog("streamNum = %d,VideoStreamInstance2 = %d\n",streamNum,VideoStreamInstance2);
    // RtspPortNumber = 0;

    // port mapping exists, not use parseRtspPortNumberFromUrl
    RtspPortNumber = LiGetPortFromPortFlagIndex(ML_PORT_INDEX_TCP_48010); // 48010;
    Limelog("RTSP port: %u\n", RtspPortNumber);
    alreadyTerminated = false;
    ConnectionInterrupted = false;

    // Validate the audio configuration
    if (MAGIC_BYTE_FROM_AUDIO_CONFIG(StreamConfig.audioConfiguration) != 0xCA ||
        CHANNEL_COUNT_FROM_AUDIO_CONFIGURATION(StreamConfig.audioConfiguration) >
            AUDIO_CONFIGURATION_MAX_CHANNEL_COUNT) {
        Limelog("Invalid audio configuration specified\n");
        err = -1;
        goto Cleanup;
    }

    // FEC only works in 16 byte chunks, so we must round down
    // the given packet size to the nearest multiple of 16.
    StreamConfig.packetSize -= StreamConfig.packetSize % 16;

    if (StreamConfig.packetSize == 0) {
        Limelog("Invalid packet size specified\n");
        err = -1;
        goto Cleanup;
    }

    // Height must not be odd or NVENC will fail to initialize
    if (StreamConfig.height & 0x1) {
        Limelog("Encoder height must not be odd. Rounding %d to %d\n", StreamConfig.height, StreamConfig.height & ~0x1);
        StreamConfig.height = StreamConfig.height & ~0x1;
    }

    // Height must not be odd or NVENC will fail to initialize
    if (StreamConfig.height2 & 0x1) {
        Limelog("Encoder height must not be odd. Rounding %d to %d\n", StreamConfig.height, StreamConfig.height & ~0x1);
        StreamConfig.height2 = StreamConfig.height2 & ~0x1;
    }

    // Dimensions over 4096 are only supported with HEVC on NVENC
    if (!StreamConfig.supportsHevc &&
        (StreamConfig.width > 4096 ||
            StreamConfig.height > 4096 ||
            StreamConfig.width2 > 4096 ||
            StreamConfig.height2 > 4096)) {
        Limelog("WARNING: Streaming at resolutions above 4K using H.264 will likely fail! Trying anyway!\n");
    }
    // Dimensions over 8192 aren't supported at all (even on Turing)
    else if (StreamConfig.width > 8192 || StreamConfig.height > 8192 ||
             StreamConfig.width2 > 8192 || StreamConfig.height2 > 8192) {
        Limelog("WARNING: Streaming at resolutions above 8K will likely fail! Trying anyway!\n");
    }

    // Reference frame invalidation doesn't seem to work with resolutions much
    // higher than 1440p. I haven't figured out a pattern to indicate which
    // resolutions will work and which won't, but we can at least exclude
    // 4K from RFI to avoid significant persistent artifacts after frame loss.
    if (((StreamConfig.width == 3840 && StreamConfig.height == 2160) || 
        (StreamConfig.width2 == 3840 && StreamConfig.height2 == 2160)) &&
        (VideoCallbacks.capabilities & CAPABILITY_REFERENCE_FRAME_INVALIDATION_AVC)) {
        Limelog("Disabling reference frame invalidation for 4K streaming\n");
        VideoCallbacks.capabilities &= ~CAPABILITY_REFERENCE_FRAME_INVALIDATION_AVC;
    }

    // Extract the appversion from the supplied string
    if (extractVersionQuadFromString(serverInfo->serverInfoAppVersion, AppVersionQuad) < 0) {
        Limelog("Invalid appversion string: %s\n", serverInfo->serverInfoAppVersion);
        err = -1;
        goto Cleanup;
    }

    Limelog("Initializing platform...");
    ListenerCallbacks.stageStarting(STAGE_PLATFORM_INIT);
    err = initializePlatform();
    if (err != 0) {
        Limelog("failed: %d\n", err);
        ListenerCallbacks.stageFailed(STAGE_PLATFORM_INIT, err);
        goto Cleanup;
    }
    stage++;
    LC_ASSERT(stage == STAGE_PLATFORM_INIT);
    ListenerCallbacks.stageComplete(STAGE_PLATFORM_INIT);
    Limelog("done\n");

    Limelog("Resolving host name...");
    ListenerCallbacks.stageStarting(STAGE_NAME_RESOLUTION);
    LC_ASSERT(RtspPortNumber != 0);
    if (RtspPortNumber != LiGetPortFromPortFlagIndex(ML_PORT_INDEX_TCP_48010)) {
        // If we have an alternate RTSP port, use that as our test port. The host probably
        // isn't listening on 47989 or 47984 anyway, since they're using alternate ports.
        err = resolveHostName(serverInfo->address, AF_UNSPEC, RtspPortNumber, &RemoteAddr, &RemoteAddrLen);
        if (err != 0) {
            // Sleep for a second and try again. It's possible that we've attempt to connect
            // before the host has gotten around to listening on the RTSP port. Give it some
            // time before retrying.
            PltSleepMs(1000);
            err = resolveHostName(serverInfo->address, AF_UNSPEC, RtspPortNumber, &RemoteAddr, &RemoteAddrLen);
        }
    } else {
        // We use TCP 47984 and 47989 first here because we know those should always be listening
        // on hosts using the standard ports.
        //
        // TCP 48010 is a last resort because:
        // a) it's not always listening and there's a race between listen() on the host and our connect()
        // b) it's not used at all by certain host versions which perform RTSP over ENet
        err = resolveHostName(serverInfo->address, AF_UNSPEC, LiGetPortFromPortFlagIndex(ML_PORT_INDEX_TCP_47984),
                              &RemoteAddr, &RemoteAddrLen);
        if (err != 0) {
            err = resolveHostName(serverInfo->address, AF_UNSPEC, LiGetPortFromPortFlagIndex(ML_PORT_INDEX_TCP_47989),
                                  &RemoteAddr, &RemoteAddrLen);
        }
        if (err != 0) {
            err = resolveHostName(serverInfo->address, AF_UNSPEC, LiGetPortFromPortFlagIndex(ML_PORT_INDEX_TCP_48010),
                                  &RemoteAddr, &RemoteAddrLen);
        }
    }
    if (err != 0) {
        Limelog("failed: %d\n", err);
        ListenerCallbacks.stageFailed(STAGE_NAME_RESOLUTION, err);
        goto Cleanup;
    }
    stage++;
    LC_ASSERT(stage == STAGE_NAME_RESOLUTION);
    ListenerCallbacks.stageComplete(STAGE_NAME_RESOLUTION);
    Limelog("done\n");

    // If STREAM_CFG_AUTO was requested, determine the streamingRemotely value
    // now that we have resolved the target address and impose the video packet
    // size cap if required.
    if (StreamConfig.streamingRemotely == STREAM_CFG_AUTO) {
        if (isPrivateNetworkAddress(&RemoteAddr)) {
            StreamConfig.streamingRemotely = STREAM_CFG_LOCAL;
        } else {
            StreamConfig.streamingRemotely = STREAM_CFG_REMOTE;

            if (StreamConfig.packetSize > 1024) {
                // Cap packet size at 1024 for remote streaming to avoid
                // MTU problems and fragmentation.
                Limelog("Packet size capped at 1KB for remote streaming\n");
                StreamConfig.packetSize = 1024;
            }
        }
    }
    Limelog("Initializing audio stream...");
    ListenerCallbacks.stageStarting(STAGE_AUDIO_STREAM_INIT);
    err = initializeAudioStream();
    if (err != 0) {
        Limelog("failed: %d\n", err);
        ListenerCallbacks.stageFailed(STAGE_AUDIO_STREAM_INIT, err);
        goto Cleanup;
    }
    stage++;
    LC_ASSERT(stage == STAGE_AUDIO_STREAM_INIT);
    ListenerCallbacks.stageComplete(STAGE_AUDIO_STREAM_INIT);
    Limelog("done\n");
    Limelog("Starting RTSP handshake...");
    ListenerCallbacks.stageStarting(STAGE_RTSP_HANDSHAKE);
    err = performRtspHandshake(serverInfo, streamNum);
    if (err != 0) {
        Limelog("failed: %d\n", err);
        ListenerCallbacks.stageFailed(STAGE_RTSP_HANDSHAKE, err);
        goto Cleanup;
    }
    stage++;
    LC_ASSERT(stage == STAGE_RTSP_HANDSHAKE);
    ListenerCallbacks.stageComplete(STAGE_RTSP_HANDSHAKE);
    Limelog("done\n");

    Limelog("Initializing control stream...");
    ListenerCallbacks.stageStarting(STAGE_CONTROL_STREAM_INIT);
    err = initializeControlStream(streamNum);
    if (err != 0) {
        Limelog("failed: %d\n", err);
        ListenerCallbacks.stageFailed(STAGE_CONTROL_STREAM_INIT, err);
        goto Cleanup;
    }
    stage++;
    LC_ASSERT(stage == STAGE_CONTROL_STREAM_INIT);
    ListenerCallbacks.stageComplete(STAGE_CONTROL_STREAM_INIT);
    Limelog("done\n");

    Limelog("Initializing video stream...");
    ListenerCallbacks.stageStarting(STAGE_VIDEO_STREAM_INIT);
    VideoStreamInstance->initializeVideoStream();
    // Second Video
    if (streamNum > 1) {
        VideoStreamInstance2->initializeVideoStream();
    }
    stage++;
    LC_ASSERT(stage == STAGE_VIDEO_STREAM_INIT);
    ListenerCallbacks.stageComplete(STAGE_VIDEO_STREAM_INIT);
    Limelog("done\n");

    Limelog("Initializing input stream...");
    ListenerCallbacks.stageStarting(STAGE_INPUT_STREAM_INIT);
    initializeInputStream();
    stage++;
    LC_ASSERT(stage == STAGE_INPUT_STREAM_INIT);
    ListenerCallbacks.stageComplete(STAGE_INPUT_STREAM_INIT);
    Limelog("done\n");

    Limelog("Starting control stream...");
    ListenerCallbacks.stageStarting(STAGE_CONTROL_STREAM_START);
    err = startControlStream(streamNum);
    if (err != 0) {
        Limelog("failed: %d\n", err);
        ListenerCallbacks.stageFailed(STAGE_CONTROL_STREAM_START, err);
        goto Cleanup;
    }
    stage++;
    LC_ASSERT(stage == STAGE_CONTROL_STREAM_START);
    ListenerCallbacks.stageComplete(STAGE_CONTROL_STREAM_START);
    Limelog("done\n");

    // First Video + Second Video(Maybe)
    Limelog("Starting video stream...");
    ListenerCallbacks.stageStarting(STAGE_VIDEO_STREAM_START);
    err = VideoStreamInstance->startVideoStream(renderContext, drFlags);
    if (err != 0) {
        Limelog("Video stream start failed: %d\n", err);
        ListenerCallbacks.stageFailed(STAGE_VIDEO_STREAM_START, err);
        goto Cleanup;
    }
    // if we have second video stream
    if (streamNum > 1) {
        err = VideoStreamInstance2->startVideoStream(renderContext, drFlags);
        if (err != 0) {
            Limelog("Video2 stream start failed: %d\n", err);
            ListenerCallbacks.stageFailed(STAGE_VIDEO_STREAM_START, err);
            goto Cleanup;
        }
    }
    stage++;
    LC_ASSERT(stage == STAGE_VIDEO_STREAM_START);
    ListenerCallbacks.stageComplete(STAGE_VIDEO_STREAM_START);
    Limelog("done\n");

    Limelog("Starting audio stream...");
    ListenerCallbacks.stageStarting(STAGE_AUDIO_STREAM_START);
    err = startAudioStream(audioContext, arFlags);
    if (err != 0) {
        Limelog("Audio stream start failed: %d\n", err);
        ListenerCallbacks.stageFailed(STAGE_AUDIO_STREAM_START, err);
        goto Cleanup;
    }
    stage++;
    LC_ASSERT(stage == STAGE_AUDIO_STREAM_START);
    ListenerCallbacks.stageComplete(STAGE_AUDIO_STREAM_START);
    Limelog("done\n");

    Limelog("Starting input stream...");
    ListenerCallbacks.stageStarting(STAGE_INPUT_STREAM_START);
    err = startInputStream();
    if (err != 0) {
        Limelog("Input stream start failed: %d\n", err);
        ListenerCallbacks.stageFailed(STAGE_INPUT_STREAM_START, err);
        goto Cleanup;
    }
    stage++;
    LC_ASSERT(stage == STAGE_INPUT_STREAM_START);
    ListenerCallbacks.stageComplete(STAGE_INPUT_STREAM_START);
    Limelog("done\n");

    // Wiggle the mouse a bit to wake the display up
    LiSendMouseMoveEvent(1, 1);
    PltSleepMs(10);
    LiSendMouseMoveEvent(-1, -1);
    PltSleepMs(10);

    ListenerCallbacks.connectionStarted();
    Limelog("LiStartConnection drCallbacks->submitDecodeUnit = %d", drCallbacks->submitDecodeUnit);
Cleanup:
    if (err != 0) {
        // Undo any work we've done here before failing
        LiStopConnection(streamNum);
    }
    return err;
}
