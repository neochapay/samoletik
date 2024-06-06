import QtQuick 2.0
import Sailfish.Silica 1.0

Item {
    height: messageText.height

    Row{
        anchors.fill: parent
        spacing: Theme.paddingSmall

        TextField{
            id: messageText
            width: parent.width - sendButtonIcon.width - Theme.paddingSmall*3
        }

        IconButton {
            id: sendButtonIcon
            icon.source: "image://theme/icon-m-send?" + (pressed
                                                         ? Theme.highlightColor
                                                         : Theme.primaryColor)
            enabled: messageText.text.length > 0
            onClicked: {
                messagesModel.sendMessage(messageText.text)
                messageText.text = "";
            }
        }
    }
}

