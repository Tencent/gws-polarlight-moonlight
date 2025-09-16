#pragma once

#include "settings/streamingpreferences.h"
#include "backend/computermanager.h"
#include "Core/displayInfo.h"
#include <SDL.h>

struct GamepadState {
    SDL_GameController* controller;
    SDL_JoystickID jsId;
    short index;

#if !SDL_VERSION_ATLEAST(2, 0, 9)
    SDL_Haptic* haptic;
    int hapticMethod;
    int hapticEffectId;
#endif

    SDL_TimerID mouseEmulationTimer;
    uint32_t lastStartDownTime;

    short buttons;
    short lsX, lsY;
    short rsX, rsY;
    unsigned char lt, rt;
};

void ShowSetting();

#define MAX_GAMEPADS 4
#define MAX_FINGERS 2

#define GAMEPAD_HAPTIC_METHOD_NONE 0
#define GAMEPAD_HAPTIC_METHOD_LEFTRIGHT 1
#define GAMEPAD_HAPTIC_METHOD_SIMPLERUMBLE 2

#define GAMEPAD_HAPTIC_SIMPLE_HIFREQ_MOTOR_WEIGHT 0.33
#define GAMEPAD_HAPTIC_SIMPLE_LOWFREQ_MOTOR_WEIGHT 0.8

extern SDL_Rect mock_SDL_Rect;

class SdlInputHandler
{
public:
    explicit SdlInputHandler(StreamingPreferences *prefs, PSTREAM_CONFIGURATION s);

    ~SdlInputHandler();
    void setDispConfigList(QList<GleamCore::DisplayInfo> displyConfig, std::pair<int,int> minXY);
    void setWindow(SDL_Window* window, SDL_Window* window2 = nullptr);

    void handleKeyEvent(SDL_KeyboardEvent* event);

    void handleMouseButtonEvent(SDL_MouseButtonEvent* event);

    void handleMouseMotionEvent(SDL_MouseMotionEvent* event);

    void handleAbsoluteMouseMotion(SDL_MouseMotionEvent* event, GleamCore::DisplayInfo& info);

    void getAnotherWindowRectAndMousePoint(SDL_Window* anotherWin, SDL_Rect& rect);

    void handleMouseWheelEvent(SDL_MouseWheelEvent* event);

    void handleControllerAxisEvent(SDL_ControllerAxisEvent* event);

    void handleControllerButtonEvent(SDL_ControllerButtonEvent* event);

    void handleControllerDeviceEvent(SDL_ControllerDeviceEvent* event);

    void handleJoystickArrivalEvent(SDL_JoyDeviceEvent* event);

    void sendText(QString& string);

    void rumble(unsigned short controllerNumber, unsigned short lowFreqMotor, unsigned short highFreqMotor);

    void handleTouchFingerEvent(SDL_TouchFingerEvent* event);

    int getAttachedGamepadMask();

    void raiseAllKeys();

    void notifyMouseLeave();

    void notifyFocusLost();

    bool isCaptureActive();

    bool isSystemKeyCaptureActive();

    void setCaptureActive(bool active);

    bool isMouseInVideoRegion(int mouseX, int mouseY, int windowWidth, int windowHeight,
        GleamCore::DisplayInfo &dispInfo, SDL_Rect &dst = mock_SDL_Rect);

    void updateKeyboardGrabState();
    void updateKeyboardGrabStateByWindow(SDL_Window* win);

    void updatePointerRegionLock();

    void raiseLastWindow();
    SDL_Window* getActiveSDLWindow() {
        bool active_1 = false, active_2 = false;
        if (m_Window != nullptr) {
            Uint32 flags = SDL_GetWindowFlags(m_Window);
            active_1 = (flags & SDL_WINDOW_INPUT_FOCUS);//|| (flags & SDL_WINDOW_MOUSE_FOCUS);

        }

        if (m_Window2 != nullptr) {
            Uint32 flags = SDL_GetWindowFlags(m_Window2);
            active_2 = (flags & SDL_WINDOW_INPUT_FOCUS);// || (flags & SDL_WINDOW_MOUSE_FOCUS);
        }
        SDL_Window *activeWin = nullptr;
        if (active_1) {
            activeWin = m_Window;
        }
        else if (active_2) {
            activeWin = m_Window2;
        }
        return activeWin;
    }
    static
    QString getUnmappedGamepads();


