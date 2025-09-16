#include "streamingpreferences.h"
#include "utils.h"
#include "backend/systemproperties.h"
#include <QSettings>
#include <QTranslator>
#include <QCoreApplication>
#include <QLocale>
#include <QJsonObject>
#include <QJsonDocument>
#include <QtDebug>

#define SER_STREAMSETTINGS "streamsettings"
#define SER_WIDTH "width"
#define SER_HEIGHT "height"
#define SER_FPS "fps"
#define SER_WIDTH2 "width2"
#define SER_HEIGHT2 "height2"
#define SER_FPS2 "fps2"
#define SER_WHLINK "whlink"
#define SER_BITRATE "bitrate"
#define SER_BITRATELIMIT "bitratelimit"
#define SER_FULLSCREEN "fullscreen"
#define SER_VSYNC "vsync"
#define SER_GAMEOPTS "gameopts"
#define SER_HOSTAUDIO "hostaudio"
#define SER_MULTICONT "multicontroller"
#define SER_AUDIOCFG "audiocfg"
#define SER_VIDEOCFG "videocfg"
#define SER_VIDEODEC "videodec"
#define SER_WINDOWMODE "windowmode"
#define SER_UNSUPPORTEDFPS "unsupportedfps"
#define SER_MDNS "mdns"
#define SER_QUITAPPAFTER "quitAppAfter"
#define SER_ABSMOUSEMODE "mouseacceleration"
#define SER_ABSTOUCHMODE "abstouchmode"
#define SER_STARTWINDOWED "startwindowed"
#define SER_FRAMEPACING "framepacing"
#define SER_CONNWARNINGS "connwarnings"
#define SER_UIDISPLAYMODE "uidisplaymode"
#define SER_RICHPRESENCE "richpresence"
#define SER_GAMEPADMOUSE "gamepadmouse"
#define SER_DEFAULTVER "defaultver"
#define SER_PACKETSIZE "packetsize"
#define SER_DETECTNETBLOCKING "detectnetblocking"
#define SER_SWAPMOUSEBUTTONS "swapmousebuttons"
#define SER_MUTEONFOCUSLOSS "muteonfocusloss"
#define SER_BACKGROUNDGAMEPAD "backgroundgamepad"
#define SER_REVERSESCROLL "reversescroll"
#define SER_SWAPFACEBUTTONS "swapfacebuttons"
#define SER_CAPTURESYSKEYS "capturesyskeys"
#define SER_KEEPAWAKE "keepawake"
#define SER_LANGUAGE "language"
#define SER_DEBUGLOG "debuglog"
#define SER_OMITALT "omitalt"
#define CURRENT_DEFAULT_VER 2

StreamingPreferences::StreamingPreferences(QObject *parent)
    : QObject(parent),
      m_QmlEngine(nullptr)
{
    reload();
}

StreamingPreferences::StreamingPreferences(QQmlEngine *qmlEngine, QObject *parent)
    : QObject(parent),
      m_QmlEngine(qmlEngine)
{
    reload();
}

