#include <Limelight.h>
#include "ffmpeg.h"
#include "streaming/streamutils.h"
#include "streaming/session.h"

#include <h264_stream.h>

#include "ffmpeg-renderers/sdlvid.h"

#ifdef Q_OS_WIN32
#include "ffmpeg-renderers/dxva2.h"
#include "ffmpeg-renderers/d3d11va.h"
#endif

#ifdef Q_OS_DARWIN
#include "ffmpeg-renderers/vt.h"
#endif

#ifdef HAVE_LIBVA
#include "ffmpeg-renderers/vaapi.h"
#endif

#ifdef HAVE_LIBVDPAU
#include "ffmpeg-renderers/vdpau.h"
#endif

#ifdef HAVE_MMAL
#include "ffmpeg-renderers/mmal.h"
#endif

#ifdef HAVE_DRM
#include "ffmpeg-renderers/drm.h"
#endif

#ifdef HAVE_EGL
#include "ffmpeg-renderers/eglvid.h"
#endif

#ifdef HAVE_CUDA
#include "ffmpeg-renderers/cuda.h"
#endif

// This is gross but it allows us to use sizeof()
#include "ffmpeg_videosamples.cpp"

#define MAX_SPS_EXTRA_SIZE 16

#define FAILED_DECODES_RESET_THRESHOLD 20

typedef struct {
    const char* codec;
    int capabilities;
} codec_info_t;

static const QList<codec_info_t> k_NonHwaccelH264Codecs = {
#ifdef HAVE_MMAL
    {"h264_mmal", 0},
#endif
    {"h264_rkmpp", 0},
    {"h264_nvv4l2", 0},
    {"h264_nvmpi", 0},
#ifndef HAVE_MMAL
    // Only enable V4L2M2M by default on non-MMAL (RPi) builds. The performance
    // of the V4L2M2M wrapper around MMAL is not enough for 1080p 60 FPS, so we
    // would rather show the missing hardware acceleration warning when the user
    // is in Full KMS mode rather than try to use a poorly performing hwaccel.
    // See discussion on https://github.com/jc-kynesim/rpi-ffmpeg/pull/25
    {"h264_v4l2m2m", 0},
#endif
    {"h264_omx", 0},
};

static const QList<codec_info_t> k_NonHwaccelHEVCCodecs = {
    {"hevc_rkmpp", 0},
    {"hevc_nvv4l2", CAPABILITY_REFERENCE_FRAME_INVALIDATION_HEVC},
    {"hevc_nvmpi", 0},
    {"hevc_v4l2m2m", 0},
    {"hevc_omx", 0},
};

bool FFmpegVideoDecoder::isHardwareAccelerated()
{
    return m_HwDecodeCfg != nullptr ||
            (m_VideoDecoderCtx->codec->capabilities & AV_CODEC_CAP_HARDWARE) != 0;
}

bool FFmpegVideoDecoder::isAlwaysFullScreen()
{
    return m_FrontendRenderer->getRendererAttributes() & RENDERER_ATTRIBUTE_FULLSCREEN_ONLY;
}

bool FFmpegVideoDecoder::isHdrSupported()
{
    return m_FrontendRenderer->getRendererAttributes() & RENDERER_ATTRIBUTE_HDR_SUPPORT;
}

void FFmpegVideoDecoder::setHdrMode(bool enabled)
{
    m_FrontendRenderer->setHdrMode(enabled);
}

int FFmpegVideoDecoder::getDecoderCapabilities()
{
    bool ok;

    int capabilities = qEnvironmentVariableIntValue("DECODER_CAPS", &ok);
    if (ok) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Using decoder capability override: 0x%x",
                    capabilities);
    }
    else {
        // Start with the backend renderer's capabilities
        capabilities = m_BackendRenderer->getDecoderCapabilities();

        if (!isHardwareAccelerated()) {
            // Slice up to 4 times for parallel CPU decoding, once slice per core
            int slices = qMin(MAX_SLICES, SDL_GetCPUCount());
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                        "Encoder configured for %d slices per frame",
                        slices);
            capabilities |= CAPABILITY_SLICES_PER_FRAME(slices);

            // Enable HEVC RFI when using the FFmpeg software decoder
            capabilities |= CAPABILITY_REFERENCE_FRAME_INVALIDATION_HEVC;
        }
        else if (m_HwDecodeCfg == nullptr) {
            // We have a non-hwaccel hardware decoder. This will always
            // be using SDLRenderer/DrmRenderer so we will pick decoder
            // capabilities based on the decoder name.
            for (const codec_info_t& codecInfo : k_NonHwaccelH264Codecs) {
                if (strcmp(m_VideoDecoderCtx->codec->name, codecInfo.codec) == 0) {
                    capabilities = codecInfo.capabilities;
                    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                                "Found capabilities for non-hwaccel H.264 decoder: %s -> %d",
                                m_VideoDecoderCtx->codec->name,
                                capabilities);
                    break;
                }
            }
            for (const codec_info_t& codecInfo : k_NonHwaccelHEVCCodecs) {
                if (strcmp(m_VideoDecoderCtx->codec->name, codecInfo.codec) == 0) {
                    capabilities = codecInfo.capabilities;
                    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                                "Found capabilities for non-hwaccel HEVC decoder: %s -> %d",
                                m_VideoDecoderCtx->codec->name,
                                capabilities);
                    break;
                }
            }
        }
    }

    // We use our own decoder thread with the "pull" model. This cannot
    // be overridden using the by the user because it is critical to
    // our operation.
    capabilities |= CAPABILITY_PULL_RENDERER;

    return capabilities;
}

int FFmpegVideoDecoder::getDecoderColorspace()
{
    return m_FrontendRenderer->getDecoderColorspace();
}

int FFmpegVideoDecoder::getDecoderColorRange()
{
    return m_FrontendRenderer->getDecoderColorRange();
}

QSize FFmpegVideoDecoder::getDecoderMaxResolution()
{
    if (m_BackendRenderer->getRendererAttributes() & RENDERER_ATTRIBUTE_1080P_MAX) {
        return QSize(1920, 1080);
    }
    else {
        // No known maximum
        return QSize(0, 0);
    }
}

