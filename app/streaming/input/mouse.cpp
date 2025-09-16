#include "input.h"

#include <Limelight.h>
#include <SDL.h>
#include "streaming/streamutils.h"
#include "Core/displayInfo.h"

void SdlInputHandler::handleMouseButtonEvent(SDL_MouseButtonEvent* event)
{
    int button;
    int windowWidth, windowHeight;
    auto info = m_windowDispInfo;
    auto id = SDL_GetWindowID(m_Window2);
    if (event->windowID == id) {
        info = m_window2DispInfo;
    }

    SDL_GetWindowSize(SDL_GetWindowFromID(event->windowID), &windowWidth, &windowHeight);
    if (event->which == SDL_TOUCH_MOUSEID) {
        // Ignore synthetic mouse events
        return;
    }
    else if (!isCaptureActive()) {
        //qDebug()<<"handleMouseButtonEvent"<<event->x <<" "<<event->y;
        if (event->button == SDL_BUTTON_LEFT && event->state == SDL_RELEASED && info.name.size() != 0 &&
            isMouseInVideoRegion(event->x, event->y, windowWidth, windowHeight, info)) {
            // Capture the mouse again if clicked when unbound.
            // We start capture on left button released instead of
            // pressed to avoid sending an errant mouse button released
            // event to the host when clicking into our window (since
            // the pressed event was consumed by this code).
            setCaptureActive(true);
        }

        // Not capturing
        return;
    }
    else if (m_AbsoluteMouseMode &&
             (info.name.size() == 0 || !isMouseInVideoRegion(event->x, event->y, windowWidth, windowHeight, info)) &&
            event->state == SDL_PRESSED) {
        // Ignore button presses outside the video region, but allow button releases
        return;
    }

    switch (event->button)
    {
    case SDL_BUTTON_LEFT:
        button = BUTTON_LEFT;
        break;
    case SDL_BUTTON_MIDDLE:
        button = BUTTON_MIDDLE;
        break;
    case SDL_BUTTON_RIGHT:
        button = BUTTON_RIGHT;
        break;
    case SDL_BUTTON_X1:
        button = BUTTON_X1;
        break;
    case SDL_BUTTON_X2:
        button = BUTTON_X2;
        break;
    default:
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "Unhandled button event: %d",
                    event->button);
        return;
    }

    if (m_SwapMouseButtons) {
        if (button == BUTTON_RIGHT)
            button = BUTTON_LEFT;
        else if (button == BUTTON_LEFT)
            button = BUTTON_RIGHT;
    }

    LiSendMouseButtonEvent(event->state == SDL_PRESSED ?
                                BUTTON_ACTION_PRESS :
                                BUTTON_ACTION_RELEASE,
                            button);
}

SDL_Rect SdlInputHandler::transVideoRegion(SDL_MouseMotionEvent* event, bool &mouseInVideoRegion) {
    int windowWidth, windowHeight;
    SDL_Window *win = SDL_GetWindowFromID(event->windowID);
    SDL_GetWindowSize(win, &windowWidth, &windowHeight);
    SDL_Rect dst;

    auto info = m_windowDispInfo;
    auto id = SDL_GetWindowID(m_Window2);
    if (event->windowID == id) {
        info= m_window2DispInfo;
    }

    // Use the stream and window sizes to determine the video region
    mouseInVideoRegion = isMouseInVideoRegion(event->x,
                                              event->y,
                                              windowWidth,
                                              windowHeight, info, dst);

    // Clamp motion to the video region
    short x = qMin(qMax(event->x - dst.x, 0), dst.w);
    short y = qMin(qMax(event->y - dst.y, 0), dst.h);

    // qDebug() << "cal temp" << x << "," << y << ","<< "\n"
    //          << info.x << "," << info.y << "," << "\n"
    //          << info.width << "," << info.height << ","<< "\n"
    //          << dst.w << "," << dst.h << ",";
    x = info.x + (long long)x * info.width /(float)dst.w;
    y = info.y + (long long)y * info.height/(float)dst.h;

    return SDL_Rect{x, y, dst.w, dst.h};
}

void SdlInputHandler::getAnotherWindowRectAndMousePoint(SDL_Window* anotherWin, SDL_Rect& rect)
{
    if (!anotherWin) return;
    SDL_GetWindowPosition(anotherWin, &rect.x, &rect.y);
    SDL_GetWindowSize(anotherWin, &rect.w, &rect.h);

#ifdef Q_OS_DARWIN
    if (SDL_GetWindowFlags(anotherWin) & SDL_WINDOW_FULLSCREEN) {
        int displayIndex = SDL_GetWindowDisplayIndex(anotherWin);
        SDL_Rect displayBounds;
        SDL_GetDisplayBounds(displayIndex, &displayBounds);
        if (rect.h != displayBounds.h) {
            rect.y += (displayBounds.h - rect.h);
        }
    }
#endif
}

