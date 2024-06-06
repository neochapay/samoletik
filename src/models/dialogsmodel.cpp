#include "dialogsmodel.h"

#include "tlschema.h"
#include <QMutexLocker>
#include <QColor>
#include <QDateTime>
#include "messageutil.h"

//TODO archived chats

DialogsModel::DialogsModel(QObject *parent)
    : QAbstractListModel(parent)
    , m_mutex(QMutex::Recursive)
    , m_dialogs()
    , m_client(nullptr)
    , m_userId(0)
    , m_requestId(0)
    , m_offsets()
    , m_avatarDownloader(nullptr)
    , m_folders(nullptr)
    , m_lastPinnedIndex(-1)
{
}

DialogsModel::~DialogsModel()
{
    if(m_client) {
        delete m_client;
    }
    if(m_avatarDownloader) {
        delete m_avatarDownloader;
    }
    if(m_folders) {
        delete  m_folders;
    }
}

void DialogsModel::setFolders(QObject *model)
{
    QMutexLocker lock(&m_mutex);

    if (m_folders) {
        m_folders->disconnect(this);
    }

    FoldersModel* folders = dynamic_cast<FoldersModel*>(model);
    if(!folders) {
        return;
    } else {
        m_folders = folders;
    }

    foldersChanged(m_folders ? m_folders->folders() : QList<TgObject>());
    connect(m_folders, SIGNAL(foldersChanged(QList<TgObject>)), this, SLOT(foldersChanged(QList<TgObject>)));
}

QObject* DialogsModel::folders() const
{
    return m_folders;
}

void DialogsModel::foldersChanged(QList<TgObject> folders)
{
    QMutexLocker lock(&m_mutex);

    for (qint32 i = 0; i < m_dialogs.size(); ++i) {
        TgObject row = m_dialogs[i];

        TgList dialogFolders;

        for (qint32 j = 0; j < folders.size(); ++j) {
            if (FoldersModel::matchesFilter(folders[j], qDeserialize(row["peerBytes"].toByteArray()).toMap())) {
                dialogFolders << j;
            }
        }

        row["folders"] = dialogFolders;

        m_dialogs[i] = row;

        emit dataChanged(index(i), index(i));
    }
}

void DialogsModel::resetState()
{
    if (!m_dialogs.isEmpty()) {
        beginRemoveRows(QModelIndex(), 0, m_dialogs.size() - 1);
        m_dialogs.clear();
        endRemoveRows();
    }

    m_requestId = 0;
    m_offsets = TgObject();
    m_offsets["_start"] = true;
    m_lastPinnedIndex = -1;
}

QHash<int, QByteArray> DialogsModel::roleNames() const
{
    static QHash<int, QByteArray> roles;

    if (!roles.isEmpty())
        return roles;

    roles[TitleRole] = "title";
    roles[ThumbnailColorRole] = "thumbnailColor";
    roles[ThumbnailTextRole] = "thumbnailText";
    roles[AvatarRole] = "avatar";
    roles[MessageTimeRole] = "messageTime";
    roles[MessageTextRole] = "messageText";
    roles[TooltipRole] = "tooltip";
    roles[PeerBytesRole] = "peerBytes";
    roles[MessageSenderNameRole] = "messageSenderName";
    roles[MessageSenderColorRole] = "messageSenderColor";

    return roles;
}

void DialogsModel::setClient(QObject *client)
{
    TgClient* tclient = dynamic_cast<TgClient*>(client);
    if(!tclient) {
        return;
    }

    QMutexLocker lock(&m_mutex);

    if (m_client) {
        m_client->disconnect(this);
    }

    m_client = tclient;
    m_userId = 0;

    resetState();

    connect(m_client, SIGNAL(authorized(TgLongVariant)), this, SLOT(authorized(TgLongVariant)));
    connect(m_client, SIGNAL(messagesDialogsResponse(TgObject,TgLongVariant)), this, SLOT(messagesGetDialogsResponse(TgObject,TgLongVariant)));
    connect(m_client, SIGNAL(gotUpdate(TgObject,TgLongVariant,TgList,TgList,qint32,qint32,qint32)), this, SLOT(gotUpdate(TgObject,TgLongVariant,TgList,TgList,qint32,qint32,qint32)));
}