void StreamingPreferences::reload()
{
    QSettings settings;
    bool needSave = false;

    width = settings.value(SER_WIDTH, 1920).toInt();
    height = settings.value(SER_HEIGHT, 1080).toInt();
    fps = settings.value(SER_FPS, 60).toInt();
    if ((width != 0 || height != 0) && (width < 1920 || height < 1080)) {
        width = 1920;
        height = 1080;
        needSave = true;
    }

    width2 = settings.value(SER_WIDTH2, 1920).toInt();
    height2 = settings.value(SER_HEIGHT2, 1080).toInt();
    fps2 = settings.value(SER_FPS2, 60).toInt();
    if ((width2 != 0 || height2 != 0) &&(width2 < 1920 || width2 < 1080)) {
        width2 = 1920;
        width2 = 1080;
        needSave = true;
    }

    whlink = settings.value(SER_WHLINK, true).toBool();
    if (whlink && (width2 != width || height2 != height || fps2 != fps)) {
        width2 = width;
        height2 = height;
        fps2 = fps;
        needSave = true;
    }

    bitrateKbps = settings.value(SER_BITRATE, getDefaultBitrate(width, height, fps)).toInt();
    if (bitrateKbps > gBitrateKbpsLimit * 1000) {
        bitrateKbps = gBitrateKbpsLimit * 1000;
    }

    enableVsync = settings.value(SER_VSYNC, true).toBool();
    gameOptimizations = settings.value(SER_GAMEOPTS, true).toBool();
    playAudioOnHost = settings.value(SER_HOSTAUDIO, false).toBool();
    multiController = settings.value(SER_MULTICONT, true).toBool();
    unsupportedFps = settings.value(SER_UNSUPPORTEDFPS, false).toBool();
    enableMdns = settings.value(SER_MDNS, true).toBool();
    quitAppAfter = settings.value(SER_QUITAPPAFTER, false).toBool();
    absoluteMouseMode = settings.value(SER_ABSMOUSEMODE, true).toBool();
    absoluteTouchMode = settings.value(SER_ABSTOUCHMODE, true).toBool();
    framePacing = settings.value(SER_FRAMEPACING, false).toBool();
    connectionWarnings = settings.value(SER_CONNWARNINGS, true).toBool();
    richPresence = settings.value(SER_RICHPRESENCE, true).toBool();
    gamepadMouse = settings.value(SER_GAMEPADMOUSE, true).toBool();
    detectNetworkBlocking = settings.value(SER_DETECTNETBLOCKING, true).toBool();
    packetSize = settings.value(SER_PACKETSIZE, 0).toInt();
    swapMouseButtons = settings.value(SER_SWAPMOUSEBUTTONS, false).toBool();
    muteOnFocusLoss = settings.value(SER_MUTEONFOCUSLOSS, false).toBool();
    backgroundGamepad = settings.value(SER_BACKGROUNDGAMEPAD, false).toBool();
    reverseScrollDirection = settings.value(SER_REVERSESCROLL, false).toBool();
    swapFaceButtons = settings.value(SER_SWAPFACEBUTTONS, false).toBool();
    keepAwake = settings.value(SER_KEEPAWAKE, true).toBool();
    captureSysKeysMode = static_cast<CaptureSysKeysMode>(settings.value(SER_CAPTURESYSKEYS,
                                                         static_cast<int>(CaptureSysKeysMode::CSK_ALWAYS)).toInt());
    if (captureSysKeysMode == CaptureSysKeysMode::CSK_FULLSCREEN) {
        captureSysKeysMode = CaptureSysKeysMode::CSK_ALWAYS;
        needSave = true;
    }
    audioConfig = static_cast<AudioConfig>(settings.value(SER_AUDIOCFG,
                                                  static_cast<int>(AudioConfig::AC_STEREO)).toInt());
    videoCodecConfig = static_cast<VideoCodecConfig>(settings.value(SER_VIDEOCFG,
                                                  static_cast<int>(VideoCodecConfig::VCC_AUTO)).toInt());

    videoDecoderSelection = static_cast<VideoDecoderSelection>(settings.value(SER_VIDEODEC,
                                                  static_cast<int>(VideoDecoderSelection::VDS_FORCE_HARDWARE)).toInt());
    if (videoDecoderSelection != VideoDecoderSelection::VDS_FORCE_HARDWARE) {
        videoDecoderSelection = VideoDecoderSelection::VDS_FORCE_HARDWARE;
        needSave = true;
    }
    windowMode = WindowMode::WM_WINDOWED;
    uiDisplayMode = static_cast<UIDisplayMode>(settings.value(SER_UIDISPLAYMODE,
                                               static_cast<int>(settings.value(SER_STARTWINDOWED, true).toBool() ? UIDisplayMode::UI_WINDOWED
                                                                                                                 : UIDisplayMode::UI_MAXIMIZED)).toInt());
    language = static_cast<Language>(settings.value(SER_LANGUAGE,
                                                    static_cast<int>(Language::LANG_AUTO)).toInt());
    bDebugLog = settings.value(SER_DEBUGLOG, false).toBool();
    bOmitAlt = settings.value(SER_OMITALT, true).toBool();

    if (needSave) {
        save();
    }
}

