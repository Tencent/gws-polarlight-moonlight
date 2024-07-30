import QtQuick 2.6
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.3
import QtQuick.Window 2.2
import QtQuick 2.12

import StreamingPreferences 1.0
import SystemProperties 1.0
import SdlGamepadKeyNavigation 1.0
Window {
    property int fullFlag: 0

    id: window
    visible: false
    title: qsTr("QML无边框拖拽")
    //color: '#1abc9c'
    color: Qt.rgba(0.5,0.5,0.5,0.5)
    flags: Qt.WindowStaysOnTopHint|Qt.FramelessWindowHint|Qt.Widge|Qt.WindowSystemMenuHint
    width: 35
    height: 35
    MouseArea{
        id: mouseArea
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        property point clickPos: "0, 0"

        onPressed: {
            if(mouse.button == Qt.LeftButton){
                clickPos = Qt.point(mouseX, mouseY)
                width: Screen.desktopAvailableWidth
                height: Screen.desktopAvailableHeight
                console.log("onLeftPressed",StreamingPreferences.width," : ",StreamingPreferences.height)
                console.log("screen", Screen.width," : ", Screen.height)
                screen.width
                fullFlag = 0
                if (SystemProperties.hasDesktopEnvironment) {
                    if (StreamingPreferences.uiDisplayMode == StreamingPreferences.UI_WINDOWED) fullFlag =  2
                    else if (StreamingPreferences.uiDisplayMode == StreamingPreferences.UI_MAXIMIZED) fullFlag =  1
                    else if (StreamingPreferences.uiDisplayMode == StreamingPreferences.UI_FULLSCREEN) fullFlag =  0
                } else {
                    fullFlag =  0
                }

            }
            if(mouse.button == Qt.RightButton){
                newWindow.show()
                console.log("onRightPressed")
            }
        }

        onPositionChanged: {

            if(pressed){

                var mousePos = Qt.point(mouseX-clickPos.x, mouseY-clickPos.y)
                var x = window.x + mousePos.x
                var y = window.y + mousePos.y
                //if(fullFlag == 2)
                {
                    //if(x >= 64  &&  x <= 1984)
                    {
                       // if(y >= 35  &&  y <= 1115)
                         {
                             window.setX(x)
                             window.setY(y)
                         }
                    }

                }
            }
        }
    }
    Rectangle {
        width: 40
        height: 40
        anchors.centerIn: parent
        color: Qt.rgba(0.5,0.5,0.5,0.5)
        Image {
            id: image
            //source: "qrc:/res/settings.svg"
            //source: "/res/button.png"

            width: 40
            height: 40

        }
    }

    SettingWin {
        id: newWindow
        visible: true
    }

}


