#include <Limelight.h>
#include <SDL.h>
#include "settings/mappingmanager.h"
#include "streaming/session.h"
#include "path.h"
#include "utils.h"

#include <QtGlobal>
#include <QDir>
#include <QGuiApplication>

SdlInputHandler::SdlInputHandler(StreamingPreferences *prefs, PSTREAM_CONFIGURATION config)
    : m_MultiController(prefs->multiController),
    m_GamepadMouse(prefs->gamepadMouse),
    m_SwapMouseButtons(prefs->swapMouseButtons),
    m_ReverseScrollDirection(prefs->reverseScrollDirection),
    m_SwapFaceButtons(prefs->swapFaceButtons),
    m_MouseWasInVideoRegion(false),
    m_PendingMouseButtonsAllUpOnVideoRegionLeave(false),
    m_PointerRegionLockActive(false),
    m_PointerRegionLockToggledByUser(false),
    m_FakeCaptureActive(false),
    m_CaptureSystemKeysMode(prefs->captureSysKeysMode),
    m_MouseCursorCapturedVisibilityState(SDL_DISABLE),
    m_LongPressTimer(0),
    m_Preferences(prefs),
    m_streamConfig(config),
    m_AbsoluteMouseMode(prefs->absoluteMouseMode),
    m_AbsoluteTouchMode(prefs->absoluteTouchMode),
    m_LeftButtonReleaseTimer(0),
    m_RightButtonReleaseTimer(0),
    m_DragTimer(0),
    m_DragButton(0),
    m_NumFingersDown(0)
{
    // System keys are always captured when running without a DE
    if (!WMUtils::isRunningDesktopEnvironment()) {
        m_CaptureSystemKeysMode = StreamingPreferences::CSK_ALWAYS;
    }

    // Allow gamepad input when the app doesn't have focus if requested
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, prefs->backgroundGamepad ? "1" : "0");

    // If absolute mouse mode is enabled, use relative mode warp (which
    // is via normal motion events that are influenced by mouse acceleration).
    // Otherwise, we'll use raw input capture which is straight from the device
    // without modification by the OS.
    SDL_SetHintWithPriority(SDL_HINT_MOUSE_RELATIVE_MODE_WARP,
                            prefs->absoluteMouseMode ? "1" : "0",
                            SDL_HINT_OVERRIDE);

#if !SDL_VERSION_ATLEAST(2, 0, 15)
    // For older versions of SDL (2.0.14 and earlier), use SDL_HINT_GRAB_KEYBOARD
    SDL_SetHintWithPriority(SDL_HINT_GRAB_KEYBOARD,
                            m_CaptureSystemKeysMode != StreamingPreferences::CSK_OFF ? "1" : "0",
                            SDL_HINT_OVERRIDE);