bool StreamingPreferences::retranslate()
{
    static QTranslator* translator = nullptr;

#if QT_VERSION < QT_VERSION_CHECK(5, 10, 0)
    if (m_QmlEngine != nullptr) {
        // Dynamic retranslation is not supported until Qt 5.10
        return false;
    }
#endif

    QTranslator* newTranslator = new QTranslator();
    QString languageSuffix = getSuffixFromLanguage(language);

    // Remove the old translator, even if we can't load a new one.
    // Otherwise we'll be stuck with the old translated values instead
    // of defaulting to English.
    if (translator != nullptr) {
        QCoreApplication::removeTranslator(translator);
        delete translator;
        translator = nullptr;
    }

    if (newTranslator->load(QString(":/languages/qml_") + languageSuffix)) {
        qInfo() << "Successfully loaded translation for" << languageSuffix;

        translator = newTranslator;
        QCoreApplication::installTranslator(translator);
    }
    else {
        qInfo() << "No translation available for" << languageSuffix;
        delete newTranslator;
    }

    if (m_QmlEngine != nullptr) {
#if QT_VERSION >= QT_VERSION_CHECK(5, 10, 0)
        // This is a dynamic retranslation from the settings page.
        // We have to kick the QML engine into reloading our text.
        m_QmlEngine->retranslate();
#else
        // Unreachable below Qt 5.10 due to the check above
        Q_ASSERT(false);
#endif
    }
    else {
        // This is a translation from a non-QML context, which means
        // it is probably app startup. There's nothing to refresh.
    }

    return true;
}

QString StreamingPreferences::getSuffixFromLanguage(StreamingPreferences::Language lang)
{
    switch (lang)
    {
    case LANG_DE:
        return "de";
    case LANG_EN:
        return "en";
    case LANG_FR:
        return "fr";
    case LANG_ZH_CN:
        return "zh_CN";
    case LANG_NB_NO:
        return "nb_NO";
    case LANG_RU:
        return "ru";
    case LANG_ES:
        return "es";
    case LANG_JA:
        return "ja";
    case LANG_VI:
        return "vi";
    case LANG_TH:
        return "th";
    case LANG_KO:
        return "ko";
    case LANG_HU:
        return "hu";
    case LANG_NL:
        return "nl";
    case LANG_SV:
        return "sv";
    case LANG_TR:
        return "tr";
    case LANG_UK:
        return "uk";
    case LANG_ZH_TW:
        return "zh_TW";
    case LANG_PT:
        return "pt";
    case LANG_PT_BR:
        return "pt_BR";
    case LANG_EL:
        return "el";
    case LANG_IT:
        return "it";
    case LANG_HI:
        return "hi";
    case LANG_PL:
        return "pl";
    case LANG_CS:
        return "cs";
    case LANG_AUTO:
    default:
        return QLocale::system().name();
    }
}

