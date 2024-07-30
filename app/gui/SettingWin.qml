import QtQuick 2.6
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.3
import QtQuick.Window 2.2

import StreamingPreferences 1.0
Window {
       id: settingchidren
       x:screen.width/5.5
       y:screen.height/5.7
       width: 1280
       height: 680
       visible: true
       color: "#000000"
        flags: Qt.Window | Qt.FramelessWindowHint|Qt.WindowStaysOnTopHint

        SettingsView
        {
           id: settingwindow
           width: 1307
           height: 680
           x:-9
        }

       onClosing: {
                console.log("SettingWin onClosing");
                StreamingPreferences.save()
                hide()
                close()
            }
       Rectangle {
               id: titleBar
               anchors.top: parent.top
               anchors.left: parent.left
               anchors.right: parent.right
               height: 35
               color: releasedColor
               state: "Released"

               property string pressedColor: "#2e6b89"
               property string releasedColor: "#263238"
               MouseArea{
                   id: mouseArea
                   anchors.fill: parent
                   hoverEnabled: true
                   acceptedButtons: Qt.LeftButton | Qt.RightButton
                   property point clickPos: "0, 0"
                   onPressed: {
                       if(mouse.button == Qt.LeftButton){
                            //clickPos = Qt.point(mouseX, mouseY)
                            clickPos = Qt.point(mouse.x, mouse.y)
                       }
                   }
                   onPositionChanged: {

                       if(pressed){
                            //var mousePos = Qt.point(mouseX-clickPos.x, mouseY-clickPos.y)
                            //var x = window.x + mousePos.x
                           // var y = window.y + mousePos.y
                           // settingchidren.setX(x)
                           // settingchidren.setY(y)


                           var pos = Qt.point(mouse.x - clickPos.x, mouse.y - clickPos.y)
                           settingchidren.setX(settingchidren.x + pos.x)
                           settingchidren.setY(settingchidren.y + pos.y)
                       }
                   }
              }
               //软件Icon
               /*
               Image {
                   id: icon
                   y: 2
                   width: 26
                   height: 26
                   anchors.left: parent.left
                   anchors.margins: 5
                   source: icon
               }

               Text {
                   anchors.left: icon.right
                   width: 26
                   height: 26
                   y:2
                   color: "white"
                   text: qsTr(softName)
                   font.pointSize: 12
                   horizontalAlignment: Text.AlignLeft
                   verticalAlignment: Text.AlignVCenter
               }
                */
               //关闭按钮

               NavigableToolButton {
                   id: settingsButton
                    x:1240
                    y:-4
                   iconSource:  "qrc:/res/baseline-cancel-24px.svg"

                   onClicked:{
                       console.log("Button onClosing");
                       StreamingPreferences.save()
                       hide()
                       close()
                      }
                   ToolTip.delay: 1000
                   ToolTip.timeout: 3000
                   ToolTip.visible: hovered
                   ToolTip.text: qsTr("close") + (settingsShortcut.nativeText ? (" ("+settingsShortcut.nativeText+")") : "(Alt+F4)")
                }

               //标题栏颜色动效
              /*states: [
                   State { name: "Pressed"; PropertyChanges { target: titleBar; color : pressedColor } },
                   State { name: "Released"; PropertyChanges { target: titleBar; color : releasedColor }}
               ]
               transitions: [
                   Transition { ColorAnimation { to: releasedColor; duration: 200 } },
                   Transition { ColorAnimation { to: pressedColor; duration: 200  } }
               ]*/
           }
        }


