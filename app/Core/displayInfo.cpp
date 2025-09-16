#include "displayInfo.h"

namespace GleamCore {
    bool IsVerticalRotation(DXGI_MODE_ROTATION rotation) {
        return rotation == DXGI_MODE_ROTATION_ROTATE90 || rotation == DXGI_MODE_ROTATION_ROTATE270;
    }
    // Function to parse JSON and extract display information
    QList<DisplayInfo> parseDisplayInfo(const QJsonValue& jsonValue) {
        QList<DisplayInfo> displayInfos;

        // Parse the JSON string
        if (!jsonValue.isArray()) {
            qWarning() << "Invalid JSON format";
            return displayInfos;
        }

        QJsonArray displayArray = jsonValue.toArray();
        for (const QJsonValue& value : displayArray) {
            if (!value.isObject()) {
                continue;
            }

            QJsonObject displayObj = value.toObject();
            if (!displayObj.contains("Name")) {
                continue;
            }

            DisplayInfo info;
            info.name = displayObj["Name"].toString();
            info.shortName = displayObj["ShortName"].toString();
            info.isPhysical = displayObj["IsAuroraDisplay"].toInt() != 1;

            info.x = displayObj["X"].toInt();
            info.y = displayObj["Y"].toInt();
            info.width = displayObj["Width"].toInt();
            info.height = displayObj["Height"].toInt();
            info.rotation = static_cast<DXGI_MODE_ROTATION>(displayObj["Rotation"].toInt());
            displayInfos.append(info);
        }

        return displayInfos;
    }

    void Test() {
        QString jsonString = R"({
        "DisplayChoose": [
            {"Name":"\\\\.\\DISPLAY5[0|0|3840|2160]P","Stream":true,"FPS":60},
            {"Name":"\\\\.\\DISPLAY6[100|100|1920|1080]P","Stream":false,"FPS":30}
        ]
    })";

        QList<DisplayInfo> displayInfos = parseDisplayInfo(jsonString);

        for (const DisplayInfo& info : displayInfos) {
            qDebug() << "Name:" << info.name;
            qDebug() << "Coordinates:" << info.x << "," << info.y << "," << info.width << "," << info.height;
            qDebug() << "Stream:" << (info.stream ? "true" : "false");
        }
    }
}