void StreamingPreferences::save()
{
    QSettings settings;

    settings.setValue(SER_WIDTH, width);
    settings.setValue(SER_HEIGHT, height);
    settings.setValue(SER_FPS, fps);
    settings.setValue(SER_WIDTH2, width2);
    settings.setValue(SER_HEIGHT2, height2);
    settings.setValue(SER_FPS2, fps2);
    settings.setValue(SER_WHLINK, whlink);
    settings.setValue(SER_BITRATE, bitrateKbps);
    settings.setValue(SER_VSYNC, enableVsync);
    settings.setValue(SER_GAMEOPTS, gameOptimizations);
    settings.setValue(SER_HOSTAUDIO, playAudioOnHost);
    settings.setValue(SER_MULTICONT, multiController);
    settings.setValue(SER_UNSUPPORTEDFPS, unsupportedFps);
    settings.setValue(SER_MDNS, enableMdns);
    settings.setValue(SER_QUITAPPAFTER, quitAppAfter);
    settings.setValue(SER_ABSMOUSEMODE, absoluteMouseMode);
    settings.setValue(SER_ABSTOUCHMODE, absoluteTouchMode);
    settings.setValue(SER_FRAMEPACING, framePacing);
    settings.setValue(SER_CONNWARNINGS, connectionWarnings);
    settings.setValue(SER_RICHPRESENCE, richPresence);
    settings.setValue(SER_GAMEPADMOUSE, gamepadMouse);
    settings.setValue(SER_PACKETSIZE, packetSize);
    settings.setValue(SER_DETECTNETBLOCKING, detectNetworkBlocking);
    settings.setValue(SER_AUDIOCFG, static_cast<int>(audioConfig));
    settings.setValue(SER_VIDEOCFG, static_cast<int>(videoCodecConfig));
    settings.setValue(SER_VIDEODEC, static_cast<int>(videoDecoderSelection));
    //settings.setValue(SER_WINDOWMODE, static_cast<int>(windowMode));
    settings.setValue(SER_UIDISPLAYMODE, static_cast<int>(uiDisplayMode));
    settings.setValue(SER_LANGUAGE, static_cast<int>(language));
    settings.setValue(SER_DEFAULTVER, CURRENT_DEFAULT_VER);
    settings.setValue(SER_SWAPMOUSEBUTTONS, swapMouseButtons);
    settings.setValue(SER_MUTEONFOCUSLOSS, muteOnFocusLoss);
    settings.setValue(SER_BACKGROUNDGAMEPAD, backgroundGamepad);
    settings.setValue(SER_REVERSESCROLL, reverseScrollDirection);
    settings.setValue(SER_SWAPFACEBUTTONS, swapFaceButtons);
    settings.setValue(SER_CAPTURESYSKEYS, captureSysKeysMode);
    settings.setValue(SER_KEEPAWAKE, keepAwake);
    settings.setValue(SER_DEBUGLOG, bDebugLog);
    settings.setValue(SER_OMITALT, bOmitAlt);
}

int StreamingPreferences::getDefaultBitrate(int width, int height, int fps)
{
    // This table prefers 16:10 resolutions because they are
    // only slightly more pixels than the 16:9 equivalents, so
    // we don't want to bump those 16:10 resolutions up to the
    // next 16:9 slot.

    if (width * height <= 640 * 360) {
        return static_cast<int>(1000 * (fps / 30.0));
    }
    else if (width * height <= 854 * 480) {
        return static_cast<int>(1500 * (fps / 30.0));
    }
    // This covers 1280x720 and 1280x800 too
    else if (width * height <= 1366 * 768) {
        return static_cast<int>(5000 * (fps / 30.0));
    }
    else if (width * height <= 1920 * 1200) {
        return static_cast<int>(10000 * (fps / 30.0));
    }
    else if (width * height <= 2560 * 1600) {
        return static_cast<int>(20000 * (fps / 30.0));
    }
    else /* if (width * height <= 3840 * 2160) */ {
        return static_cast<int>(40000 * (fps / 30.0));
    }
}