QObject* DialogsModel::client() const
{
    return m_client;
}

void DialogsModel::setAvatarDownloader(QObject *avatarDownloader)
{
    QMutexLocker lock(&m_mutex);
    AvatarDownloader* tavatarDownloader = dynamic_cast<AvatarDownloader*>(avatarDownloader);
    if(!tavatarDownloader) {
        return;
    }

    if (m_avatarDownloader) {
        m_avatarDownloader->disconnect(this);
    }
    m_avatarDownloader = tavatarDownloader;

    connect(m_avatarDownloader, SIGNAL(avatarDownloaded(TgLongVariant,QString)), this, SLOT(avatarDownloaded(TgLongVariant,QString)));
}

QObject* DialogsModel::avatarDownloader() const
{
    return m_avatarDownloader;
}

int DialogsModel::rowCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent);
    return m_dialogs.size();
}

QVariant DialogsModel::data(const QModelIndex &index, int role) const
{
    if (index.row() < 0) //TODO why this is even calling
        return QVariant();

    return m_dialogs[index.row()][roleNames()[role]];
}

bool DialogsModel::canFetchMoreDownwards() const
{
    if(!m_client) {
        return false;
    }
    return m_client && m_client->isAuthorized() && !m_requestId.toLongLong() && !m_offsets.isEmpty();
}

void DialogsModel::fetchMoreDownwards()
{
    QMutexLocker lock(&m_mutex);   
    if(!m_client) {
        return;
    }

    m_requestId = m_client->messagesGetDialogsWithOffsets(m_offsets, 40);
}

void DialogsModel::authorized(TgLongVariant userId)
{
    QMutexLocker lock(&m_mutex);

    if (m_userId != userId) {
        resetState();
        m_userId = userId;
        fetchMoreDownwards();
    }
}

void DialogsModel::messagesGetDialogsResponse(TgObject data, TgLongVariant messageId)
{
    QMutexLocker lock(&m_mutex);

    if (m_requestId != messageId) {
        return;
    }

    m_requestId = 0;

    switch (GETID(data)) {
    case TLType::MessagesDialogs:
    case TLType::MessagesDialogsNotModified:
        m_offsets = TgObject();
        break;
    case TLType::MessagesDialogsSlice:
        m_offsets = TgClient::getDialogsOffsets(data);
        break;
    }

    TgList dialogsList = data["dialogs"].toList();
    TgList messagesList = data["messages"].toList();
    TgList usersList = data["users"].toList();
    TgList chatsList = data["chats"].toList();

    globalUsers().append(usersList);
    globalChats().append(chatsList);

    if (dialogsList.isEmpty()) {
        m_offsets = TgObject();
        return;
    }

    QList<TgObject> dialogsRows;
    dialogsRows.reserve(dialogsList.size());

    QList<TgObject> folders;
    if (m_folders) {
        folders = m_folders->folders();
    }

    for (qint32 i = 0; i < dialogsList.size(); ++i) {
        TgObject lastDialog = dialogsList[i].toMap();

        if (lastDialog["pinned"].toBool()) {
            m_lastPinnedIndex = qMax(m_lastPinnedIndex, m_dialogs.size() + i);
        }

        TgObject lastDialogPeer = lastDialog["peer"].toMap();
        TgInt lastMessageId = lastDialog["top_message"].toInt();

        TgObject lastMessage;
        TgObject lastPeer;

        for (qint32 j = messagesList.size() - 1; j >= 0; --j) {
            TgObject message = messagesList[j].toMap();
            if (TgClient::peersEqual(message["peer_id"].toMap(), lastDialogPeer)
                && message["id"].toInt() == lastMessageId) {
                lastMessage = message;
                break;
            }
        }

        if (TgClient::isUser(lastDialogPeer)) for (qint32 j = 0; j < usersList.size(); ++j) {
                TgObject peer = usersList[j].toMap();
                if (TgClient::peersEqual(peer, lastDialogPeer)) {
                    lastPeer = peer;
                    break;
                }
            }
        if (TgClient::isChat(lastDialogPeer)) for (qint32 j = 0; j < chatsList.size(); ++j) {
                TgObject peer = chatsList[j].toMap();
                if (TgClient::peersEqual(peer, lastDialogPeer)) {
                    lastPeer = peer;
                    break;
                }
            }

        TgObject fromId = lastMessage["from_id"].toMap();
        TgObject messageSender;

        if (TgClient::isUser(fromId)) for (qint32 j = 0; j < usersList.size(); ++j) {
                TgObject peer = usersList[j].toMap();
                if (TgClient::peersEqual(peer, fromId)) {
                    messageSender = peer;
                    break;
                }
            }
        if (TgClient::isChat(fromId)) for (qint32 j = 0; j < chatsList.size(); ++j) {
                TgObject peer = chatsList[j].toMap();
                if (TgClient::peersEqual(peer, fromId)) {
                    messageSender = peer;
                    break;
                }
            }

        dialogsRows.append(createRow(lastDialog, lastPeer, lastMessage, messageSender, folders, usersList, chatsList));
    }

    beginInsertRows(QModelIndex(), m_dialogs.size(), m_dialogs.size() + dialogsRows.size() - 1);
    m_dialogs.append(dialogsRows);
    endInsertRows();

    if (m_avatarDownloader) {
        for (qint32 i = 0; i < usersList.size(); ++i) {
            m_avatarDownloader->downloadAvatar(usersList[i].toMap());
        }
        for (qint32 i = 0; i < chatsList.size(); ++i) {
            m_avatarDownloader->downloadAvatar(chatsList[i].toMap());
        }
    }

    if (canFetchMoreDownwards()) {
        fetchMoreDownwards();
    }
}