enum AVPixelFormat FFmpegVideoDecoder::ffGetFormat(AVCodecContext* context,
                                                   const enum AVPixelFormat* pixFmts)
{
    FFmpegVideoDecoder* decoder = (FFmpegVideoDecoder*)context->opaque;
    const enum AVPixelFormat *p;

    for (p = pixFmts; *p != -1; p++) {
        // Only match our hardware decoding codec or preferred SW pixel
        // format (if not using hardware decoding). It's crucial
        // to override the default get_format() which will try
        // to gracefully fall back to software decode and break us.
        if (*p == (decoder->m_HwDecodeCfg ? decoder->m_HwDecodeCfg->pix_fmt : context->pix_fmt) &&
                decoder->m_BackendRenderer->prepareDecoderContextInGetFormat(context, *p)) {
            return *p;
        }
    }

    // Failed to match the preferred pixel formats. Try non-preferred options for non-hwaccel decoders.
    if (decoder->m_HwDecodeCfg == nullptr) {
        for (p = pixFmts; *p != -1; p++) {
            if (decoder->m_FrontendRenderer->isPixelFormatSupported(decoder->m_VideoFormat, *p) &&
                    decoder->m_BackendRenderer->prepareDecoderContextInGetFormat(context, *p)) {
                return *p;
            }
        }
    }

    return AV_PIX_FMT_NONE;
}

FFmpegVideoDecoder::FFmpegVideoDecoder(bool testOnly)
    : m_Pkt(av_packet_alloc()),
      m_VideoDecoderCtx(nullptr),
      m_DecodeBuffer(1024 * 1024, 0),
      m_HwDecodeCfg(nullptr),
      m_BackendRenderer(nullptr),
      m_FrontendRenderer(nullptr),
      m_ConsecutiveFailedDecodes(0),
      m_Pacer(nullptr),
      m_FramesIn(0),
      m_FramesOut(0),
      m_LastFrameNumber(0),
      m_StreamFps(0),
      m_VideoFormat(0),
      m_NeedsSpsFixup(false),
      m_TestOnly(testOnly),
      m_DecoderThread(nullptr)
{
    SDL_zero(m_ActiveWndVideoStats);
    SDL_zero(m_LastWndVideoStats);
    SDL_zero(m_GlobalVideoStats);

    SDL_AtomicSet(&m_DecoderThreadShouldQuit, 0);

    // Use linear filtering when renderer scaling is required
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
}

FFmpegVideoDecoder::~FFmpegVideoDecoder()
{
    reset();

    // Set log level back to default.
    // NB: We don't do this in reset() because we want
    // to preserve the log level across reset() during
    // test initialization.
    av_log_set_level(AV_LOG_INFO);

    av_packet_free(&m_Pkt);
}

IFFmpegRenderer* FFmpegVideoDecoder::getBackendRenderer()
{
    return m_BackendRenderer;
}

void FFmpegVideoDecoder::reset()
{
    // Terminate the decoder thread before doing anything else.
    // It might be touching things we're about to free.
    if (m_DecoderThread != nullptr) {
        SDL_AtomicSet(&m_DecoderThreadShouldQuit, 1);
        LiWakeWaitForVideoFrame(m_streamId);
        SDL_WaitThread(m_DecoderThread, NULL);
        SDL_AtomicSet(&m_DecoderThreadShouldQuit, 0);
        m_DecoderThread = nullptr;
    }

    m_FramesIn = m_FramesOut = 0;
    m_FrameInfoQueue.clear();

    delete m_Pacer;
    m_Pacer = nullptr;

    // This must be called after deleting Pacer because it
    // may be holding AVFrames to free in its destructor.
    // However, it must be called before deleting the IFFmpegRenderer
    // since the codec context may be referencing objects that we
    // need to delete in the renderer destructor.
    avcodec_free_context(&m_VideoDecoderCtx);

    if (!m_TestOnly) {
        Session::get()->getOverlayManager(m_streamId).setOverlayRenderer(nullptr);
    }

    // If we have a separate frontend renderer, free that first
    if (m_FrontendRenderer != m_BackendRenderer) {
        delete m_FrontendRenderer;
    }

    delete m_BackendRenderer;

    m_FrontendRenderer = m_BackendRenderer = nullptr;

    if (!m_TestOnly) {
        logVideoStats(m_GlobalVideoStats, "Global video stats");
    }
    else {
        // Test-only decoders can't have any frames submitted
        SDL_assert(m_GlobalVideoStats.totalFrames == 0);
    }
}

bool FFmpegVideoDecoder::createFrontendRenderer(PDECODER_PARAMETERS params, bool useAlternateFrontend)
{
    if (useAlternateFrontend) {
#ifdef HAVE_DRM
        // If we're trying to stream HDR, we need to use the DRM renderer in direct
        // rendering mode so it can set the HDR metadata on the display. EGL does
        // not currently support this (and even if it did, Mesa and Wayland don't
        // currently have protocols to actually get that metadata to the display).
        if ((params->videoFormat & VIDEO_FORMAT_MASK_10BIT) && m_BackendRenderer->canExportDrmPrime()) {
            m_FrontendRenderer = new DrmRenderer(false, m_BackendRenderer);
            if (m_FrontendRenderer->initialize(params)) {
                return true;
            }
            delete m_FrontendRenderer;
            m_FrontendRenderer = nullptr;
        }
#endif

#if defined(HAVE_EGL) && !defined(GL_IS_SLOW)
        if (m_BackendRenderer->canExportEGL()) {
            m_FrontendRenderer = new EGLRenderer(m_BackendRenderer);
            if (m_FrontendRenderer->initialize(params)) {
                return true;
            }
            delete m_FrontendRenderer;
            m_FrontendRenderer = nullptr;
        }
#endif
        // If we made it here, we failed to create the EGLRenderer
        return false;
    }

    if (m_BackendRenderer->isDirectRenderingSupported()) {
        // The backend renderer can render to the display
        m_FrontendRenderer = m_BackendRenderer;
    }
    else {
        // The backend renderer cannot directly render to the display, so
        // we will create an SDL or DRM renderer to draw the frames.

#ifdef GL_IS_SLOW
#ifdef HAVE_DRM
        m_FrontendRenderer = new DrmRenderer(false, m_BackendRenderer);
        if (m_FrontendRenderer->initialize(params)) {
            return true;
        }
        delete m_FrontendRenderer;
        m_FrontendRenderer = nullptr;
#endif

#ifdef HAVE_EGL
        // We explicitly skipped EGL in the GL_IS_SLOW case above.
        // If DRM didn't work either, try EGL now.
        if (m_BackendRenderer->canExportEGL()) {
            m_FrontendRenderer = new EGLRenderer(m_BackendRenderer);
            if (m_FrontendRenderer->initialize(params)) {
                return true;
            }
            delete m_FrontendRenderer;
            m_FrontendRenderer = nullptr;
        }
#endif
#endif

        m_FrontendRenderer = new SdlRenderer();
        if (!m_FrontendRenderer->initialize(params)) {
            return false;
        }
    }

    return true;
}