QString StreamingPreferences::WebViewSettingToJson()
{
    QJsonObject jsonObj;
    // ENCODING
    // Video codec StreamingPreferences::VideoCodecConfig {VCC_AUTO, VCC_FORCE_H264,  VCC_FORCE_HEVC, VCC_FORCE_HEVC_HDR}
    jsonObj[SER_VIDEOCFG] = videoCodecConfig;
    // Audio configuration StreamingPreferences::AudioConfig{AC_STEREO, AC_51_SURROUND, AC_71_SURROUND};
    jsonObj[SER_AUDIOCFG] = audioConfig;
    // Video decoder StreamingPreferences::VideoDecoderSelection { VDS_AUTO, VDS_FORCE_HARDWARE,VDS_FORCE_SOFTWARE};              
    jsonObj[SER_VIDEODEC] = videoDecoderSelection;
    // V-Sync
    jsonObj[SER_VSYNC] = enableVsync;
    // Video Bitrate 70500  preferences->bitrateKbps = 
    //      preferences->getDefaultBitrate(preferences->width, preferences->height, preferences->fps);
    jsonObj[SER_BITRATE] = bitrateKbps;

    if (gBitrateKbpsLimit != 0) {
        jsonObj[SER_BITRATELIMIT] = gBitrateKbpsLimit;
    }

    jsonObj[SER_WIDTH] = width;                               //2560
    jsonObj[SER_HEIGHT] = height;                             //1440
    jsonObj[SER_FPS] = fps;
    jsonObj[SER_WIDTH2] = width2;                               //2560
    jsonObj[SER_HEIGHT2] = height2;                             //1440
    jsonObj[SER_FPS2] = fps2;

    jsonObj[SER_WHLINK] = whlink;

    //jsonObj[SER_WINDOWMODE] = windowMode;                     //Display mode,StreamingPreferences::WM_WINDOWED

    //Audio
    // playAudioOnHost = !playAudioOnHost;
    jsonObj[SER_HOSTAUDIO] = !playAudioOnHost;                 // Mute host PC speakers while streaming
    jsonObj[SER_MUTEONFOCUSLOSS] = muteOnFocusLoss;           //Mute audio stream when Gleam is not the active window
    //Input
    jsonObj[SER_ABSMOUSEMODE] = absoluteMouseMode;            //Optimize mouse for remote desktop instead  of games
    jsonObj[SER_CAPTURESYSKEYS] = captureSysKeysMode;         //    Capture system keyboard shortcuts    
                                                              //    enum CaptureSysKeysMode 
                                                              //    { enum CaptureSysKeysMode { CSK_OFF, CSK_FULLSCREEN, CSK_ALWAYS,};

    jsonObj[SER_ABSTOUCHMODE] = absoluteTouchMode;            //Use touchscreen as a virtual trackpad
    jsonObj[SER_SWAPMOUSEBUTTONS] = swapMouseButtons;         //Swap left and right mouse buttons
    jsonObj[SER_REVERSESCROLL] = reverseScrollDirection;      //Reverse mouse scrolling direction
    //Camepad
    jsonObj[SER_SWAPFACEBUTTONS] = swapFaceButtons;           //Swap A/B and X/gamepad buttons
    jsonObj[SER_MULTICONT] = multiController;                 //Force gamepad #1 always connected
    jsonObj[SER_GAMEPADMOUSE] = gamepadMouse;                 //Enable mouse control with gamepads 
                                                              //    by holding the 'Start' button
    jsonObj[SER_BACKGROUNDGAMEPAD] = backgroundGamepad;       //Process gamepad input when Gleam is in the background

    jsonObj[SER_GAMEOPTS] = gameOptimizations;                  //Optimize game settings for streaming (launch send server)
    jsonObj[SER_UNSUPPORTEDFPS] = unsupportedFps;               //Unlock unsupported FPS options
    jsonObj[SER_QUITAPPAFTER] = quitAppAfter;                   //Quit app on host PC after ending stream
    jsonObj[SER_FRAMEPACING] = framePacing;                     //Frame pacing
    jsonObj[SER_CONNWARNINGS] = connectionWarnings;            //Show connection quality warnings
    jsonObj[SER_KEEPAWAKE] = keepAwake;                         //Keep the display awake while streaming
    jsonObj[SER_DEBUGLOG] = bDebugLog;
    jsonObj[SER_OMITALT] = bOmitAlt;

    std::set<int> fpsItem;
    SystemProperties::getMaxFps(fpsItem);
    QString szStr = "30,60";
    for(std::set<int>::iterator iter = fpsItem.begin() ; iter != fpsItem.end() ; ++iter)
    {
        szStr += ",";
        szStr += QString::number(*iter);
    }
    jsonObj["fpsitem"] = szStr;
    QJsonDocument jsonDoc(jsonObj);
    QString jsonString = jsonDoc.toJson(QJsonDocument::Compact);


    return jsonString;
}

