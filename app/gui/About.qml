import QtQuick 2.6
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.3
import QtQuick.Window 2.2

Window {
       id: about
       x:screen.width/2.5
       y:screen.height/2.7
       minimumWidth: 420
       maximumWidth: 420

       minimumHeight: 320
       maximumHeight: 320
       visible: true
	   flags: Qt.Window |Qt.WindowStaysOnTopHint| Qt.MSWindowsFixedSizeDontScale
       Column {
               anchors.fill: parent
               spacing: 5

               // 第一层
               Rectangle {
                   width: parent.width
                   height: 100
                   RowLayout {
                                anchors.centerIn: parent
                                 Image {

                                     fillMode: Image.PreserveAspectFit
                                     source: "qrc:\about.png"
                                     Layout.preferredWidth: 85
                                     Layout.preferredHeight: 85
                                     anchors.horizontalCenter: parent.horizontalCenter
                                 }


                             }
               }

               // 第二层
               Rectangle {
                   width: parent.width
                   height: 20
                   z: 4
                   ColumnLayout {
                              spacing: 5
                              anchors.centerIn: parent
                              Text {
                                  font.bold: true
                                  text: "Remote Graphic Workstation"
                                  font.pixelSize: 20
                                  horizontalAlignment: Text.AlignHCenter
                              }

                          }
               }
               // 第三层
               Rectangle {
                   width: parent.width
                   height: 20
                   z: 3
                   RowLayout {
                              anchors.centerIn: parent
                              Text {
                                  color: "#7E8B99"
                                  font.bold: true
                                  text: "Version 3.3.1"
                                  font.pixelSize: 17
                                  horizontalAlignment: Text.AlignHCenter
                              }


                          }
               }
               // 第四层
               Rectangle {
                   width: parent.width
                   height: 45
                   z: 2

                   RowLayout {
                              anchors.centerIn: parent
                              anchors.bottom: parent.bottom

                               Text {
                                   text: "<a href='https://www.cloudstudio.games/terms'>Terms of Service</a>"
                                   font.pixelSize: 15
                                   textFormat: Text.RichText
                                   onLinkActivated: Qt.openUrlExternally(link)
                               }
                               Text {
                                              text: "|"
                                              color: "#7E8B92"
                                              font.pixelSize: 15
                                          }
                               Text {
                                   text: "<a href='https://www.cloudstudio.games/privacy-policy'>Privacy Policy</a>"
                                   font.pixelSize: 15
                                   textFormat: Text.RichText
                                   onLinkActivated: Qt.openUrlExternally(link)
                               }
                           }
               }
               // 第五层
               Rectangle {
                   width: parent.width
                   height: 50
                   z: 1
                  // ColumnLayout {

                              Text {
                                  color: "#7E8B92"
                                  text: "     @2024 Tencent America LLC or its affiliates. All \ntrademarks are the property of their respective owners.
                                All rights reserved."
                                  font.pixelSize: 15
                                  anchors.horizontalCenter: parent.horizontalCenter
                              }
                       //   }
               }
           }


}


