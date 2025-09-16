#pragma once

#include "renderer.h"

// Avoid X11 if SDL was built without it
#if !defined(SDL_VIDEO_DRIVER_X11) && defined(HAVE_LIBVA_X11)
#warning Unable to use libva-x11 without SDL support
#undef HAVE_LIBVA_X11
#endif

// Avoid Wayland if SDL was built without it
#if !defined(SDL_VIDEO_DRIVER_WAYLAND) && defined(HAVE_LIBVA_WAYLAND)
#warning Unable to use libva-wayland without SDL support
#undef HAVE_LIBVA_WAYLAND
#endif

extern "C" {
#include <va/va.h>
#ifdef HAVE_LIBVA_X11
#include <va/va_x11.h>
#endif
#ifdef HAVE_LIBVA_WAYLAND
#include <va/va_wayland.h>
#endif
#ifdef HAVE_LIBVA_DRM
#include <va/va_drm.h>
#endif
#include <libavutil/hwcontext_vaapi.h>
#if defined(HAVE_EGL) || defined(HAVE_DRM)
#include <va/va_drmcommon.h>
#endif
}

class VAAPIRenderer : public IFFmpegRenderer
{
public:
    VAAPIRenderer(int decoderSelectionPass);
    virtual ~VAAPIRenderer() override;
    virtual bool initialize(PDECODER_PARAMETERS params) override;
    virtual bool prepareDecoderContext(AVCodecContext* context, AVDictionary** options) override;
    virtual void renderFrame(AVFrame* frame) override;
    virtual bool needsTestFrame() override;
    virtual bool isDirectRenderingSupported() override;
    virtual int getDecoderColorspace() override;
    virtual int getDecoderCapabilities() override;
    virtual void notifyOverlayUpdated(Overlay::OverlayType) override;

#ifdef HAVE_EGL
    virtual bool canExportEGL() override;
    virtual AVPixelFormat getEGLImagePixelFormat() override;
    virtual bool initializeEGL(EGLDisplay dpy, const EGLExtensions &ext) override;
    virtual ssize_t exportEGLImages(AVFrame *frame, EGLDisplay dpy, EGLImage images[EGL_MAX_PLANES]) override;
    virtual void freeEGLImages(EGLDisplay dpy, EGLImage[EGL_MAX_PLANES]) override;
#endif

#ifdef HAVE_DRM
    virtual bool canExportDrmPrime() override;
    virtual bool mapDrmPrimeFrame(AVFrame* frame, AVDRMFrameDescriptor* drmDescriptor) override;
    virtual void unmapDrmPrimeFrame(AVDRMFrameDescriptor* drmDescriptor) override;
#endif

private:
    VADisplay openDisplay(SDL_Window* window);
    void renderOverlay(VADisplay display, VASurfaceID surface, Overlay::OverlayType type);

#if defined(HAVE_EGL) || defined(HAVE_DRM)
    bool canExportSurfaceHandle(int layerTypeFlag);
#endif

    int m_DecoderSelectionPass;
    int m_WindowSystem;
    AVBufferRef* m_HwContext;
    bool m_BlacklistedForDirectRendering;
    bool m_HasRfiLatencyBug;

    SDL_mutex* m_OverlayMutex;
    VAImageFormat m_OverlayFormat;
    Uint32 m_OverlaySdlPixelFormat;
    VAImage m_OverlayImage[Overlay::OverlayMax];
    VASubpictureID m_OverlaySubpicture[Overlay::OverlayMax];
    SDL_Rect m_OverlayRect[Overlay::OverlayMax];

#ifdef HAVE_LIBVA_X11
    Window m_XWindow;
#endif

    int m_VideoWidth;
    int m_VideoHeight;
    int m_VideoFormat;
    int m_DisplayWidth;
    int m_DisplayHeight;

#ifdef HAVE_EGL
    VADRMPRIMESurfaceDescriptor m_PrimeDescriptor;
    bool m_EGLExtDmaBuf;
    PFNEGLCREATEIMAGEPROC m_eglCreateImage;
    PFNEGLDESTROYIMAGEPROC m_eglDestroyImage;
    PFNEGLCREATEIMAGEKHRPROC m_eglCreateImageKHR;
    PFNEGLDESTROYIMAGEKHRPROC m_eglDestroyImageKHR;
#endif
};
