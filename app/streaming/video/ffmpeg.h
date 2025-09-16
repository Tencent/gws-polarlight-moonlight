#pragma once

#include <functional>
#include <QQueue>

#include "decoder.h"
#include "ffmpeg-renderers/renderer.h"
#include "ffmpeg-renderers/pacer/pacer.h"
#include <atomic>
#include <condition_variable>
#include <mutex>

extern "C" {
#include <libavcodec/avcodec.h>
}

class FFmpegVideoDecoder : public IVideoDecoder
{
    int m_streamId = -1;
public:
    FFmpegVideoDecoder(bool testOnly);
    virtual ~FFmpegVideoDecoder() override;
    virtual void SetStreamId(int streamId) override
    {
        m_streamId = streamId;
    }
    virtual bool initialize(PDECODER_PARAMETERS params) override;
    virtual bool isHardwareAccelerated() override;
    virtual bool isAlwaysFullScreen() override;
    virtual bool isHdrSupported() override;
    virtual int getDecoderCapabilities() override;
    virtual int getDecoderColorspace() override;
    virtual int getDecoderColorRange() override;
    virtual QSize getDecoderMaxResolution() override;
    virtual int submitDecodeUnit(PDECODE_UNIT du) override;
    virtual void renderFrameOnMainThread() override;
    virtual void setHdrMode(bool enabled) override;

    virtual IFFmpegRenderer* getBackendRenderer();
    void reset();
private:
    bool completeInitialization(const AVCodec* decoder, PDECODER_PARAMETERS params, bool testFrame, bool useAlternateFrontend);

    virtual int collectMetrics(float* metrics, int size) override;
    void stringifyVideoStats(VIDEO_STATS& stats, char* output);

    void logVideoStats(VIDEO_STATS& stats, const char* title);

    void addVideoStats(VIDEO_STATS& src, VIDEO_STATS& dst);

    bool createFrontendRenderer(PDECODER_PARAMETERS params, bool useAlternateFrontend);

    bool tryInitializeRendererForDecoderByName(const char* decoderName,
            PDECODER_PARAMETERS params);

    bool tryInitializeRenderer(const AVCodec* decoder,
                               PDECODER_PARAMETERS params,
                               const AVCodecHWConfig* hwConfig,
                               std::function<IFFmpegRenderer*()> createRendererFunc);

    static IFFmpegRenderer* createHwAccelRenderer(const AVCodecHWConfig* hwDecodeCfg, int pass);



    void writeBuffer(PLENTRY entry, int& offset);

    static
    enum AVPixelFormat ffGetFormat(AVCodecContext* context,
                                   const enum AVPixelFormat* pixFmts);

    //to pause this thread
    std::atomic<bool> m_pauseFlag = false;
    std::atomic<bool> m_paused = false;
    std::condition_variable m_cvPause;
    std::mutex m_mutexPause;
    virtual void WaitPaused() override {
        std::unique_lock<std::mutex> lock(m_mutexPause);
        m_cvPause.wait(lock, [this] { return m_paused || SDL_AtomicGet(&m_DecoderThreadShouldQuit); });
    }
    virtual void Pause() override {
        m_Pacer->Reset();
        std::unique_lock<std::mutex> lock(m_mutexPause);
        m_pauseFlag = true;
    }
    virtual void Wakeup() override {
        {
            std::lock_guard<std::mutex> lock(m_mutexPause);
            m_pauseFlag = false;
            m_paused = false;
        }
        m_cvPause.notify_all();
    }
    void decoderThreadProc();

    static int decoderThreadProcThunk(void* context);

    AVPacket* m_Pkt;
    AVCodecContext* m_VideoDecoderCtx;
    QByteArray m_DecodeBuffer;
    const AVCodecHWConfig* m_HwDecodeCfg;
    IFFmpegRenderer* m_BackendRenderer;
    IFFmpegRenderer* m_FrontendRenderer;
    int m_ConsecutiveFailedDecodes;
    Pacer* m_Pacer;
    VIDEO_STATS m_ActiveWndVideoStats;
    VIDEO_STATS m_LastWndVideoStats;
    VIDEO_STATS m_GlobalVideoStats;

    int m_FramesIn;
    int m_FramesOut;

    int m_LastFrameNumber;
    int m_StreamFps;
    int m_VideoFormat;
    bool m_NeedsSpsFixup;
    bool m_TestOnly;
    SDL_Thread* m_DecoderThread;
    SDL_atomic_t m_DecoderThreadShouldQuit;

    // Data buffers in the queued DU are not valid
    QQueue<DECODE_UNIT> m_FrameInfoQueue;

    static const uint8_t k_H264TestFrame[];
    static const uint8_t k_HEVCMainTestFrame[];
    static const uint8_t k_HEVCMain10TestFrame[];
};
