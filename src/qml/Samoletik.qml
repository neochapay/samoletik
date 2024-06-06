import QtQuick 2.0
import Sailfish.Silica 1.0

import Kutegram 1.0
import ru.neochapay.samoletik 1.0

ApplicationWindow {
    id: root
    property bool needAuth: true

    allowedOrientations: Orientation.Portrait
    initialPage: getInitPage()

    cover: Qt.resolvedUrl("cover/CoverPage.qml")

    TgClient {
        id: telegramClient

        onInitialized: {
            if (hasUserId) {
                return;
            }
        }

        onAuthorized: {
            pageStack.clear()
            pageStack.push(Qt.resolvedUrl("pages/ConverstationListPage.qml"))
        }
    }

    FoldersModel {
        id: foldersModel
        client: telegramClient
    }

    DialogsModel {
        id: dialogsModel
        folders: foldersModel
        client: telegramClient
        avatarDownloader: globalAvatarDownloader
    }

    AvatarDownloader {
        id: globalAvatarDownloader
        client: telegramClient
    }

    MessagesModel{
        id: messagesModel
        client: telegramClient
        avatarDownloader: globalAvatarDownloader
    }

    function getInitPage() {
        if (telegramClient.hasSession()) {
            return Qt.resolvedUrl("pages/ConverstationListPage.qml")
        } else {
            return Qt.resolvedUrl("pages/AuthPage.qml")
        }
    }
}