void DialogsModel::handleDialogMessage(TgObject &row, TgObject message, TgObject messageSender, TgList users, TgList chats)
{
    //TODO 12-hour format
    row["messageTime"] = QDateTime::fromTime_t(qMax(message["date"].toInt(), message["edit_date"].toInt())).toString("hh:mm");

    QString messageSenderName;

    row["messageOut"] = message["out"].toBool();

    if (message["out"].toBool()) {
        if (ID(message["action"].toMap()) != 0) {
            messageSenderName = "You";
        }
    } else if (TgClient::isUser(messageSender)) {
        messageSenderName = messageSender["first_name"].toString();
    } else {
        messageSenderName = messageSender["title"].toString();
    }

    if (!messageSenderName.isEmpty()) {
        if (ID(message["action"].toMap()) == 0) {
            messageSenderName += ": ";
        } else {
            messageSenderName += " ";
        }
    }

    row["messageSenderName"] = messageSenderName;
    row["messageSenderColor"] = AvatarDownloader::userColor(messageSender["id"]);

    QString messageText = prepareDialogItemMessage(message["message"].toString(), message["entities"].toList());
    QString afterMessageText;
    if (GETID(message["media"].toMap()) != 0) {
        if (!messageText.isEmpty()) {
            afterMessageText += ", ";
        }

        //TODO attachment type
        afterMessageText += "Attachment";
    }
    messageText += afterMessageText;
    row["messageText"] = messageText;

    handleMessageAction(row, message, messageSender, users, chats);
}