#endif

    // Opt-out of SDL's built-in Alt+Tab handling while keyboard grab is enabled
    SDL_SetHint("SDL_ALLOW_ALT_TAB_WHILE_GRABBED", "0");

    // Allow clicks to pass through to us when focusing the window. If we're in
    // absolute mouse mode, this will avoid the user having to click twice to
    // trigger a click on the host if the Gleam window is not focused. In
    // relative mode, the click event will trigger the mouse to be recaptured.
    SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1");

    // Enabling extended input reports allows rumble to function on Bluetooth PS4/PS5
    // controllers, but breaks DirectInput applications. We will enable it because
    // it's likely that working rumble is what the user is expecting. If they don't
    // want this behavior, they can override it with the environment variable.
    SDL_SetHint("SDL_JOYSTICK_HIDAPI_PS4_RUMBLE", "1");
    SDL_SetHint("SDL_JOYSTICK_HIDAPI_PS5_RUMBLE", "1");

    // Populate special key combo configuration
    m_SpecialKeyCombos[KeyComboQuit].keyCombo = KeyComboQuit;
    m_SpecialKeyCombos[KeyComboQuit].keyCode = SDLK_q;
    m_SpecialKeyCombos[KeyComboQuit].scanCode = SDL_SCANCODE_Q;
    m_SpecialKeyCombos[KeyComboQuit].enabled = true;

    m_SpecialKeyCombos[KeyComboUngrabInput].keyCombo = KeyComboUngrabInput;
    m_SpecialKeyCombos[KeyComboUngrabInput].keyCode = SDLK_z;
    m_SpecialKeyCombos[KeyComboUngrabInput].scanCode = SDL_SCANCODE_Z;
    m_SpecialKeyCombos[KeyComboUngrabInput].enabled = QGuiApplication::platformName() != "eglfs";

    m_SpecialKeyCombos[KeyComboToggleFullScreen_1].keyCombo = KeyComboToggleFullScreen_1;
    m_SpecialKeyCombos[KeyComboToggleFullScreen_1].keyCode = SDLK_1;
    m_SpecialKeyCombos[KeyComboToggleFullScreen_1].scanCode = SDL_SCANCODE_1;
    m_SpecialKeyCombos[KeyComboToggleFullScreen_1].enabled = QGuiApplication::platformName() != "eglfs";

    m_SpecialKeyCombos[KeyComboToggleFullScreen_2].keyCombo = KeyComboToggleFullScreen_2;
    m_SpecialKeyCombos[KeyComboToggleFullScreen_2].keyCode = SDLK_2;
    m_SpecialKeyCombos[KeyComboToggleFullScreen_2].scanCode = SDL_SCANCODE_2;
    m_SpecialKeyCombos[KeyComboToggleFullScreen_2].enabled = QGuiApplication::platformName() != "eglfs";

    m_SpecialKeyCombos[KeyComboToggleStatsOverlay].keyCombo = KeyComboToggleStatsOverlay;
    m_SpecialKeyCombos[KeyComboToggleStatsOverlay].keyCode = SDLK_s;
    m_SpecialKeyCombos[KeyComboToggleStatsOverlay].scanCode = SDL_SCANCODE_S;
    m_SpecialKeyCombos[KeyComboToggleStatsOverlay].enabled = true;

    m_SpecialKeyCombos[KeyComboToggleMouseMode].keyCombo = KeyComboToggleMouseMode;
    m_SpecialKeyCombos[KeyComboToggleMouseMode].keyCode = SDLK_m;
    m_SpecialKeyCombos[KeyComboToggleMouseMode].scanCode = SDL_SCANCODE_M;
    m_SpecialKeyCombos[KeyComboToggleMouseMode].enabled = true;

    // m_SpecialKeyCombos[KeyComboToggleCursorHide].keyCombo = KeyComboToggleCursorHide;
    // m_SpecialKeyCombos[KeyComboToggleCursorHide].keyCode = SDLK_c;
    // m_SpecialKeyCombos[KeyComboToggleCursorHide].scanCode = SDL_SCANCODE_C;
    // m_SpecialKeyCombos[KeyComboToggleCursorHide].enabled = true;

    // m_SpecialKeyCombos[KeyComboToggleMinimize].keyCombo = KeyComboToggleMinimize;
    // m_SpecialKeyCombos[KeyComboToggleMinimize].keyCode = SDLK_d;
    // m_SpecialKeyCombos[KeyComboToggleMinimize].scanCode = SDL_SCANCODE_D;
    // m_SpecialKeyCombos[KeyComboToggleMinimize].enabled = QGuiApplication::platformName() != "eglfs";

    m_SpecialKeyCombos[KeyComboPasteText].keyCombo = KeyComboPasteText;
    m_SpecialKeyCombos[KeyComboPasteText].keyCode = SDLK_v;
    m_SpecialKeyCombos[KeyComboPasteText].scanCode = SDL_SCANCODE_V;
    m_SpecialKeyCombos[KeyComboPasteText].enabled = true;

    // m_SpecialKeyCombos[KeyComboTogglePointerRegionLock].keyCombo = KeyComboTogglePointerRegionLock;
    // m_SpecialKeyCombos[KeyComboTogglePointerRegionLock].keyCode = SDLK_l;
    // m_SpecialKeyCombos[KeyComboTogglePointerRegionLock].scanCode = SDL_SCANCODE_L;
    // m_SpecialKeyCombos[KeyComboTogglePointerRegionLock].enabled = true;

    m_SpecialKeyCombos[KeyComboStreamConfigWebView].keyCombo = KeyComboStreamConfigWebView;
    m_SpecialKeyCombos[KeyComboStreamConfigWebView].keyCode = SDLK_i;
    m_SpecialKeyCombos[KeyComboStreamConfigWebView].scanCode = SDL_SCANCODE_I;
    m_SpecialKeyCombos[KeyComboStreamConfigWebView].enabled = true;


    m_SpecialKeyCombos[KeyComboSettingWebView].keyCombo = KeyComboSettingWebView;
    m_SpecialKeyCombos[KeyComboSettingWebView].keyCode = SDLK_o;
    m_SpecialKeyCombos[KeyComboSettingWebView].scanCode = SDL_SCANCODE_O;
    m_SpecialKeyCombos[KeyComboSettingWebView].enabled = true;

    m_SpecialKeyCombos[KeyComboHelpWebView].keyCombo = KeyComboHelpWebView;
    m_SpecialKeyCombos[KeyComboHelpWebView].keyCode = SDLK_h;
    m_SpecialKeyCombos[KeyComboHelpWebView].scanCode = SDL_SCANCODE_H;
    m_SpecialKeyCombos[KeyComboHelpWebView].enabled = true;

    m_SpecialKeyCombos[KeyComboHelpWebViewQuestion].keyCombo = KeyComboHelpWebViewQuestion;
    m_SpecialKeyCombos[KeyComboHelpWebViewQuestion].keyCode = SDLK_SLASH;
    m_SpecialKeyCombos[KeyComboHelpWebViewQuestion].scanCode = SDL_SCANCODE_SLASH;
    m_SpecialKeyCombos[KeyComboHelpWebViewQuestion].enabled = true;

    m_OldIgnoreDevices = SDL_GetHint(SDL_HINT_GAMECONTROLLER_IGNORE_DEVICES);
    m_OldIgnoreDevicesExcept = SDL_GetHint(SDL_HINT_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT);

    QString streamIgnoreDevices = qgetenv("STREAM_GAMECONTROLLER_IGNORE_DEVICES");
    QString streamIgnoreDevicesExcept = qgetenv("STREAM_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT");

    if (!streamIgnoreDevices.isEmpty() && !streamIgnoreDevices.endsWith(',')) {
        streamIgnoreDevices += ',';
    }
    streamIgnoreDevices += m_OldIgnoreDevices;

    // For SDL_HINT_GAMECONTROLLER_IGNORE_DEVICES, we use the union of SDL_GAMECONTROLLER_IGNORE_DEVICES
    // and STREAM_GAMECONTROLLER_IGNORE_DEVICES while streaming. STREAM_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT
    // overrides SDL_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT while streaming.
    SDL_SetHint(SDL_HINT_GAMECONTROLLER_IGNORE_DEVICES, streamIgnoreDevices.toUtf8());
    SDL_SetHint(SDL_HINT_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT, streamIgnoreDevicesExcept.toUtf8());

    // We must initialize joystick explicitly before gamecontroller in order
    // to ensure we receive gamecontroller attach events for gamepads where
    // SDL doesn't have a built-in mapping. By starting joystick first, we
    // can allow mapping manager to update the mappings before GC attach
    // events are generated.
    //SDL_assert(!SDL_WasInit(SDL_INIT_JOYSTICK));
    if (SDL_InitSubSystem(SDL_INIT_JOYSTICK) != 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "SDL_InitSubSystem(SDL_INIT_JOYSTICK) failed: %s",
                     SDL_GetError());
    }

    MappingManager mappingManager;
    mappingManager.applyMappings();

    // Flush gamepad arrival and departure events which may be queued before
    // starting the gamecontroller subsystem again. This prevents us from
    // receiving duplicate arrival and departure events for the same gamepad.
    SDL_FlushEvent(SDL_CONTROLLERDEVICEADDED);
    SDL_FlushEvent(SDL_CONTROLLERDEVICEREMOVED);

    // We need to reinit this each time, since you only get
    // an initial set of gamepad arrival events once per init.
    //SDL_assert(!SDL_WasInit(SDL_INIT_GAMECONTROLLER));
    if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) != 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) failed: %s",
                     SDL_GetError());
    }