void SdlInputHandler::handleAbsoluteMouseMotion(SDL_MouseMotionEvent* event, GleamCore::DisplayInfo& info)
{
    SDL_Window *win = SDL_GetWindowFromID(event->windowID);
    SDL_Window * activeWin = getActiveSDLWindow();
    if (activeWin == nullptr) {
        setCaptureActive(false);
        return;
    }

    bool mouseInVideoRegion;
    SDL_Rect resRect = transVideoRegion(event, mouseInVideoRegion);
    // qDebug() << "resRect:" << resRect.x << "," << resRect.y << ","<< resRect.w << "," << resRect.h;

    // qDebug() << "cal finall" << x << "," << y;

    // Send the mouse position update if one of the following is true:
    // a) it is in the video region now
    // b) it just left the video region (to ensure the mouse is clamped to the video boundary)
    // c) a mouse button is still down from before the cursor left the video region (to allow smooth dragging)
    SDL_Window *anotherWin = (win == m_Window ? m_Window2: m_Window);
    SDL_Rect anotherWinRect;
    SDL_Point mousePoint;

    getAnotherWindowRectAndMousePoint(anotherWin, anotherWinRect);
    Uint32 buttonState = SDL_GetGlobalMouseState(&mousePoint.x, &mousePoint.y);
    bool mouseInAnotherWin = (anotherWin == nullptr ? false : SDL_PointInRect(&mousePoint, &anotherWinRect));

    if (buttonState == 0) {
        if (m_PendingMouseButtonsAllUpOnVideoRegionLeave) {
            // Stop capturing the mouse now
            SDL_CaptureMouse(SDL_FALSE);
            m_PendingMouseButtonsAllUpOnVideoRegionLeave = false;
        }
    } else {
        if (!mouseInVideoRegion) {
            if (mouseInAnotherWin) {
#ifdef Q_OS_DARWIN
                SDL_MouseMotionEvent tranEvent;
                tranEvent.x = mousePoint.x - anotherWinRect.x;
                tranEvent.y = mousePoint.y - anotherWinRect.y;
                tranEvent.windowID = SDL_GetWindowID(anotherWin);
                tranEvent.which = 0;
                bool mouseInAnotherVideoRegion;
                SDL_Rect transRect = transVideoRegion(&tranEvent, mouseInAnotherVideoRegion);
                if (mouseInAnotherVideoRegion) {
                    LiSendMousePositionEvent(transRect.x, transRect.y, transRect.w, transRect.h);
                }
                m_MouseWasInVideoRegion = mouseInVideoRegion;
                return;
#else
                SDL_RaiseWindow(anotherWin);
#endif
            }
        }
#ifdef Q_OS_DARWIN
        else if (anotherWin != nullptr) {
            int windowWidth, windowHeight;
            SDL_GetWindowSize(SDL_GetWindowFromID(event->windowID), &windowWidth, &windowHeight);
            int dragBorder = (std::max)(40, (int)(windowHeight * 0.04));
            if (event->x < dragBorder ||
                event->y < dragBorder ||
                event->x > windowWidth - dragBorder ||
                event->y > windowHeight - dragBorder) {
                m_MouseWasInDragBorder = true;
                SDL_ShowCursor(SDL_TRUE);
            } else {
                m_MouseWasInDragBorder = false;
                SDL_ShowCursor(SDL_FALSE);
            }
        }
#endif
    }

    // qDebug() << (activeWin != win) <<  ", " << (activeWin == m_Window);
    static int cnt = 0;
    if (activeWin != nullptr && activeWin != win) {
        if (cnt++ > 1) {
            qDebug() << "raise windows: " << (win == m_Window ? 1 : 2);
            SDL_RaiseWindow(win);
        }

    } else {
        cnt = 0;
    }

    if (mouseInVideoRegion || m_MouseWasInVideoRegion || m_PendingMouseButtonsAllUpOnVideoRegionLeave) {
        LiSendMousePositionEvent(resRect.x, resRect.y, resRect.w, resRect.h);
    }

    // Adjust the cursor visibility if applicable
    if ((buttonState == 0 && m_MouseWasInDragBorder) || (mouseInVideoRegion ^ m_MouseWasInVideoRegion)) {
        m_MouseWasInDragBorder = false;
        SDL_ShowCursor((mouseInVideoRegion && m_MouseCursorCapturedVisibilityState == SDL_DISABLE) ? SDL_DISABLE : SDL_ENABLE);
        if (!mouseInVideoRegion && buttonState != 0) {
            // If we still have a button pressed on leave, wait for that to come up
            // before we stop sending mouse position events.
            m_PendingMouseButtonsAllUpOnVideoRegionLeave = true;
        }
    }

    m_MouseWasInVideoRegion = mouseInVideoRegion;
}