void StreamingPreferences::SaveWebViewSetting(QString &str)
{
    qDebug()<<"SaveWebViewSetting"<<str;
    QJsonDocument jsonDoc = QJsonDocument::fromJson(str.toUtf8());
    if (jsonDoc.isObject()) {
        QJsonObject jsonObj = jsonDoc.object();
        videoCodecConfig = (VideoCodecConfig)jsonObj.value(SER_VIDEOCFG).toInt();
        audioConfig = (AudioConfig)jsonObj.value(SER_AUDIOCFG).toInt();
        // videoDecoderSelection = (VideoDecoderSelection)jsonObj.value(SER_VIDEODEC).toInt();
        width = jsonObj.value(SER_WIDTH).toInt();
        height = jsonObj.value(SER_HEIGHT).toInt();
        fps = jsonObj.value(SER_FPS).toInt();

        width2 = jsonObj.value(SER_WIDTH2).toInt();
        height2 = jsonObj.value(SER_HEIGHT2).toInt();
        fps2 = jsonObj.value(SER_FPS2).toInt();

        whlink = jsonObj.value(SER_WHLINK).toBool();

        //windowMode = (WindowMode)jsonObj.value(SER_WINDOWMODE).toInt();
        captureSysKeysMode = (CaptureSysKeysMode)jsonObj.value(SER_CAPTURESYSKEYS).toInt();
        enableVsync = jsonObj.value(SER_VSYNC).toBool();
        playAudioOnHost = !jsonObj.value(SER_HOSTAUDIO).toBool();
        // playAudioOnHost = !playAudioOnHost;
        muteOnFocusLoss = jsonObj.value(SER_MUTEONFOCUSLOSS).toBool();
        absoluteMouseMode = jsonObj.value(SER_ABSMOUSEMODE).toBool();
        absoluteTouchMode = jsonObj.value(SER_ABSTOUCHMODE).toBool();
        swapMouseButtons = jsonObj.value(SER_SWAPMOUSEBUTTONS).toBool();
        reverseScrollDirection = jsonObj.value(SER_SWAPMOUSEBUTTONS).toBool();
        swapFaceButtons = jsonObj.value(SER_SWAPFACEBUTTONS).toBool();
        multiController = jsonObj.value(SER_MULTICONT).toBool();
        gamepadMouse = jsonObj.value(SER_GAMEPADMOUSE).toBool();
        backgroundGamepad = jsonObj.value(SER_BACKGROUNDGAMEPAD).toBool();
        gameOptimizations = jsonObj.value(SER_GAMEOPTS).toBool();
        unsupportedFps = jsonObj.value(SER_UNSUPPORTEDFPS).toBool();
        quitAppAfter = jsonObj.value(SER_QUITAPPAFTER).toBool();
        framePacing = jsonObj.value(SER_FRAMEPACING).toBool();
        connectionWarnings = jsonObj.value(SER_CONNWARNINGS).toBool();
        keepAwake = jsonObj.value(SER_KEEPAWAKE).toBool();
        bDebugLog = jsonObj.value(SER_DEBUGLOG).toBool();
        bitrateKbps = jsonObj.value(SER_BITRATE).toInt();
        bOmitAlt = jsonObj.value(SER_OMITALT).toBool();
    }
    else
    {
        qWarning()<<"save setting  config error";
    }
    save();
}

bool StreamingPreferences::DebugOutLogFlag()
{
    return bDebugLog;
}