#if !SDL_VERSION_ATLEAST(2, 0, 9)
    //SDL_assert(!SDL_WasInit(SDL_INIT_HAPTIC));
    if (SDL_InitSubSystem(SDL_INIT_HAPTIC) != 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "SDL_InitSubSystem(SDL_INIT_HAPTIC) failed: %s",
                     SDL_GetError());
    }
#endif

    // Initialize the gamepad mask with currently attached gamepads to avoid
    // causing gamepads to unexpectedly disappear and reappear on the host
    // during stream startup as we detect currently attached gamepads one at a time.
    m_GamepadMask = getAttachedGamepadMask();

    SDL_zero(m_GamepadState);
    SDL_zero(m_LastTouchDownEvent);
    SDL_zero(m_LastTouchUpEvent);
    SDL_zero(m_TouchDownEvent);
}

SdlInputHandler::~SdlInputHandler()
{
    for (int i = 0; i < MAX_GAMEPADS; i++) {
        if (m_GamepadState[i].mouseEmulationTimer != 0) {
            Session::get()->notifyMouseEmulationMode(false);
            SDL_RemoveTimer(m_GamepadState[i].mouseEmulationTimer);
        }
#if !SDL_VERSION_ATLEAST(2, 0, 9)
        if (m_GamepadState[i].haptic != nullptr) {
            SDL_HapticClose(m_GamepadState[i].haptic);
        }
#endif
        if (m_GamepadState[i].controller != nullptr) {
            SDL_GameControllerClose(m_GamepadState[i].controller);
        }
    }

    SDL_RemoveTimer(m_LongPressTimer);
    SDL_RemoveTimer(m_LeftButtonReleaseTimer);
    SDL_RemoveTimer(m_RightButtonReleaseTimer);
    SDL_RemoveTimer(m_DragTimer);

#if !SDL_VERSION_ATLEAST(2, 0, 9)
    SDL_QuitSubSystem(SDL_INIT_HAPTIC);
    SDL_assert(!SDL_WasInit(SDL_INIT_HAPTIC));
#endif

    SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER);
    SDL_assert(!SDL_WasInit(SDL_INIT_GAMECONTROLLER));

    SDL_QuitSubSystem(SDL_INIT_JOYSTICK);
    SDL_assert(!SDL_WasInit(SDL_INIT_JOYSTICK));

    // Return background event handling to off
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "0");

    // Restore the ignored devices
    SDL_SetHint(SDL_HINT_GAMECONTROLLER_IGNORE_DEVICES, m_OldIgnoreDevices.toUtf8());
    SDL_SetHint(SDL_HINT_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT, m_OldIgnoreDevicesExcept.toUtf8());