TgObject DialogsModel::createRow(TgObject dialog, TgObject peer, TgObject message, TgObject messageSender, QList<TgObject> folders, TgList users, TgList chats)
{
    TgObject row;

    row["peer"] = peer;
    row["pinned"] = dialog["pinned"].toBool();
    row["silent"] = dialog["notify_settings"].toMap()["silent"].toBool();

    TgObject inputPeer = peer;
    inputPeer.unite(dialog);
    ID_PROPERTY(inputPeer) = ID_PROPERTY(peer);
    row["peerBytes"] = qSerialize(inputPeer);

    TgList dialogFolders;

    for (qint32 j = 0; j < folders.size(); ++j) {
        if (FoldersModel::matchesFilter(folders[j], inputPeer)) {
            dialogFolders << j;
        }
    }

    row["folders"] = dialogFolders;

    //TODO typing status
    if (TgClient::isUser(peer)) {
        row["title"] = QString(peer["first_name"].toString() + " " + peer["last_name"].toString());
        row["tooltip"] = "user"; //TODO last seen and online
    } else {
        row["title"] = peer["title"].toString();

        QString tooltip = TgClient::isChannel(peer) ? "channel" : "chat";
        if (!peer["participants_count"].isNull()) {
            tooltip = peer["participants_count"].toString();
            //TODO localization support
            tooltip += TgClient::isChannel(peer) ? " subscribers" : " members";
        }

        row["tooltip"] = tooltip;
    }

    row["thumbnailColor"] = AvatarDownloader::userColor(peer["id"].toLongLong());
    row["thumbnailText"] = AvatarDownloader::getAvatarText(row["title"].toString());
    row["avatar"] = "";
    row["photoId"] = peer["photo"].toMap()["photo_id"];

    handleDialogMessage(row, message, messageSender, users, chats);

    return row;
}

void DialogsModel::avatarDownloaded(TgLongVariant photoId, QString filePath)
{
    QMutexLocker lock(&m_mutex);

    for (qint32 i = 0; i < m_dialogs.size(); ++i) {
        TgObject dialog = m_dialogs[i];

        if (dialog["photoId"] != photoId) {
            continue;
        }

        dialog["avatar"] = filePath;
        m_dialogs[i] = dialog;

        emit dataChanged(index(i), index(i));
        break;
    }
}

void DialogsModel::refresh()
{
    resetState();
    fetchMoreDownwards();
}

bool DialogsModel::inFolder(qint32 index, qint32 folderIndex)
{
    QMutexLocker lock(&m_mutex);

    if (!m_folders || index < 0 || folderIndex < 0)
        return true;

    return m_dialogs[index]["folders"].toList().contains(folderIndex);
}

void DialogsModel::gotMessageUpdate(TgObject update, TgLongVariant messageId)
{
    Q_UNUSED(messageId);
    QMutexLocker lock(&m_mutex);




    if (!m_offsets.isEmpty()) {
        return;
    }

    TgObject peerId;
    TgObject fromId;
    qint64 fromIdNumeric;

    //TODO this should be handled by singleton object with data from DB
    if (ID(update) == TLType::UpdateShortSentMessage) { //TODO what if we don't know about message?
        if (ID(update["peer_id"].toMap()) == 0) {
            return;
        }

        peerId = update["peer_id"].toMap();

        fromIdNumeric = m_client->getUserId().toLongLong();
    } else if (update["user_id"].toLongLong()) {
        ID_PROPERTY(peerId) = TLType::PeerUser;
        peerId["user_id"] = update["user_id"];

        fromIdNumeric = update["out"].toBool() ? m_client->getUserId().toLongLong() : update["user_id"].toLongLong();
    } else if (update["chat_id"].toLongLong()) {
        ID_PROPERTY(peerId) = TLType::PeerChat;
        peerId["chat_id"] = update["chat_id"];

        fromIdNumeric = update["from_id"].toLongLong();
    } else {
        return;
    }

    qint32 rowIndex = -1;
    for (qint32 i = 0; i < m_dialogs.size(); ++i) {
        if (TgClient::peersEqual(m_dialogs[i]["peer"].toMap(), peerId)) {
            rowIndex = i;
            break;
        }
    }

    if (rowIndex == -1) {
        return;
    }

    TgObject sender;
    for (qint32 j = 0; j < globalUsers().size(); ++j) {
        TgObject peer = globalUsers()[j].toMap();
        if (TgClient::getPeerId(peer) == fromIdNumeric) {
            sender = peer;
            break;
        }
    }
    if (ID(sender) == 0) for (qint32 j = 0; j < globalChats().size(); ++j) {
            TgObject peer = globalChats()[j].toMap();
            if (TgClient::getPeerId(peer) == fromIdNumeric) {
                sender = peer;
                break;
            }
        }

    if (TgClient::isChannel(sender)) {
        ID_PROPERTY(fromId) = TLType::PeerChannel;
        fromId["channel_id"] = TgClient::getPeerId(sender);
    } else if (TgClient::isChat(sender)) {
        ID_PROPERTY(fromId) = TLType::PeerChat;
        fromId["chat_id"] = TgClient::getPeerId(sender);
    } else if (TgClient::isUser(sender)) {
        ID_PROPERTY(fromId) = TLType::PeerUser;
        fromId["user_id"] = TgClient::getPeerId(sender);
    }

    update["peer_id"] = peerId;
    update["from_id"] = fromId;

    handleDialogMessage(m_dialogs[rowIndex], update, sender, globalUsers(), globalChats());
    emit dataChanged(index(rowIndex), index(rowIndex));

    prepareNotification(m_dialogs[rowIndex]);

    if (m_dialogs[rowIndex]["pinned"].toBool()) {
        return;
    }

    if (beginMoveRows(QModelIndex(), rowIndex, rowIndex, QModelIndex(), m_lastPinnedIndex + 1)) {
        m_dialogs.insert(m_lastPinnedIndex + 1, m_dialogs.takeAt(rowIndex));
        endMoveRows();
    }
}