void SdlInputHandler::handleMouseMotionEvent(SDL_MouseMotionEvent* event)
{
    // qDebug()<<"handleMouseMotionEvent: "<< (SDL_GetWindowFromID(event->windowID) == m_Window ? 1 : 2);
    if (!isCaptureActive() || // Not capturing
        event->which == SDL_TOUCH_MOUSEID) { // Ignore synthetic mouse events
        return;
    }

    auto info = m_windowDispInfo;
    auto id = SDL_GetWindowID(m_Window2);
    if (event->windowID == id) {
        info= m_window2DispInfo;
    }
    if (m_AbsoluteMouseMode && info.name.size() != 0) {
        handleAbsoluteMouseMotion(event, info);
    }
    else {
        LiSendMouseMoveEvent(event->xrel, event->yrel);
    }
}

void SdlInputHandler::handleMouseWheelEvent(SDL_MouseWheelEvent* event)
{
    if (!isCaptureActive()) {
        // Not capturing
        return;
    }
    else if (event->which == SDL_TOUCH_MOUSEID) {
        // Ignore synthetic mouse events
        return;
    }
    int windowWidth, windowHeight;
    auto info = m_windowDispInfo;
    auto id = SDL_GetWindowID(m_Window2);
    if (event->windowID == id) {
        info= m_window2DispInfo;
    }
    SDL_GetWindowSize(SDL_GetWindowFromID(event->windowID), &windowWidth, &windowHeight);
    if (m_AbsoluteMouseMode) {
        int mouseX, mouseY;
        SDL_GetMouseState(&mouseX, &mouseY);

        if (info.name.size() == 0) {
            return;
        }

        if (!isMouseInVideoRegion(mouseX, mouseY, windowWidth, windowHeight, info)) {
            // Ignore scroll events outside the video region
            return;
        }
    }

#if SDL_VERSION_ATLEAST(2, 0, 18)
    if (event->preciseY != 0.0f) {
        // Invert the scroll direction if needed
        if (m_ReverseScrollDirection) {
            event->preciseY = -event->preciseY;
        }

#ifdef Q_OS_DARWIN
        // HACK: Clamp the scroll values on macOS to prevent OS scroll acceleration
        // from generating wild scroll deltas when scrolling quickly.
        event->preciseY = SDL_clamp(event->preciseY, -1.0f, 1.0f);
#endif

        LiSendHighResScrollEvent((short)(event->preciseY * 120)); // WHEEL_DELTA
    }

    if (event->preciseX != 0.0f) {
        // Invert the scroll direction if needed
        if (m_ReverseScrollDirection) {
            event->preciseX = -event->preciseY;
        }

#ifdef Q_OS_DARWIN
        // HACK: Clamp the scroll values on macOS to prevent OS scroll acceleration
        // from generating wild scroll deltas when scrolling quickly.
        event->preciseX = SDL_clamp(event->preciseX, -1.0f, 1.0f);
#endif

        LiSendHighResHScrollEvent((short)(event->preciseX * 120)); // WHEEL_DELTA
    }
#else
    if (event->y != 0) {
        // Invert the scroll direction if needed
        if (m_ReverseScrollDirection) {
            event->y = -event->y;
        }

#ifdef Q_OS_DARWIN
        // See comment above
        event->y = SDL_clamp(event->y, -1, 1);
#endif

        LiSendScrollEvent((signed char)event->y);
    }

    if (event->x != 0) {
        // Invert the scroll direction if needed
        if (m_ReverseScrollDirection) {
            event->x = -event->x;
        }

#ifdef Q_OS_DARWIN
        // See comment above
        event->x = SDL_clamp(event->x, -1, 1);
#endif

        LiSendHScrollEvent((signed char)event->x);
    }
#endif
}