#ifdef STEAM_LINK
    // Hide SDL's cursor on Steam Link after quitting the stream.
    // FIXME: We should also do this for other situations where SDL
    // and Qt will draw their own mouse cursors like KMSDRM or RPi
    // video backends.
    SDL_ShowCursor(SDL_DISABLE);
#endif
}

void SdlInputHandler::setWindow(SDL_Window *window, SDL_Window* window2)
{
    m_Window = window;
    m_Window2 = window2;
}

void SdlInputHandler::setDispConfigList(QList<GleamCore::DisplayInfo> displyConfig, std::pair<int,int> minXY) {
    for (auto &info : displyConfig) {
        if (info.x != 0 || info.y != 0) {
            m_window2DispInfo = info;
            m_window2DispInfo.x -= minXY.first;
            m_window2DispInfo.y -= minXY.second;
        } else {
            m_windowDispInfo = info;
            m_windowDispInfo.x -= minXY.first;
            m_windowDispInfo.y -= minXY.second;
        }
    }
    m_minXY = minXY;
}

void SdlInputHandler::raiseAllKeys()
{
    if (m_KeysDown.isEmpty()) {
        return;
    }

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "Raising %d keys",
                (int)m_KeysDown.count());

    for (auto keyDown : m_KeysDown) {
        LiSendKeyboardEvent(keyDown, KEY_ACTION_UP, 0);
    }

    m_KeysDown.clear();
}