bool FFmpegVideoDecoder::completeInitialization(const AVCodec* decoder, PDECODER_PARAMETERS params, bool testFrame, bool useAlternateFrontend)
{
    // In test-only mode, we should only see test frames
    SDL_assert(!m_TestOnly || testFrame);

    // Create the frontend renderer based on the capabilities of the backend renderer
    if (!createFrontendRenderer(params, useAlternateFrontend)) {
        return false;
    }

    m_StreamFps = params->frameRate;
    m_VideoFormat = params->videoFormat;

    // Don't bother initializing Pacer if we're not actually going to render
    if (!testFrame) {
        m_Pacer = new Pacer(m_FrontendRenderer, &m_ActiveWndVideoStats);
        if (!m_Pacer->initialize(params->window,
                 params->frameRate,
                 params->enableFramePacing || (params->enableVsync && (m_FrontendRenderer->getRendererAttributes() & RENDERER_ATTRIBUTE_FORCE_PACING)))) {
            return false;
        }
    }

    m_VideoDecoderCtx = avcodec_alloc_context3(decoder);
    if (!m_VideoDecoderCtx) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Unable to allocate video decoder context");
        return false;
    }

    // Always request low delay decoding
    m_VideoDecoderCtx->flags |= AV_CODEC_FLAG_LOW_DELAY;

    // Allow display of corrupt frames and frames missing references
    m_VideoDecoderCtx->flags |= AV_CODEC_FLAG_OUTPUT_CORRUPT;
    m_VideoDecoderCtx->flags2 |= AV_CODEC_FLAG2_SHOW_ALL;

    // Report decoding errors to allow us to request a key frame
    //
    // With HEVC streams, FFmpeg can drop a frame (hwaccel->start_frame() fails)
    // without telling us. Since we have an infinite GOP length, this causes artifacts
    // on screen that persist for a long time. It's easy to cause this condition
    // by using NVDEC and delaying 100 ms randomly in the render path so the decoder
    // runs out of output buffers.
    m_VideoDecoderCtx->err_recognition = AV_EF_EXPLODE;

    // Enable slice multi-threading for software decoding
    if (!isHardwareAccelerated()) {
        m_VideoDecoderCtx->thread_type = FF_THREAD_SLICE;
        m_VideoDecoderCtx->thread_count = qMin(MAX_SLICES, SDL_GetCPUCount());
    }
    else {
        // No threading for HW decode
        m_VideoDecoderCtx->thread_count = 1;
    }

    // Setup decoding parameters
    m_VideoDecoderCtx->width = params->width;
    m_VideoDecoderCtx->height = params->height;
    m_VideoDecoderCtx->pix_fmt = m_FrontendRenderer->getPreferredPixelFormat(params->videoFormat);
    m_VideoDecoderCtx->get_format = ffGetFormat;

    AVDictionary* options = nullptr;

    // Allow the backend renderer to attach data to this decoder
    if (!m_BackendRenderer->prepareDecoderContext(m_VideoDecoderCtx, &options)) {
        return false;
    }

    // Nobody must override our ffGetFormat
    SDL_assert(m_VideoDecoderCtx->get_format == ffGetFormat);

    // Stash a pointer to this object in the context
    SDL_assert(m_VideoDecoderCtx->opaque == nullptr);
    m_VideoDecoderCtx->opaque = this;

    int err = avcodec_open2(m_VideoDecoderCtx, decoder, &options);
    av_dict_free(&options);
    if (err < 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Unable to open decoder for format: %x",
                     params->videoFormat);
        return false;
    }

    // FFMpeg doesn't completely initialize the codec until the codec
    // config data comes in. This would be too late for us to change
    // our minds on the selected video codec, so we'll do a trial run
    // now to see if things will actually work when the video stream
    // comes in.
    if (testFrame) {
        switch (params->videoFormat) {
        case VIDEO_FORMAT_H264:
            m_Pkt->data = (uint8_t*)k_H264TestFrame;
            m_Pkt->size = sizeof(k_H264TestFrame);
            break;
        case VIDEO_FORMAT_H265:
            m_Pkt->data = (uint8_t*)k_HEVCMainTestFrame;
            m_Pkt->size = sizeof(k_HEVCMainTestFrame);
            break;
        case VIDEO_FORMAT_H265_MAIN10:
            m_Pkt->data = (uint8_t*)k_HEVCMain10TestFrame;
            m_Pkt->size = sizeof(k_HEVCMain10TestFrame);
            break;
        default:
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "No test frame for format: %x",
                         params->videoFormat);
            return false;
        }

        AVFrame* frame = av_frame_alloc();
        if (!frame) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "Failed to allocate frame");
            return false;
        }

        // Some decoders won't output on the first frame, so we'll submit
        // a few test frames if we get an EAGAIN error.
        for (int retries = 0; retries < 5; retries++) {
            // Most FFmpeg decoders process input using a "push" model.
            // We'll see those fail here if the format is not supported.
            err = avcodec_send_packet(m_VideoDecoderCtx, m_Pkt);
            if (err < 0) {
                av_frame_free(&frame);
                char errorstring[512];
                av_strerror(err, errorstring, sizeof(errorstring));
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                            "Test decode failed (avcodec_send_packet): %s", errorstring);
                return false;
            }

            // A few FFmpeg decoders (h264_mmal) process here using a "pull" model.
            // Those decoders will fail here if the format is not supported.
            err = avcodec_receive_frame(m_VideoDecoderCtx, frame);
            if (err == AVERROR(EAGAIN)) {
                // Wait a little while to let the hardware work
                SDL_Delay(100);
            }
            else {
                // Done!
                break;
            }
        }

        if (err < 0) {
            char errorstring[512];
            av_strerror(err, errorstring, sizeof(errorstring));
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "Test decode failed (avcodec_receive_frame): %s", errorstring);
            av_frame_free(&frame);
            return false;
        }

        // Allow the renderer to do any validation it wants on this frame
        if (!m_FrontendRenderer->testRenderFrame(frame)) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "Test decode failed (testRenderFrame)");
            av_frame_free(&frame);
            return false;
        }

        av_frame_free(&frame);
    }
    else {
        if ((params->videoFormat & VIDEO_FORMAT_MASK_H264) &&
                !(m_BackendRenderer->getDecoderCapabilities() & CAPABILITY_REFERENCE_FRAME_INVALIDATION_AVC)) {
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                        "Using H.264 SPS fixup");
            m_NeedsSpsFixup = true;
        }
        else {
            m_NeedsSpsFixup = false;
        }

        // Tell overlay manager to use this frontend renderer
        Session::get()->getOverlayManager(m_streamId).setOverlayRenderer(m_FrontendRenderer);

        // Only create the decoder thread when instantiating the decoder for real. It will use APIs from
        // Gleam-common-c that can only be legally called with an established connection.
        m_DecoderThread = SDL_CreateThread(FFmpegVideoDecoder::decoderThreadProcThunk, "FFDecoder", (void*)this);
        if (m_DecoderThread == nullptr) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "Failed to create decoder thread: %s", SDL_GetError());
            return false;
        }
    }

    return true;
}

