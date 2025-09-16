#ifndef _DISPLAY_INFO_H_
#define _DISPLAY_INFO_H_
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>
#include <QRegularExpression>
#include <QNetworkReply>
#ifdef Q_OS_WIN32
#include <dxgi.h>
#else
#ifdef Q_OS_DARWIN
typedef enum DXGI_MODE_ROTATION
{
    DXGI_MODE_ROTATION_UNSPECIFIED  = 0,
    DXGI_MODE_ROTATION_IDENTITY     = 1,
    DXGI_MODE_ROTATION_ROTATE90     = 2,
    DXGI_MODE_ROTATION_ROTATE180    = 3,
    DXGI_MODE_ROTATION_ROTATE270    = 4
} DXGI_MODE_ROTATION;
#endif
#endif

namespace GleamCore {
    bool IsVerticalRotation(DXGI_MODE_ROTATION rotation);

    struct DisplayInfo {
        QString name;
        QString shortName;
        int x;
        int y;
        int width;
        int height;
        DXGI_MODE_ROTATION rotation;
        bool isPhysical;
        bool stream;

        bool operator==(const DisplayInfo& other) const {
            return name == other.name &&
                shortName == other.shortName &&
                isPhysical == other.isPhysical &&
                x == other.x &&
                y== other.y &&
                width == other.width &&
                height == other.height &&
                rotation == other.rotation &&
                stream == other.stream;
        };

        bool operator!=(const DisplayInfo& other) const {
            return name != other.name ||
                shortName != other.shortName ||
                isPhysical != other.isPhysical ||
                x != other.x ||
                y != other.y ||
                width != other.width ||
                height != other.height ||
                rotation != other.rotation ||
                stream != other.stream;
        };
    };

    QList<DisplayInfo> parseDisplayInfo(const QJsonValue& jsonValue);
}
#endif //_DISPLAY_INFO_H_
