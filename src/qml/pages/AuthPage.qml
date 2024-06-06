import QtQuick 2.0
import Sailfish.Silica 1.0

Page {
    id: authPage
    allowedOrientations:  Orientation.All

    Component.onCompleted: {
        telegramClient.resetSession();
        telegramClient.start();
    }

    Connections{
        target: telegramClient
        onAuthSentCodeResponse: {
            var phoneCodeCache = data["phone_code_hash"];
            pageStack.push(Qt.resolvedUrl("ConfirmCodePage.qml") ,
                           {
                               phoneNumber: phoneEdit.text,
                               phoneCache: phoneCodeCache
                           });
        }
    }

    SilicaFlickable {
        anchors.fill: parent
        VerticalScrollDecorator {}

        PageHeader {
            id: header
            title: qsTr("Login")
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
                text : qsTr("Your Phone Number")
                width: parent.width - Theme.paddingMedium *2
                wrapMode: Text.WordWrap
                horizontalAlignment: Text.AlignHCenter
            }

            TextField {
                id: phoneEdit
                width: parent.width - Theme.paddingMedium *2
                horizontalAlignment: Text.AlignHCenter
                placeholderText: qsTr("Please, enter your phone number")
            }

            Button{
                id: sendButton
                text: qsTr("Send me code")
                onClicked: {
                    var phoneNumber = phoneEdit.text;
                    phoneNumber.replace(' ', "");
                    phoneNumber.replace('-', "");
                    telegramClient.authSendCode(phoneNumber);
                }
                width: parent.width - Theme.paddingMedium *2
                anchors.horizontalCenter: parent.horizontalCenter
            }
        }
    }
}