void FFmpegVideoDecoder::addVideoStats(VIDEO_STATS& src, VIDEO_STATS& dst)
{
    dst.receivedFrames += src.receivedFrames;
    dst.decodedFrames += src.decodedFrames;
    dst.renderedFrames += src.renderedFrames;
    dst.totalFrames += src.totalFrames;
    dst.networkDroppedFrames += src.networkDroppedFrames;
    dst.pacerDroppedFrames += src.pacerDroppedFrames;
    dst.totalReassemblyTime += src.totalReassemblyTime;
    dst.totalDecodeTime += src.totalDecodeTime;
    dst.totalPacerTime += src.totalPacerTime;
    dst.totalRenderTime += src.totalRenderTime;

    if (!LiGetEstimatedRttInfo(&dst.lastRtt, &dst.lastRttVariance)) {
        dst.lastRtt = 0;
        dst.lastRttVariance = 0;
    }
    else {
        // Our logic to determine if RTT is valid depends on us never
        // getting an RTT of 0. ENet currently ensures RTTs are >= 1.
        SDL_assert(dst.lastRtt > 0);
    }

    Uint32 now = SDL_GetTicks();

    // Initialize the measurement start point if this is the first video stat window
    if (!dst.measurementStartTimestamp) {
        dst.measurementStartTimestamp = src.measurementStartTimestamp;
    }

    // The following code assumes the global measure was already started first
    SDL_assert(dst.measurementStartTimestamp <= src.measurementStartTimestamp);

    dst.totalFps = (float)dst.totalFrames / ((float)(now - dst.measurementStartTimestamp) / 1000);
    dst.receivedFps = (float)dst.receivedFrames / ((float)(now - dst.measurementStartTimestamp) / 1000);
    dst.decodedFps = (float)dst.decodedFrames / ((float)(now - dst.measurementStartTimestamp) / 1000);
    dst.renderedFps = (float)dst.renderedFrames / ((float)(now - dst.measurementStartTimestamp) / 1000);
}

int FFmpegVideoDecoder::collectMetrics(float* metrics, int size)
{
    VIDEO_STATS lastTwoWndStats = {};
    addVideoStats(m_LastWndVideoStats, lastTwoWndStats);
    addVideoStats(m_ActiveWndVideoStats, lastTwoWndStats);
    auto& stats = lastTwoWndStats;
    int i = 0;
    metrics[i++] = stats.totalFps;
    metrics[i++] = stats.receivedFps;
    metrics[i++] = stats.decodedFps;
    metrics[i++] = stats.renderedFps;
    metrics[i++] = stats.lastRtt;
    metrics[i++] = stats.lastRttVariance;
    metrics[i++] = (float)stats.networkDroppedFrames / stats.totalFrames * 100;
    metrics[i++] = (float)stats.pacerDroppedFrames / stats.decodedFrames * 100;
    metrics[i++] = (float)stats.totalDecodeTime / stats.decodedFrames;
    metrics[i++] = (float)stats.totalPacerTime / stats.renderedFrames;
    metrics[i++] = (float)stats.totalRenderTime / stats.renderedFrames;
    return i;
}
void FFmpegVideoDecoder::stringifyVideoStats(VIDEO_STATS& stats, char* output)
{
    int offset = 0;
    const char* codecString;

    // Start with an empty string
    output[offset] = 0;

    switch (m_VideoFormat)
    {
    case VIDEO_FORMAT_H264:
        codecString = "H.264";
        break;

    case VIDEO_FORMAT_H265:
        codecString = "HEVC";
        break;

    case VIDEO_FORMAT_H265_MAIN10:
        if (LiGetCurrentHostDisplayHdrMode()) {
            codecString = "HEVC Main 10 HDR";
        }
        else {
            codecString = "HEVC Main 10 SDR";
        }
        break;

    default:
        SDL_assert(false);
        codecString = "UNKNOWN";
        break;
    }

    if (stats.receivedFps > 0) {
        if (m_VideoDecoderCtx != nullptr) {
            offset += sprintf(&output[offset],
                              "Video stream: %dx%d %.2f FPS (Codec: %s)\n",
                              m_VideoDecoderCtx->width,
                              m_VideoDecoderCtx->height,
                              stats.totalFps,
                              codecString);
        }

        offset += sprintf(&output[offset],
                          "Incoming frame rate from network: %.2f FPS\n"
                          "Decoding frame rate: %.2f FPS\n"
                          "Rendering frame rate: %.2f FPS\n",
                          stats.receivedFps,
                          stats.decodedFps,
                          stats.renderedFps);
    }

    if (stats.renderedFrames != 0) {
        char rttString[32];

        if (stats.lastRtt != 0) {
            sprintf(rttString, "%u ms (variance: %u ms)", stats.lastRtt, stats.lastRttVariance);
        }
        else {
            sprintf(rttString, "N/A");
        }

        offset += sprintf(&output[offset],
                          "Display:%d\n"
                          "Frames dropped by your network connection: %.2f%%\n"
                          "Frames dropped due to network jitter: %.2f%%\n"
                          "Average network latency: %s\n"
                          "Average decoding time: %.2f ms\n"
                          "Average frame queue delay: %.2f ms\n"
                          "Average rendering time (including monitor V-sync latency): %.2f ms\n",
                          m_streamId,
                          (float)stats.networkDroppedFrames / stats.totalFrames * 100,
                          (float)stats.pacerDroppedFrames / stats.decodedFrames * 100,
                          rttString,
                          (float)stats.totalDecodeTime / stats.decodedFrames,
                          (float)stats.totalPacerTime / stats.renderedFrames,
                          (float)stats.totalRenderTime / stats.renderedFrames);
    }
}