void SdlInputHandler::notifyMouseLeave()
{
    // SDL on Windows doesn't send the mouse button up until the mouse re-enters the window
    // after leaving it. This breaks some of the Aero snap gestures, so we'll capture it to
    // allow us to receive the mouse button up events later.
    //
    // On macOS and X11, capturing the mouse allows us to receive mouse motion outside the
    // window (button up already worked without capture).
    if (m_AbsoluteMouseMode && isCaptureActive()) {
        // NB: Not using SDL_GetGlobalMouseState() because we want our state not the system's
        Uint32 mouseState = SDL_GetMouseState(nullptr, nullptr);
        for (Uint32 button = SDL_BUTTON_LEFT; button <= SDL_BUTTON_X2; button++) {
            if (mouseState & SDL_BUTTON(button)) {
                SDL_CaptureMouse(SDL_TRUE);
                break;
            }
        }
    }
}

void SdlInputHandler::notifyFocusLost()
{
    // Release mouse cursor when another window is activated (e.g. by using ALT+TAB).
    // This lets user to interact with our window's title bar and with the buttons in it.
    // Doing this while the window is full-screen breaks the transition out of FS
    // (desktop and exclusive), so we must check for that before releasing mouse capture.
    if (!(SDL_GetWindowFlags(m_Window) & SDL_WINDOW_FULLSCREEN) && !m_AbsoluteMouseMode) {
        setCaptureActive(false);
    }
    //if (!(SDL_GetWindowFlags(m_Window2) & SDL_WINDOW_FULLSCREEN) && !m_AbsoluteMouseMode) {
    //   setCaptureActive(false);}

    // Raise all keys that are currently pressed. If we don't do this, certain keys
    // used in shortcuts that cause focus loss (such as Alt+Tab) may get stuck down.
    raiseAllKeys();
}

bool SdlInputHandler::isCaptureActive()
{
    if (SDL_GetRelativeMouseMode()) {
        return true;
    }

    // Some platforms don't support SDL_SetRelativeMouseMode
    return m_FakeCaptureActive;
}

void SdlInputHandler::updateKeyboardGrabStateByWindow(SDL_Window* win) {
    if (win == nullptr) {
        return;
    }
    bool shouldGrab = isCaptureActive();
    Uint32 windowFlags = SDL_GetWindowFlags(win);
    if (m_CaptureSystemKeysMode == StreamingPreferences::CSK_FULLSCREEN &&
        !(windowFlags & SDL_WINDOW_FULLSCREEN)) {
        // Ungrab if it's fullscreen only and we left fullscreen
        shouldGrab = false;
    }

    // Don't close the window on Alt+F4 when keyboard grab is enabled
    SDL_SetHint(SDL_HINT_WINDOWS_NO_CLOSE_ON_ALT_F4, shouldGrab ? "1" : "0");

#if SDL_VERSION_ATLEAST(2, 0, 15)
    // On SDL 2.0.15+, we can get keyboard-only grab on Win32, X11, and Wayland.
    // SDL 2.0.18 adds keyboard grab on macOS (if built with non-AppStore APIs).
    SDL_SetWindowKeyboardGrab(win, shouldGrab ? SDL_TRUE : SDL_FALSE);
#endif
}

void SdlInputHandler::updateKeyboardGrabState()
{
    if (m_CaptureSystemKeysMode == StreamingPreferences::CSK_OFF) {
        return;
    }

    updateKeyboardGrabStateByWindow(m_Window);
    updateKeyboardGrabStateByWindow(m_Window2);
}