SDL_Rect mock_SDL_Rect;
bool SdlInputHandler::isMouseInVideoRegion(int mouseX, int mouseY, int windowWidth, int windowHeight,
    GleamCore::DisplayInfo &dispInfo, SDL_Rect &dst /*= mock_SDL_Rect*/)
{
    SDL_Rect src;//, dst;
    DXGI_MODE_ROTATION rotation = dispInfo.rotation;
    if (windowWidth < 0 || windowHeight < 0) {
        SDL_GetWindowSize(m_Window, &windowWidth, &windowHeight);
    }


    src.x = src.y = 0;
    if (dispInfo.x + m_minXY.first == 0 && dispInfo.y + m_minXY.second == 0) {
        src.w = m_streamConfig->width;
        src.h = m_streamConfig->height;
    } else {
        src.w = m_streamConfig->width2;
        src.h = m_streamConfig->height2;
    }


    if (GleamCore::IsVerticalRotation(rotation)) {
        std::swap(src.w, src.h);
    }

    dst.x = dst.y = 0;
    dst.w = windowWidth;
    dst.h = windowHeight;
    // qDebug() << "src.x = " << src.x << "src.y = " << src.y<< "src.w = " << src.w << "src.h = " << src.h;
    // Use the stream and window sizes to determine the video region
    StreamUtils::scaleSourceToDestinationSurface(&src, &dst);

    int oriWidth = dispInfo.width, oriHeight = dispInfo.height;
    if (GleamCore::IsVerticalRotation(rotation)) {
        std::swap(oriWidth, oriHeight);
    }

    if (src.w * oriHeight != src.h * oriWidth) {
        if (oriWidth * src.h > oriHeight * src.w) { // 100:9
            if (GleamCore::IsVerticalRotation(rotation)) {
                int realDispWidth = dst.h * oriHeight / oriWidth;
                dst.x += (dst.w - realDispWidth) / 2;
                dst.w = realDispWidth;

            } else {
                int realDispHeight = dst.w * oriHeight / oriWidth;
                dst.y += (dst.h - realDispHeight) / 2;
                dst.h = realDispHeight;
            }
        } else {//16:100
            if (GleamCore::IsVerticalRotation(rotation)) {
                int realDispHeight = dst.w * oriWidth / oriHeight;
                dst.y += (dst.h - realDispHeight) / 2;
                dst.h = realDispHeight;
            } else {
                int realDispWidth = dst.h * oriWidth / oriHeight;
                dst.x += (dst.w - realDispWidth) / 2;
                dst.w = realDispWidth;
            }
        }
    }

    return (mouseX >= dst.x && mouseX <= dst.x + dst.w) &&
           (mouseY >= dst.y && mouseY <= dst.y + dst.h);
}

void SdlInputHandler::updatePointerRegionLock()
{
    // Pointer region lock is irrelevant in relative mouse mode
    if (SDL_GetRelativeMouseMode()) {
        return;
    }

    // Our pointer lock behavior tracks with the fullscreen mode unless the user has
    // toggled it themselves using the keyboard shortcut. If that's the case, they
    // have full control over it and we don't touch it anymore.
    if (!m_PointerRegionLockToggledByUser) {
        // Lock the pointer in true full-screen mode and leave it unlocked in other modes
        m_PointerRegionLockActive = (SDL_GetWindowFlags(m_Window) & SDL_WINDOW_FULLSCREEN_DESKTOP) == SDL_WINDOW_FULLSCREEN;
    }

    // If region lock is enabled, grab the cursor so it can't accidentally leave our window.
    if (isCaptureActive() && m_PointerRegionLockActive) {
#if SDL_VERSION_ATLEAST(2, 0, 18)
        SDL_Rect src, dst;

        src.x = src.y = 0;
        src.w = m_streamConfig->width;
        src.h = m_streamConfig->height;

        dst.x = dst.y = 0;
        SDL_GetWindowSize(m_Window, &dst.w, &dst.h);

        // Use the stream and window sizes to determine the video region
        StreamUtils::scaleSourceToDestinationSurface(&src, &dst);

        // SDL 2.0.18 lets us lock the cursor to a specific region
        SDL_SetWindowMouseRect(m_Window, &dst);
#elif SDL_VERSION_ATLEAST(2, 0, 15)
        // SDL 2.0.15 only lets us lock the cursor to the whole window
        SDL_SetWindowMouseGrab(m_Window, SDL_TRUE);
#else
        SDL_SetWindowGrab(m_Window, SDL_TRUE);
#endif
    }
    else {
        // Allow the cursor to leave the bounds of our video region or window
#if SDL_VERSION_ATLEAST(2, 0, 18)
        SDL_SetWindowMouseRect(m_Window, nullptr);
#elif SDL_VERSION_ATLEAST(2, 0, 15)
        SDL_SetWindowMouseGrab(m_Window, SDL_FALSE);
#else
        SDL_SetWindowGrab(m_Window, SDL_FALSE);
#endif
    }
}