void FFmpegVideoDecoder::logVideoStats(VIDEO_STATS& stats, const char* title)
{
    if (stats.renderedFps > 0 || stats.renderedFrames != 0) {
        char videoStatsStr[512];
        stringifyVideoStats(stats, videoStatsStr);

        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "%s", title);
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "----------------------------------------------------------\n%s",
                    videoStatsStr);
    }
}

IFFmpegRenderer* FFmpegVideoDecoder::createHwAccelRenderer(const AVCodecHWConfig* hwDecodeCfg, int pass)
{
    if (!(hwDecodeCfg->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX)) {
        return nullptr;
    }

    // First pass using our top-tier hwaccel implementations
    if (pass == 0) {
        switch (hwDecodeCfg->device_type) {
#ifdef Q_OS_WIN32
        // DXVA2 appears in the hwaccel list before D3D11VA, so we will prefer it.
        //
        // There is logic in DXVA2 that may elect to fail on the first selection pass
        // to allow D3D11VA to be used in cases where it is known to be better.
        case AV_HWDEVICE_TYPE_DXVA2:
            return new DXVA2Renderer(pass);
        case AV_HWDEVICE_TYPE_D3D11VA:
            return new D3D11VARenderer(pass);
#endif
#ifdef Q_OS_DARWIN
        case AV_HWDEVICE_TYPE_VIDEOTOOLBOX:
            return VTRendererFactory::createRenderer();
#endif
#ifdef HAVE_LIBVA
        case AV_HWDEVICE_TYPE_VAAPI:
            return new VAAPIRenderer(pass);
#endif
#ifdef HAVE_LIBVDPAU
        case AV_HWDEVICE_TYPE_VDPAU:
            return new VDPAURenderer();
#endif
#ifdef HAVE_DRM
        case AV_HWDEVICE_TYPE_DRM:
            return new DrmRenderer(true);
#endif
        default:
            return nullptr;
        }
    }
    // Second pass for our second-tier hwaccel implementations
    else if (pass == 1) {
        switch (hwDecodeCfg->device_type) {
#ifdef HAVE_CUDA
        case AV_HWDEVICE_TYPE_CUDA:
            // CUDA should only be used to cover the NVIDIA+Wayland case
            return new CUDARenderer();
#endif
#ifdef Q_OS_WIN32
        // This gives DXVA2 and D3D11VA another shot at handling cases where they
        // chose to purposefully fail in the first selection pass to allow a more
        // optimal decoder to be tried.
        case AV_HWDEVICE_TYPE_DXVA2:
            return new DXVA2Renderer(pass);
        case AV_HWDEVICE_TYPE_D3D11VA:
            return new D3D11VARenderer(pass);
#endif
#ifdef HAVE_LIBVA
        case AV_HWDEVICE_TYPE_VAAPI:
            return new VAAPIRenderer(pass);
#endif
#ifdef HAVE_LIBVDPAU
        case AV_HWDEVICE_TYPE_VDPAU:
            return new VDPAURenderer();
#endif
        default:
            return nullptr;
        }
    }
    else {
        SDL_assert(false);
        return nullptr;
    }
}

bool FFmpegVideoDecoder::tryInitializeRenderer(const AVCodec* decoder,
                                               PDECODER_PARAMETERS params,
                                               const AVCodecHWConfig* hwConfig,
                                               std::function<IFFmpegRenderer*()> createRendererFunc)
{
    m_HwDecodeCfg = hwConfig;

    // i == 0 - Indirect via EGL or DRM frontend with zero-copy DMA-BUF passing
    // i == 1 - Direct rendering or indirect via SDL read-back
#ifdef HAVE_EGL
    for (int i = 0; i < 2; i++) {
#else
    for (int i = 1; i < 2; i++) {
#endif
        SDL_assert(m_BackendRenderer == nullptr);
        if ((m_BackendRenderer = createRendererFunc()) != nullptr &&
                m_BackendRenderer->initialize(params) &&
                completeInitialization(decoder, params, m_TestOnly || m_BackendRenderer->needsTestFrame(), i == 0 /* EGL/DRM */)) {
            if (m_TestOnly) {
                // This decoder is only for testing capabilities, so don't bother
                // creating a usable renderer
                return true;
            }

            if (m_BackendRenderer->needsTestFrame()) {
                // The test worked, so now let's initialize it for real
                reset();
                if ((m_BackendRenderer = createRendererFunc()) != nullptr &&
                        m_BackendRenderer->initialize(params) &&
                        completeInitialization(decoder, params, false, i == 0 /* EGL/DRM */)) {
                    return true;
                }
                else {
                    SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION,
                                    "Decoder failed to initialize after successful test");
                    reset();
                }
            }
            else {
                // No test required. Good to go now.
                return true;
            }
        }
        else {
            // Failed to initialize, so keep looking
            reset();
        }
    }

    // reset() must be called before we reach this point!
    SDL_assert(m_BackendRenderer == nullptr);
    return false;
}

#define TRY_PREFERRED_PIXEL_FORMAT(RENDERER_TYPE) \
    { \
        RENDERER_TYPE renderer; \
        if (renderer.getPreferredPixelFormat(params->videoFormat) == decoder->pix_fmts[i]) { \
            if (tryInitializeRenderer(decoder, params, nullptr, \
                                      []() -> IFFmpegRenderer* { return new RENDERER_TYPE(); })) { \
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, \
                            "Chose " #RENDERER_TYPE " for codec %s due to preferred pixel format: 0x%x", \
                            decoder->name, decoder->pix_fmts[i]); \
                return true; \
            } \
        } \
    }

#define TRY_SUPPORTED_NON_PREFERRED_PIXEL_FORMAT(RENDERER_TYPE) \
    { \
        RENDERER_TYPE renderer; \
        if (decoder->pix_fmts[i] != renderer.getPreferredPixelFormat(params->videoFormat) && \
            renderer.isPixelFormatSupported(params->videoFormat, decoder->pix_fmts[i])) { \
            if (tryInitializeRenderer(decoder, params, nullptr, \
                                      []() -> IFFmpegRenderer* { return new RENDERER_TYPE(); })) { \
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, \
                            "Chose " #RENDERER_TYPE " for codec %s due to compatible pixel format: 0x%x", \
                            decoder->name, decoder->pix_fmts[i]); \
                return true; \
            } \
        } \
    }

