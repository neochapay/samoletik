import QtQuick 2.0
import Sailfish.Silica 1.0

Item {
    width: parent.width
    height: Theme.itemSizeLarge

    MouseArea {
        anchors.fill: parent
        onClicked: {
            openDialog();
        }
    }

    Rectangle {
        id: avatarRect
        visible: avatar.length == 0 || avatarImage.status != Image.Ready

        anchors.verticalCenter: parent.verticalCenter
        anchors.left: parent.left
        anchors.leftMargin: Theme.paddingSmall

        width: parent.height * 0.8
        height: width
        smooth: true

        color: thumbnailColor
        radius: width / 2

        Text {
            anchors.fill: parent
            text: thumbnailText
            color: Theme.primaryColor
            font.bold: true
            font.pixelSize: Theme.fontSizeExtraLarge
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
    }

    Image {
        id: avatarImage
        visible: avatar.length != 0

        anchors.left: parent.left
        anchors.verticalCenter: parent.verticalCenter
        anchors.leftMargin: avatarRect.anchors.leftMargin

        width: parent.height * 0.8
        height: width
        smooth: true

        asynchronous: true
        source: avatar
    }

    Column {
        anchors.left: avatarRect.right
        anchors.right: parent.right
        anchors.verticalCenter: avatarRect.verticalCenter
        anchors.leftMargin: avatarRect.anchors.leftMargin
        anchors.rightMargin: anchors.leftMargin

        Item {
            anchors.left: parent.left
            anchors.right: parent.right
            height: childrenRect.height

            Text {
                text: title
                elide: Text.ElideRight
                font.pixelSize: Theme.fontSizeLarge
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.right: messageTimeText.left
            }

            Text {
                id: messageTimeText
                text: messageTime
                color: "#999999"
                font.pixelSize: Theme.fontSizeSmall
                anchors.top: parent.top
                anchors.right: parent.right
            }
        }

        Item {
            anchors.left: parent.left
            anchors.right: parent.right
            height: messageSenderLabel.height

            Text {
                id: messageSenderLabel
                text: messageSenderName
                color: messageSenderColor
                font.pixelSize: Theme.fontSizeLarge
                anchors.top: parent.top
                anchors.left: parent.left
            }

            Text {
                id: messageTextLabel
                text: messageText
                color: "#8D8D8D"
                elide: Text.ElideRight
                font.pixelSize: Theme.fontSizeSmall
                anchors.top: parent.top
                anchors.left: messageSenderLabel.right
                anchors.right: parent.right

                onLinkActivated: {
                    openDialog();
                }
            }
        }
    }

    function openDialog() {
        pageStack.push(Qt.resolvedUrl("../pages/ConverstationPage.qml"),
                       {
                            dialogTitle: title,
                            peer: peerBytes
                       })
    }
}
