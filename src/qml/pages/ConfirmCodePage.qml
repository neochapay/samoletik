import QtQuick 2.0
import Sailfish.Silica 1.0

Page {
    id: codePage
    allowedOrientations:  Orientation.All

    property string phoneNumber
    property string phoneCache

    Component.onCompleted: console.log(phoneCache)

    SilicaFlickable {
        anchors.fill: parent
        VerticalScrollDecorator {}

        PageHeader {
            id: header
            title: qsTr("Confirm code")
        }

        Column {
            id: contentColumn
            spacing: Theme.paddingMedium

            anchors {
                top: header.bottom;
                topMargin: Theme.paddingMedium;
                left: parent.left;
                right: parent.right;
            }

            Label {
                text : qsTr("Confirmation Code")
                width: parent.width - Theme.paddingMedium *2
                wrapMode: Text.WordWrap
                horizontalAlignment: Text.AlignHCenter
            }

            TextField {
                id: codeEdit
                width: parent.width - Theme.paddingMedium *2
                horizontalAlignment: Text.AlignHCenter
                placeholderText: qsTr("Please, enter your confirmation code.")
            }

            Button{
                id: sendButton
                text: qsTr("Confirm")
                width: parent.width - Theme.paddingMedium *2
                anchors.horizontalCenter: parent.horizontalCenter
                onClicked: {
                    telegramClient.authSignIn(codePage.phoneNumber, codePage.phoneCache, codeEdit.text);
                }
            }
        }
    }
}