bool FFmpegVideoDecoder::tryInitializeRendererForDecoderByName(const char *decoderName,
                                                               PDECODER_PARAMETERS params)
{
    const AVCodec* decoder = avcodec_find_decoder_by_name(decoderName);
    if (decoder == nullptr) {
        return false;
    }

    // This might be a hwaccel decoder, so try any hw configs first
    for (int i = 0;; i++) {
        const AVCodecHWConfig *config = avcodec_get_hw_config(decoder, i);
        if (!config) {
            // No remaing hwaccel options
            break;
        }

        // Initialize the hardware codec and submit a test frame if the renderer needs it
        if (tryInitializeRenderer(decoder, params, config,
                                  [config]() -> IFFmpegRenderer* { return createHwAccelRenderer(config, 0); })) {
            return true;
        }
    }

    if (decoder->pix_fmts == NULL) {
        // Supported output pixel formats are unknown. We'll just try DRM/SDL and hope it can cope.

#if defined(HAVE_DRM) && defined(GL_IS_SLOW)
        if (tryInitializeRenderer(decoder, params, nullptr,
                                  []() -> IFFmpegRenderer* { return new DrmRenderer(); })) {
            return true;
        }
#endif

        if (tryInitializeRenderer(decoder, params, nullptr,
                                  []() -> IFFmpegRenderer* { return new SdlRenderer(); })) {
            return true;
        }

        return false;
    }

#ifdef HAVE_MMAL
    // HACK: Avoid using YUV420P on h264_mmal. It can cause a deadlock inside the MMAL libraries.
    // Even if it didn't completely deadlock us, the performance would likely be atrocious.
    if (strcmp(decoderName, "h264_mmal") == 0) {
        for (int i = 0; decoder->pix_fmts[i] != AV_PIX_FMT_NONE; i++) {
            TRY_PREFERRED_PIXEL_FORMAT(MmalRenderer);
        }

        for (int i = 0; decoder->pix_fmts[i] != AV_PIX_FMT_NONE; i++) {
            TRY_SUPPORTED_NON_PREFERRED_PIXEL_FORMAT(MmalRenderer);
        }

        // Give up if we can't use MmalRenderer for h264_mmal
        return false;
    }
#endif

    // Check if any of our decoders prefer any of the pixel formats first
    for (int i = 0; decoder->pix_fmts[i] != AV_PIX_FMT_NONE; i++) {
#ifdef HAVE_DRM
        TRY_PREFERRED_PIXEL_FORMAT(DrmRenderer);
#endif
#ifndef GL_IS_SLOW
        TRY_PREFERRED_PIXEL_FORMAT(SdlRenderer);
#endif
    }

    // Nothing prefers any of them. Let's see if anyone will tolerate one.
    for (int i = 0; decoder->pix_fmts[i] != AV_PIX_FMT_NONE; i++) {
#ifdef HAVE_DRM
        TRY_SUPPORTED_NON_PREFERRED_PIXEL_FORMAT(DrmRenderer);
#endif
#ifndef GL_IS_SLOW
        TRY_SUPPORTED_NON_PREFERRED_PIXEL_FORMAT(SdlRenderer);
#endif
    }

#ifdef GL_IS_SLOW
    // If we got here with GL_IS_SLOW, DrmRenderer didn't work, so we have
    // to resort to SdlRenderer.
    for (int i = 0; decoder->pix_fmts[i] != AV_PIX_FMT_NONE; i++) {
        TRY_PREFERRED_PIXEL_FORMAT(SdlRenderer);
    }
    for (int i = 0; decoder->pix_fmts[i] != AV_PIX_FMT_NONE; i++) {
        TRY_SUPPORTED_NON_PREFERRED_PIXEL_FORMAT(SdlRenderer);
    }
#endif

    // If we made it here, we couldn't find anything
    return false;
}

bool FFmpegVideoDecoder::initialize(PDECODER_PARAMETERS params)
{
    // Increase log level until the first frame is decoded
    av_log_set_level(AV_LOG_DEBUG);

    // First try decoders that the user has manually specified via environment variables.
    // These must output surfaces in one of the formats that one of our renderers supports,
    // which is currently:
    // - AV_PIX_FMT_DRM_PRIME
    // - AV_PIX_FMT_MMAL
    // - AV_PIX_FMT_YUV420P
    // - AV_PIX_FMT_YUVJ420P
    // - AV_PIX_FMT_NV12
    // - AV_PIX_FMT_NV21
    {
        QString h264DecoderHint = qgetenv("H264_DECODER_HINT");
        if (!h264DecoderHint.isEmpty() && (params->videoFormat & VIDEO_FORMAT_MASK_H264)) {
            QByteArray decoderString = h264DecoderHint.toLocal8Bit();
            if (tryInitializeRendererForDecoderByName(decoderString.constData(), params)) {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                            "Using custom H.264 decoder (H264_DECODER_HINT): %s",
                            decoderString.constData());
                return true;
            }
            else {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                             "Custom H.264 decoder (H264_DECODER_HINT) failed to load: %s",
                             decoderString.constData());
            }
        }
    }
    {
        QString hevcDecoderHint = qgetenv("HEVC_DECODER_HINT");
        if (!hevcDecoderHint.isEmpty() && (params->videoFormat & VIDEO_FORMAT_MASK_H265)) {
            QByteArray decoderString = hevcDecoderHint.toLocal8Bit();
            if (tryInitializeRendererForDecoderByName(decoderString.constData(), params)) {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                            "Using custom HEVC decoder (HEVC_DECODER_HINT): %s",
                            decoderString.constData());
                return true;
            }
            else {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                             "Custom HEVC decoder (HEVC_DECODER_HINT) failed to load: %s",
                             decoderString.constData());
            }
        }
    }

    const AVCodec* decoder;

    if (params->videoFormat & VIDEO_FORMAT_MASK_H264) {
        decoder = avcodec_find_decoder(AV_CODEC_ID_H264);
    }
    else if (params->videoFormat & VIDEO_FORMAT_MASK_H265) {
        decoder = avcodec_find_decoder(AV_CODEC_ID_HEVC);
    }
    else {
        Q_ASSERT(false);
        decoder = nullptr;
    }

    if (!decoder) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Unable to find decoder for format: %x",
                     params->videoFormat);
        return false;
    }

    // Look for a hardware decoder first unless software-only
    if (params->vds != StreamingPreferences::VDS_FORCE_SOFTWARE) {
        // Look for the first matching hwaccel hardware decoder (pass 0)
        for (int i = 0;; i++) {
            const AVCodecHWConfig *config = avcodec_get_hw_config(decoder, i);
            if (!config) {
                // No remaing hwaccel options
                break;
            }

            // Initialize the hardware codec and submit a test frame if the renderer needs it
            if (tryInitializeRenderer(decoder, params, config,
                                      [config]() -> IFFmpegRenderer* { return createHwAccelRenderer(config, 0); })) {
                return true;
            }
        }

        // Continue with special non-hwaccel hardware decoders
        if (params->videoFormat & VIDEO_FORMAT_MASK_H264) {
            for (const codec_info_t& codecInfo : k_NonHwaccelH264Codecs) {
                if (tryInitializeRendererForDecoderByName(codecInfo.codec, params)) {
                    return true;
                }
            }
        }
        else {
            for (const codec_info_t& codecInfo : k_NonHwaccelHEVCCodecs) {
                if (tryInitializeRendererForDecoderByName(codecInfo.codec, params)) {
                    return true;
                }
            }
        }

        // Look for the first matching hwaccel hardware decoder (pass 1)
        // This picks up "second-tier" hwaccels like CUDA.
        for (int i = 0;; i++) {
            const AVCodecHWConfig *config = avcodec_get_hw_config(decoder, i);
            if (!config) {
                // No remaing hwaccel options
                break;
            }

            // Initialize the hardware codec and submit a test frame if the renderer needs it
            if (tryInitializeRenderer(decoder, params, config,
                                      [config]() -> IFFmpegRenderer* { return createHwAccelRenderer(config, 1); })) {
                return true;
            }
        }
    }

    // Fallback to software if no matching hardware decoder was found
    // and if software fallback is allowed
    if (params->vds != StreamingPreferences::VDS_FORCE_HARDWARE) {
        if (tryInitializeRenderer(decoder, params, nullptr,
                                  []() -> IFFmpegRenderer* { return new SdlRenderer(); })) {
            return true;
        }
    }

    // No decoder worked
    return false;
}

