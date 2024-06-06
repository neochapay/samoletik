import QtQuick 2.0
import Sailfish.Silica 1.0

import ru.neochapay.samoletik 1.0
import "../components"

Page {
    id: converstationPage
    allowedOrientations:  Orientation.All

    property string dialogTitle: qsTr("Loading...")
    property var peer

    SilicaFlickable {
        anchors.fill: parent
        VerticalScrollDecorator {}

        PageHeader {
            id: header
            title: converstationPage.dialogTitle
        }

        SilicaListView {
            id: messageList
            width: parent.width - Theme.paddingMedium * 2
            height: parent.height - header.height - Theme.paddingMedium * 4 - editMessageItem.height
            anchors{
                top: header.bottom
                topMargin: Theme.paddingMedium
                left: parent.left
                leftMargin: Theme.paddingMedium
            }
            clip: true

            cacheBuffer: Math.max(parent.height / 6, 0)
            snapMode: ListView.SnapToItem

            onMovementEnded: {
                if (atYBeginning && messagesModel.canFetchMoreUpwards()) {
                    messagesModel.fetchMoreUpwards();
                }
                if (atYEnd && messagesModel.canFetchMoreDownwards()) {
                    messagesModel.canFetchMoreDownwards();
                }
            }
            VerticalScrollDecorator {}
            model: messagesModel
            delegate: MessageListItem {}
            onCountChanged: scrollToBottom()
            Component.onCompleted: scrollToBottom()

            Connections{
                target: messagesModel
                onScrollForNew: {
                    messageList.scrollToBottom();
                }
                onScrollTo: {
                    messageList.currentIndex = index
                }
            }
        }

        EditMessageItem{
            id: editMessageItem
            width: parent.width - Theme.paddingMedium * 2
            anchors{
                bottom: parent.bottom
                bottomMargin: Theme.paddingMedium
                left: parent.left
                leftMargin: Theme.paddingMedium
            }
        }
    }

    onPeerChanged: {
        messagesModel.peer = converstationPage.peer
    }
}