void DialogsModel::gotUpdate(TgObject update, TgLongVariant messageId, TgList users, TgList chats, qint32 date, qint32 seq, qint32 seqStart)
{
    Q_UNUSED(messageId);
    Q_UNUSED(date)
    Q_UNUSED(seq)
    Q_UNUSED(seqStart)

    QMutexLocker lock(&m_mutex);
    if(!m_client) {
        return;
    }
    //We should avoid duplicates. (implement DB)
    //    _globalUsers.append(users);
    //    _globalChats.append(chats);

    switch (ID(update)) {
    case TLType::UpdateNewMessage:
    case TLType::UpdateNewChannelMessage:
    {
        if (!m_offsets.isEmpty()) {
            return;
        }

        TgObject message = update["message"].toMap();

        qint32 rowIndex = -1;
        for (qint32 i = 0; i < m_dialogs.size(); ++i) {
            if (TgClient::peersEqual(m_dialogs[i]["peer"].toMap(), message["peer_id"].toMap())) {
                rowIndex = i;
                break;
            }
        }

        if (rowIndex == -1) {
            return;
        }

        TgObject fromId = message["from_id"].toMap();
        TgObject sender;
        if (TgClient::isUser(fromId)) for (qint32 j = 0; j < users.size(); ++j) {
                TgObject peer = users[j].toMap();
                if (TgClient::peersEqual(peer, fromId)) {
                    sender = peer;
                    break;
                }
            }
        if (TgClient::isChat(fromId)) for (qint32 j = 0; j < chats.size(); ++j) {
                TgObject peer = chats[j].toMap();
                if (TgClient::peersEqual(peer, fromId)) {
                    sender = peer;
                    break;
                }
            }
        if (TgClient::commonPeerType(fromId) == 0) {
            //This means that it is a channel feed or personal messages.
            //Authorized user is returned by API, so we don't need to put it manually.
            sender = m_dialogs[rowIndex]["peer"].toMap();
        }

        message["out"] = TgClient::getPeerId(sender) == m_client->getUserId();

        handleDialogMessage(m_dialogs[rowIndex], message, sender, users, chats);
        emit dataChanged(index(rowIndex), index(rowIndex));

        prepareNotification(m_dialogs[rowIndex]);

        if (m_dialogs[rowIndex]["pinned"].toBool()) {
            return;
        }

        if (beginMoveRows(QModelIndex(), rowIndex, rowIndex, QModelIndex(), m_lastPinnedIndex + 1)) {
            m_dialogs.insert(m_lastPinnedIndex + 1, m_dialogs.takeAt(rowIndex));
            endMoveRows();
        }

        break;
    }
    }
}

void DialogsModel::prepareNotification(TgObject row)
{
    if (row["messageOut"].toBool())
        return;

    emit sendNotification(TgClient::getPeerId(row["peer"].toMap()).toLongLong(),
                          row["title"].toString(),
                          row["messageSenderName"].toString(),
                          row["messageText"].toString(),
                          row["silent"].toBool());
}