void FFmpegVideoDecoder::writeBuffer(PLENTRY entry, int& offset)
{
    if (m_NeedsSpsFixup && entry->bufferType == BUFFER_TYPE_SPS) {
        h264_stream_t* stream = h264_new();
        int nalStart, nalEnd;

        // Read the old NALU
        find_nal_unit((uint8_t*)entry->data, entry->length, &nalStart, &nalEnd);
        read_nal_unit(stream,
                      (unsigned char *)&entry->data[nalStart],
                      nalEnd - nalStart);

        SDL_assert(nalStart == 3 || nalStart == 4); // 3 or 4 byte Annex B start sequence
        SDL_assert(nalEnd == entry->length);

        // Fixup the SPS to what OS X needs to use hardware acceleration
        stream->sps->num_ref_frames = 1;
        stream->sps->vui.max_dec_frame_buffering = 1;

        int initialOffset = offset;

        // Copy the modified NALU data. This clobbers byte 0 and starts NALU data at byte 1.
        // Since it prepended one extra byte, subtract one from the returned length.
        offset += write_nal_unit(stream, (uint8_t*)&m_DecodeBuffer.data()[initialOffset + nalStart - 1],
                                 MAX_SPS_EXTRA_SIZE + entry->length - nalStart) - 1;

        // Copy the NALU prefix over from the original SPS
        memcpy(&m_DecodeBuffer.data()[initialOffset], entry->data, nalStart);
        offset += nalStart;

        h264_free(stream);
    }
    else {
        // Write the buffer as-is
        memcpy(&m_DecodeBuffer.data()[offset],
               entry->data,
               entry->length);
        offset += entry->length;
    }
}

int FFmpegVideoDecoder::decoderThreadProcThunk(void *context)
{
    ((FFmpegVideoDecoder*)context)->decoderThreadProc();
    return 0;
}

void FFmpegVideoDecoder::decoderThreadProc()
{
    while (!SDL_AtomicGet(&m_DecoderThreadShouldQuit)) {
        if (m_FramesIn == m_FramesOut) {
            VIDEO_FRAME_HANDLE handle;
            PDECODE_UNIT du;

            // Waiting for input. All output frames have been received.
            // Block until we receive a new frame from the host.
            if (!LiWaitForNextVideoFrame(m_streamId,&handle, &du)) {
                // This might be a signal from the main thread to exit
                continue;
            }
            if(du->streamId ==2) {
                int x =1;
                x+=1;
            }
            LiCompleteVideoFrame(m_streamId,handle, submitDecodeUnit(du));
        }

        if (m_FramesIn != m_FramesOut) {
            SDL_assert(m_FramesIn > m_FramesOut);

            // We have output frames to receive. Let's poll until we get one,
            // and submit new input data if/when we get it.
            AVFrame* frame = av_frame_alloc();
            if (!frame) {
                // Failed to allocate a frame but we did submit,
                // so we can return DR_OK
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                            "Failed to allocate frame");
                continue;
            }

            int err;
            do {
                err = avcodec_receive_frame(m_VideoDecoderCtx, frame);
                if (err == 0) {
                    SDL_assert(m_FrameInfoQueue.size() == m_FramesIn - m_FramesOut);
                    m_FramesOut++;

                    // Reset failed decodes count if we reached this far
                    m_ConsecutiveFailedDecodes = 0;

                    // Restore default log level after a successful decode
                    av_log_set_level(AV_LOG_INFO);

                    // Capture a frame timestamp to measuring pacing delay
                    frame->pkt_dts = SDL_GetTicks();

                    if (!m_FrameInfoQueue.isEmpty()) {
                        // Data buffers in the DU are not valid here!
                        DECODE_UNIT du = m_FrameInfoQueue.dequeue();

                        // Count time in avcodec_send_packet() and avcodec_receive_frame()
                        // as time spent decoding. Also count time spent in the decode unit
                        // queue because that's directly caused by decoder latency.
                        m_ActiveWndVideoStats.totalDecodeTime += LiGetMillis() - du.enqueueTimeMs;

                        // Store the presentation time
                        frame->pts = du.presentationTimeMs;
                    }

                    m_ActiveWndVideoStats.decodedFrames++;

                    // Queue the frame for rendering (or render now if pacer is disabled)
                    m_Pacer->submitFrame(frame);
                }
                else if (err == AVERROR(EAGAIN)) {
                    VIDEO_FRAME_HANDLE handle;
                    PDECODE_UNIT du;

                    // No output data, so let's try to submit more input data,
                    // while we're waiting for this to frame to come back.
                    if (LiPollNextVideoFrame(m_streamId,&handle, &du)) {
                        // FIXME: Handle EAGAIN on avcodec_send_packet() properly?
                        LiCompleteVideoFrame(m_streamId,handle, submitDecodeUnit(du));
                    }
                    else {
                        // No output data or input data. Let's wait a little bit.
                        SDL_Delay(2);
                    }
                }
                else {
                    char errorstring[512];

                    // FIXME: Should we pop an entry off m_FrameInfoQueue here?

                    av_strerror(err, errorstring, sizeof(errorstring));
                    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                                "avcodec_receive_frame() failed: %s (frame %d)",
                                errorstring,
                                !m_FrameInfoQueue.isEmpty() ? m_FrameInfoQueue.head().frameNumber : -1);

                    if (++m_ConsecutiveFailedDecodes == FAILED_DECODES_RESET_THRESHOLD) {
                        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                                     "Resetting decoder due to consistent failure");

                        SDL_Event event;
                        event.type = SDL_RENDER_DEVICE_RESET;
                        SDL_PushEvent(&event);

                        // Don't consume any additional data
                        SDL_AtomicSet(&m_DecoderThreadShouldQuit, 1);
                    }

                    // Just in case the error resulted in the loss of the frame,
                    // request an IDR frame to reset our decoder state.
                    LiRequestIdrFrame(m_streamId);
                }
            } while (err == AVERROR(EAGAIN) && !SDL_AtomicGet(&m_DecoderThreadShouldQuit));

            if (err != 0) {
                // Free the frame if we failed to submit it
                av_frame_free(&frame);
            }
        }
    }
}

