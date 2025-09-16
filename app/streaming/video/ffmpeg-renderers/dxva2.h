#pragma once

#include "renderer.h"
#include "pacer/pacer.h"

#include <d3d9.h>
#include <dxva2api.h>

extern "C" {
#include <libavcodec/dxva2.h>
}

class DXVA2Renderer : public IFFmpegRenderer
{
public:
    DXVA2Renderer(int decoderSelectionPass);
    virtual ~DXVA2Renderer() override;
    virtual bool initialize(PDECODER_PARAMETERS params) override;
    virtual bool prepareDecoderContext(AVCodecContext* context, AVDictionary** options) override;
    virtual void renderFrame(AVFrame* frame) override;
    virtual void notifyOverlayUpdated(Overlay::OverlayType type) override;
    virtual int getDecoderColorspace() override;
    virtual int getDecoderCapabilities() override;

private:
    bool initializeDecoder();
    bool initializeRenderer();
    bool initializeDevice(SDL_Window* window, bool enableVsync);
    bool isDecoderBlacklisted();
    bool initializeQuirksForAdapter(IDirect3D9Ex* d3d9ex, int adapterIndex);
    void renderOverlay(Overlay::OverlayType type);

#if LIBAVUTIL_VERSION_INT >= AV_VERSION_INT(56, 68, 0)
#define FF_POOL_SIZE_TYPE size_t
#else
#define FF_POOL_SIZE_TYPE int
#endif

    static
    AVBufferRef* ffPoolAlloc(void* opaque, FF_POOL_SIZE_TYPE size);

    static
    void ffPoolDummyDelete(void*, uint8_t*);

    static
    int ffGetBuffer2(AVCodecContext* context, AVFrame* frame, int flags);

    DECODER_PARAMETERS m_DecoderParams;
    int m_DecoderSelectionPass;

    int m_VideoFormat;
    int m_VideoWidth;
    int m_VideoHeight;

    int m_DisplayWidth;
    int m_DisplayHeight;

    struct dxva_context m_DXVAContext;
    IDirect3DSurface9* m_DecSurfaces[19];
    DXVA2_ConfigPictureDecode m_Config;
    IDirectXVideoDecoderService* m_DecService;
    IDirectXVideoDecoder* m_Decoder;
    int m_SurfacesUsed;
    AVBufferPool* m_Pool;

    SDL_SpinLock m_OverlayLock;
    IDirect3DVertexBuffer9* m_OverlayVertexBuffers[Overlay::OverlayMax];
    IDirect3DTexture9* m_OverlayTextures[Overlay::OverlayMax];

    IDirect3DDevice9Ex* m_Device;
    IDirect3DSurface9* m_RenderTarget;
    IDirectXVideoProcessorService* m_ProcService;
    IDirectXVideoProcessor* m_Processor;
    DXVA2_ValueRange m_BrightnessRange;
    DXVA2_ValueRange m_ContrastRange;
    DXVA2_ValueRange m_HueRange;
    DXVA2_ValueRange m_SaturationRange;
    DXVA2_VideoDesc m_Desc;
    REFERENCE_TIME m_FrameIndex;
    bool m_BlockingPresent;

#define DXVA2_QUIRK_NO_VP 0x01
#define DXVA2_QUIRK_SET_DEST_FORMAT 0x02
#define DXVA2_QUIRK_WDDM_20_PLUS 0x04
#define DXVA2_QUIRK_MULTI_GPU 0x08
    int m_DeviceQuirks;
};