    SDL_Rect transVideoRegion(SDL_MouseMotionEvent* event, bool &mouseInVideoRegion);
private:
    enum KeyCombo {
        KeyComboQuit,
        KeyComboUngrabInput,
        KeyComboToggleFullScreen_1,
        KeyComboToggleFullScreen_2,
        KeyComboToggleStatsOverlay,
        KeyComboToggleMouseMode,
        // KeyComboToggleCursorHide,
        // KeyComboToggleMinimize,
        KeyComboPasteText,
        KeyComboTogglePointerRegionLock,
        KeyComboStreamConfigWebView,
        KeyComboSettingWebView,
        KeyComboHelpWebView,
        KeyComboHelpWebViewQuestion,
        KeyComboMax
    };

    GamepadState*
    findStateForGamepad(SDL_JoystickID id);

    void sendGamepadState(GamepadState* state);

    void handleAbsoluteFingerEvent(SDL_TouchFingerEvent* event);

    void handleRelativeFingerEvent(SDL_TouchFingerEvent* event);

    void performSpecialKeyCombo(KeyCombo combo);

    static
    Uint32 longPressTimerCallback(Uint32 interval, void* param);

    static
    Uint32 mouseEmulationTimerCallback(Uint32 interval, void* param);

    static
    Uint32 releaseLeftButtonTimerCallback(Uint32 interval, void* param);

    static
    Uint32 releaseRightButtonTimerCallback(Uint32 interval, void* param);

    static
    Uint32 dragTimerCallback(Uint32 interval, void* param);
    void setLastActiveWindowIndex(SDL_KeyboardEvent* event);

    SDL_Window* m_Window = nullptr;
    SDL_Window* m_Window2 = nullptr;
    int lastActiveWindowIndex = -1;
    bool m_MultiController;
    bool m_GamepadMouse;
    bool m_SwapMouseButtons;
    bool m_ReverseScrollDirection;
    bool m_SwapFaceButtons;

    bool m_MouseWasInDragBorder = false;
    bool m_MouseWasInVideoRegion;
    bool m_PendingMouseButtonsAllUpOnVideoRegionLeave;
    bool m_PointerRegionLockActive;
    bool m_PointerRegionLockToggledByUser;

    int m_GamepadMask;
    GamepadState m_GamepadState[MAX_GAMEPADS];
    QSet<short> m_KeysDown;
    bool m_FakeCaptureActive;
    QString m_OldIgnoreDevices;
    QString m_OldIgnoreDevicesExcept;
    StreamingPreferences::CaptureSysKeysMode m_CaptureSystemKeysMode;
    int m_MouseCursorCapturedVisibilityState;

    struct {
        KeyCombo keyCombo;
        SDL_Keycode keyCode;
        SDL_Scancode scanCode;
        bool enabled;
    } m_SpecialKeyCombos[KeyComboMax];

    SDL_TouchFingerEvent m_LastTouchDownEvent;
    SDL_TouchFingerEvent m_LastTouchUpEvent;
    SDL_TimerID m_LongPressTimer;
    StreamingPreferences *m_Preferences;
    PSTREAM_CONFIGURATION m_streamConfig;

    bool m_AbsoluteMouseMode;
    bool m_AbsoluteTouchMode;

    SDL_TouchFingerEvent m_TouchDownEvent[MAX_FINGERS];
    SDL_TimerID m_LeftButtonReleaseTimer;
    SDL_TimerID m_RightButtonReleaseTimer;
    SDL_TimerID m_DragTimer;
    char m_DragButton;
    int m_NumFingersDown;

    GleamCore::DisplayInfo m_windowDispInfo;
    GleamCore::DisplayInfo m_window2DispInfo;
    std::pair<int,int> m_minXY;

    static const int k_ButtonMap[];
};