bool SdlInputHandler::isSystemKeyCaptureActive()
{
    if (m_CaptureSystemKeysMode == StreamingPreferences::CSK_OFF) {
        return false;
    }

    if (m_Window == nullptr) {
        return false;
    }

    Uint32 windowFlags = SDL_GetWindowFlags(m_Window);
    if (!(windowFlags & SDL_WINDOW_INPUT_FOCUS)
#if SDL_VERSION_ATLEAST(2, 0, 15)
        || !(windowFlags & SDL_WINDOW_KEYBOARD_GRABBED)
#else
        || !(windowFlags & SDL_WINDOW_INPUT_GRABBED)
#endif
        ) {
        if (m_Window2 == nullptr) {
            qDebug()<<"SdlInputHandler::isSystemKeyCaptureActive() m_Window2 is nullptr";
            return false;
        }

        Uint32 windowFlags2 = SDL_GetWindowFlags(m_Window2);
        if (!(windowFlags2 & SDL_WINDOW_INPUT_FOCUS)
#if SDL_VERSION_ATLEAST(2, 0, 15)
            || !(windowFlags2 & SDL_WINDOW_KEYBOARD_GRABBED)
#else
            || !(windowFlags2 & SDL_WINDOW_INPUT_GRABBED)
#endif
            ) {
            return false;
        }

        if (m_CaptureSystemKeysMode == StreamingPreferences::CSK_FULLSCREEN &&
            !(windowFlags2 & SDL_WINDOW_FULLSCREEN)) {
            return false;
        }
        return true;
    }

    if (m_CaptureSystemKeysMode == StreamingPreferences::CSK_FULLSCREEN &&
        !(windowFlags & SDL_WINDOW_FULLSCREEN)) {
        return false;
    }

    return true;
}

void SdlInputHandler::setCaptureActive(bool active)
{
    qDebug() << "setCaptureActive = " << active;
    if (active) {
        // If we're in relative mode, try to activate SDL's relative mouse mode
        if (m_AbsoluteMouseMode || SDL_SetRelativeMouseMode(SDL_TRUE) < 0) {
            // Relative mouse mode didn't work or was disabled, so we'll just hide the cursor
            SDL_ShowCursor(m_MouseCursorCapturedVisibilityState);
            m_FakeCaptureActive = true;
        }

        // // Synchronize the client and host cursor when activating absolute capture
        // if (m_AbsoluteMouseMode) {
        //     int mouseX, mouseY;
        //     int windowX, windowY;

        //     // We have to use SDL_GetGlobalMouseState() because macOS may not reflect
        //     // the new position of the mouse when outside the window.
        //     SDL_GetGlobalMouseState(&mouseX, &mouseY);

        //     // Convert global mouse state to window-relative
        //     SDL_GetWindowPosition(m_Window, &windowX, &windowY);
        //     mouseX -= windowX;
        //     mouseY -= windowY;

        //     if (isMouseInVideoRegion(mouseX, mouseY)) {
        //         // Synthesize a mouse event to synchronize the cursor
        //         SDL_MouseMotionEvent motionEvent = {};
        //         motionEvent.type = SDL_MOUSEMOTION;
        //         motionEvent.timestamp = SDL_GetTicks();
        //         motionEvent.windowID = SDL_GetWindowID(m_Window);
        //         motionEvent.x = mouseX;
        //         motionEvent.y = mouseY;
        //         handleMouseMotionEvent(&motionEvent);
        //     }
        // }
    }
    else {
        if (m_FakeCaptureActive) {
            // Display the cursor again
            SDL_ShowCursor(SDL_ENABLE);
            m_FakeCaptureActive = false;
        }
        else {
            SDL_SetRelativeMouseMode(SDL_FALSE);
        }
    }

    // Update mouse pointer region constraints
    updatePointerRegionLock();

    // Now update the keyboard grab
    updateKeyboardGrabState();
}

void SdlInputHandler::handleTouchFingerEvent(SDL_TouchFingerEvent* event)
{
#if SDL_VERSION_ATLEAST(2, 0, 10)
    if (SDL_GetTouchDeviceType(event->touchId) != SDL_TOUCH_DEVICE_DIRECT) {
        // Ignore anything that isn't a touchscreen. We may get callbacks
        // for trackpads, but we want to handle those in the mouse path.
        return;
    }
#elif defined(Q_OS_DARWIN)
    // SDL2 sends touch events from trackpads by default on
    // macOS. This totally screws our actual mouse handling,
    // so we must explicitly ignore touch events on macOS
    // until SDL 2.0.10 where we have SDL_GetTouchDeviceType()
    // to tell them apart.
    return;
#endif

    if (m_AbsoluteTouchMode) {
        handleAbsoluteFingerEvent(event);
    }
    else {
        handleRelativeFingerEvent(event);
    }
}