int FFmpegVideoDecoder::submitDecodeUnit(PDECODE_UNIT du)
{
    PLENTRY entry = du->bufferList;
    int err;

    SDL_assert(!m_TestOnly);

    // If this is the first frame, reject anything that's not an IDR frame
    if (m_FramesIn == 0 && du->frameType != FRAME_TYPE_IDR) {
        return DR_NEED_IDR;
    }

    if (!m_LastFrameNumber) {
        m_ActiveWndVideoStats.measurementStartTimestamp = SDL_GetTicks();
        m_LastFrameNumber = du->frameNumber;
    }
    else {
        // Any frame number greater than m_LastFrameNumber + 1 represents a dropped frame
        m_ActiveWndVideoStats.networkDroppedFrames += du->frameNumber - (m_LastFrameNumber + 1);
        m_ActiveWndVideoStats.totalFrames += du->frameNumber - (m_LastFrameNumber + 1);
        m_LastFrameNumber = du->frameNumber;
    }

    // Flip stats windows roughly every second
    if (SDL_TICKS_PASSED(SDL_GetTicks(), m_ActiveWndVideoStats.measurementStartTimestamp + 1000)) {
        // Update overlay stats if it's enabled
        if (Session::get()->getOverlayManager(m_streamId).isOverlayEnabled(Overlay::OverlayDebug)) {
            VIDEO_STATS lastTwoWndStats = {};
            addVideoStats(m_LastWndVideoStats, lastTwoWndStats);
            addVideoStats(m_ActiveWndVideoStats, lastTwoWndStats);

            stringifyVideoStats(lastTwoWndStats, Session::get()->getOverlayManager(m_streamId).getOverlayText(Overlay::OverlayDebug));
            Session::get()->getOverlayManager(m_streamId).setOverlayTextUpdated(Overlay::OverlayDebug);
        }

        // Accumulate these values into the global stats
        addVideoStats(m_ActiveWndVideoStats, m_GlobalVideoStats);

        // Move this window into the last window slot and clear it for next window
        SDL_memcpy(&m_LastWndVideoStats, &m_ActiveWndVideoStats, sizeof(m_ActiveWndVideoStats));
        SDL_zero(m_ActiveWndVideoStats);
        m_ActiveWndVideoStats.measurementStartTimestamp = SDL_GetTicks();
    }

    m_ActiveWndVideoStats.receivedFrames++;
    m_ActiveWndVideoStats.totalFrames++;

    int requiredBufferSize = du->fullLength;
    if (du->frameType == FRAME_TYPE_IDR) {
        // Add some extra space in case we need to do an SPS fixup
        requiredBufferSize += MAX_SPS_EXTRA_SIZE;
    }

    // Ensure the decoder buffer is large enough
    m_DecodeBuffer.reserve(requiredBufferSize + AV_INPUT_BUFFER_PADDING_SIZE);

    int offset = 0;
    while (entry != nullptr) {
        writeBuffer(entry, offset);
        entry = entry->next;
    }

    m_Pkt->data = reinterpret_cast<uint8_t*>(m_DecodeBuffer.data());
    m_Pkt->size = offset;

    if (du->frameType == FRAME_TYPE_IDR) {
        m_Pkt->flags = AV_PKT_FLAG_KEY;
    }
    else {
        m_Pkt->flags = 0;
    }

    m_ActiveWndVideoStats.totalReassemblyTime += du->enqueueTimeMs - du->receiveTimeMs;

    err = avcodec_send_packet(m_VideoDecoderCtx, m_Pkt);
    if (err < 0) {
        char errorstring[512];
        av_strerror(err, errorstring, sizeof(errorstring));
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "avcodec_send_packet() failed: %s (frame %d)",
                    errorstring,
                    du->frameNumber);

        // If we've failed a bunch of decodes in a row, the decoder/renderer is
        // clearly unhealthy, so let's generate a synthetic reset event to trigger
        // the event loop to destroy and recreate the decoder.
        if (++m_ConsecutiveFailedDecodes == FAILED_DECODES_RESET_THRESHOLD) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "Resetting decoder due to consistent failure");

            SDL_Event event;
            event.type = SDL_RENDER_DEVICE_RESET;
            SDL_PushEvent(&event);

            // Don't consume any additional data
            SDL_AtomicSet(&m_DecoderThreadShouldQuit, 1);
        }

        return DR_NEED_IDR;
    }

    m_FrameInfoQueue.enqueue(*du);

    m_FramesIn++;
    return DR_OK;
}

void FFmpegVideoDecoder::renderFrameOnMainThread()
{
    m_Pacer->renderOnMainThread();
}

