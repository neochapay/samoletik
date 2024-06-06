import QtQuick 2.0
import Sailfish.Silica 1.0

import "../components"

Page {
    id: codePage
    allowedOrientations:  Orientation.All

    property string phoneNumber
    property string phoneCache

    Component.onCompleted: {
        telegramClient.start();
    }

    Connections{
        target: telegramClient
        onAuthorized: {
            foldersModel.refresh()
            dialogsModel.refresh()
        }
    }

    SilicaFlickable {
        anchors.fill: parent
        VerticalScrollDecorator {}

        PageHeader {
            id: header
            title: qsTr("Converstations")
        }

        ListView {
            id: folderSlide
            width: parent.width - Theme.paddingMedium * 2
            height: parent.height - header.height
            anchors{
                top: header.bottom
                topMargin: Theme.paddingMedium
                left: parent.left
                leftMargin: Theme.paddingMedium
            }

            clip: true
            model: dialogsModel
            boundsBehavior: Flickable.StopAtBounds
            snapMode: ListView.SnapOneItem
            highlightRangeMode: ListView.StrictlyEnforceRange
            highlightFollowsCurrentItem: true
            highlightMoveDuration: 200
            delegate:  ConverstationListItem{}
        }
    }
}
