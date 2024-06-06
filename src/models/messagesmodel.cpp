#include "messagesmodel.h"

#include "tlschema.h"
#include <QMutexLocker>
#include <QColor>
#include <QDateTime>
#include <QUrl>
#include <QDomDocument>
#include "avatardownloader.h"
#include "../messageutil.h"
#include <QStandardPaths>

using namespace TLType;

#define BATCH_SIZE 40

MessagesModel::MessagesModel(QObject *parent)
    : QAbstractListModel(parent)
    , m_mutex(QMutex::Recursive)
    , m_history()
    , m_client(nullptr)
    , m_userId(0)
    , m_peer()
    , m_inputPeer()
    , m_upRequestId(0)
    , m_downRequestId(0)
    , m_upOffset(0)
    , m_downOffset(0)
    , m_avatarDownloader(nullptr)
    , m_downloadRequests()
    , m_uploadId(0)
    , m_sentMessages()
    , m_media()
{
}

MessagesModel::~MessagesModel()
{
    if(m_client) {
        delete m_client;
    }
    if(m_avatarDownloader) {
        delete m_avatarDownloader;
    }
}

QHash<int, QByteArray> MessagesModel::roleNames() const
{
    static QHash<int, QByteArray> roles;

    if (!roles.isEmpty())
        return roles;

    roles[PeerNameRole] = "peerName";
    roles[MessageTextRole] = "messageText";
    roles[MergeMessageRole] = "mergeMessage";
    roles[MessageTimeRole] = "messageTime";
    roles[SenderNameRole] = "senderName";
    roles[IsChannelRole] = "isChannel";
    roles[ThumbnailColorRole] = "thumbnailColor";
    roles[ThumbnailTextRole] = "thumbnailText";
    roles[AvatarRole] = "avatar";
    roles[HasMediaRole] = "hasMedia";
    roles[MediaImageRole] = "mediaImage";
    roles[MediaTitleRole] = "mediaTitle";
    roles[MediaTextRole] = "mediaText";
    roles[MediaDownloadableRole] = "mediaDownloadable";
    roles[MessageIdRole] = "messageId";
    roles[ForwardedFromRole] = "forwardedFrom";
    roles[MediaUrlRole] = "mediaUrl";
    roles[PhotoFileRole] = "photoFile";
    roles[HasPhotoRole] = "hasPhoto";
    roles[MediaSpoilerRole] = "mediaSpoiler";
    roles[PhotoSpoilerRole] = "photoSpoiler";

    return roles;
}

void MessagesModel::resetState()
{
    if (!m_history.isEmpty()) {
        beginRemoveRows(QModelIndex(), 0, m_history.size() - 1);
        m_history.clear();
        endRemoveRows();
    }

    m_peer = TgObject();
    m_inputPeer = TgObject();
    m_upRequestId = 0;
    m_downRequestId = 0;
    m_upOffset = 0;
    m_downOffset = 0;
}

void MessagesModel::setClient(QObject *client)
{
    if(!client) {
        return;
    }

    QMutexLocker lock(&m_mutex);

    if (m_client) {
        m_client->disconnect(this);
    }

    m_client = dynamic_cast<TgClient*>(client);
    m_userId = 0;

    resetState();
    cancelUpload();

    if (!m_client) {
        return;
    }

    connect(m_client, SIGNAL(authorized(TgLongVariant)), this, SLOT(authorized(TgLongVariant)));
    connect(m_client, SIGNAL(messagesMessagesResponse(TgObject,TgLongVariant)), this, SLOT(messagesGetHistoryResponse(TgObject,TgLongVariant)));
    connect(m_client, SIGNAL(fileDownloaded(TgLongVariant,QString)), this, SLOT(fileDownloaded(TgLongVariant,QString)));
    connect(m_client, SIGNAL(fileDownloadCanceled(TgLongVariant,QString)), this, SLOT(fileDownloadCanceled(TgLongVariant,QString)));
    connect(m_client, SIGNAL(gotMessageUpdate(TgObject,TgLongVariant)), this, SLOT(gotMessageUpdate(TgObject,TgLongVariant)));
    connect(m_client, SIGNAL(gotUpdate(TgObject,TgLongVariant,TgList,TgList,qint32,qint32,qint32)), this, SLOT(gotUpdate(TgObject,TgLongVariant,TgList,TgList,qint32,qint32,qint32)));
    connect(m_client, SIGNAL(fileUploading(TgLongVariant,TgLongVariant,TgLongVariant,qint32)), this, SLOT(fileUploading(TgLongVariant,TgLongVariant,TgLongVariant,qint32)));
    connect(m_client, SIGNAL(fileUploaded(TgLongVariant,TgObject)), this, SLOT(fileUploaded(TgLongVariant,TgObject)));
    connect(m_client, SIGNAL(fileUploadCanceled(TgLongVariant)), this, SLOT(fileUploadCanceled(TgLongVariant)));
}

QObject* MessagesModel::client() const
{
    return m_client;
}

void MessagesModel::setAvatarDownloader(QObject *avatarDownloader)
{
    if(!avatarDownloader) {
        return;
    }

    QMutexLocker lock(&m_mutex);

    if (m_avatarDownloader) {
        m_avatarDownloader->disconnect(this);
    }

    m_avatarDownloader = dynamic_cast<AvatarDownloader*>(avatarDownloader);

    if (!m_avatarDownloader) {
        return;
    }

    connect(m_avatarDownloader, SIGNAL(avatarDownloaded(TgLongVariant,QString)), this, SLOT(avatarDownloaded(TgLongVariant,QString)));
    connect(m_avatarDownloader, SIGNAL(photoDownloaded(TgLongVariant,QString)), this, SLOT(photoDownloaded(TgLongVariant,QString)));
}

QObject* MessagesModel::avatarDownloader() const
{
    return m_avatarDownloader;
}

void MessagesModel::setPeer(QByteArray bytes)
{
    QMutexLocker lock(&m_mutex);

    resetState();
    m_downloadRequests.clear();

    m_peer = qDeserialize(bytes).toMap();
    m_inputPeer = TgClient::toInputPeer(m_peer);
    cancelUpload();

    m_upOffset = m_downOffset = qMax(m_peer["read_inbox_max_id"].toInt(), m_peer["read_outbox_max_id"].toInt());
    fetchMoreUpwards();
    fetchMoreDownwards();
}

QByteArray MessagesModel::peer() const
{
    return qSerialize(m_peer);
}

int MessagesModel::rowCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent)
    return m_history.size();
}

QVariant MessagesModel::data(const QModelIndex &index, int role) const
{
    if (role == IsChannelRole) {
        return TgClient::isChannel(m_peer);
    }

    if (role == MergeMessageRole) {
        if (index.row() < 1) {
            return false;
        }

        TgObject curr = m_history[index.row()];
        TgObject prev = m_history[index.row() - 1];

        if (!TgClient::peersEqual(curr["sender"].toMap(), prev["sender"].toMap())) {
            return false;
        }

        if (!curr["grouped_id"].isNull() && !prev["grouped_id"].isNull()
            && curr["grouped_id"].toLongLong() == prev["grouped_id"].toLongLong()) {
            return true;
        }

        if (!TgClient::isChannel(m_peer) && curr["date"].toInt() - prev["date"].toInt() < 300) {
            return true;
        }

        return false;
    }

    return m_history[index.row()][roleNames()[role]];
}

bool MessagesModel::canFetchMoreDownwards() const
{
    if(!m_client) {
        return false;
    }

    return m_client && m_client->isAuthorized() && TgClient::commonPeerType(m_inputPeer) != 0 && !m_downRequestId.toLongLong() && m_downOffset != -1;
}

void MessagesModel::fetchMoreDownwards()
{
    if(!m_client) {
        return;
    }

    QMutexLocker lock(&m_mutex);

    m_downRequestId = m_client->messagesGetHistory(m_inputPeer, m_downOffset, 0, -BATCH_SIZE, BATCH_SIZE);
}

bool MessagesModel::canFetchMoreUpwards() const
{
    if(!m_client) {
        return false;
    }
    return m_client && m_client->isAuthorized() && TgClient::commonPeerType(m_inputPeer) != 0 && !m_upRequestId.toLongLong() && m_upOffset != -1;
}

void MessagesModel::fetchMoreUpwards()
{
    if(!m_client) {
        return;
    }

    QMutexLocker lock(&m_mutex);

    m_upRequestId = m_client->messagesGetHistory(m_inputPeer, m_upOffset, 0, 0, BATCH_SIZE);
}

void MessagesModel::authorized(TgLongVariant userId)
{
    QMutexLocker lock(&m_mutex);

    if (m_userId != userId) {
        resetState();
        cancelUpload();
        m_downloadRequests.clear();
        m_userId = userId;
    }
    qDebug() << Q_FUNC_INFO;
}

void MessagesModel::messagesGetHistoryResponse(TgObject data, TgLongVariant messageId)
{
    QMutexLocker lock(&m_mutex);

    if (messageId == m_downRequestId) {
        handleHistoryResponse(data, messageId);
        m_downRequestId = 0;
        return;
    }

    if (messageId == m_upRequestId) {
        handleHistoryResponseUpwards(data, messageId);
        m_upRequestId = 0;
        return;
    }
}

void MessagesModel::handleHistoryResponse(TgObject data, TgLongVariant messageId)
{
    Q_UNUSED(messageId)

    TgList messages = data["messages"].toList();
    TgList chats = data["chats"].toList();
    TgList users = data["users"].toList();

    globalUsers().append(users);
    globalChats().append(chats);

    if (messages.isEmpty()) {
        m_downOffset = -1;
        return;
    }

    QList<TgObject> messagesRows;
    messagesRows.reserve(messages.size());

    for (qint32 i = messages.size() - 1; i >= 0; --i) {
        TgObject message = messages[i].toMap();
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
            sender = m_peer;
        }

        messagesRows.append(createRow(message, sender, users, chats));
    }

    qint32 oldOffset = m_downOffset;
    qint32 newOffset = messages.first().toMap()["id"].toInt();
    if (m_downOffset != newOffset && messages.size() == BATCH_SIZE) {
        m_downOffset = newOffset;
    } else {
        m_downOffset = -1;
    }

    qint32 oldSize = m_history.size();

    beginInsertRows(QModelIndex(), m_history.size(), m_history.size() + messagesRows.size() - 1);
    m_history.append(messagesRows);
    endInsertRows();

    if (oldSize > 0) {
        emit dataChanged(index(oldSize - 1), index(oldSize - 1));
    }

    // aka it is the first time when history is loaded in chat
    if (qMax(m_peer["read_inbox_max_id"].toInt(), m_peer["read_outbox_max_id"].toInt()) == oldOffset) {
        emit scrollTo(m_history.size() - 1);
    }


    if (m_avatarDownloader) {
        for (qint32 i = 0; i < users.size(); ++i) {
            m_avatarDownloader->downloadAvatar(users[i].toMap());
        }
        for (qint32 i = 0; i < chats.size(); ++i) {
            m_avatarDownloader->downloadAvatar(chats[i].toMap());
        }
        for (qint32 i = 0; i < messagesRows.size(); ++i) {
            TgObject photo = messagesRows[i]["_photoToDownload"].toMap();
            m_avatarDownloader->downloadPhoto(photo);
        }
    }
}

void MessagesModel::handleHistoryResponseUpwards(TgObject data, TgLongVariant messageId)
{
    Q_UNUSED(messageId)

    TgList messages = data["messages"].toList();
    TgList chats = data["chats"].toList();
    TgList users = data["users"].toList();

    globalUsers().append(users);
    globalChats().append(chats);

    if (messages.isEmpty()) {
        m_upOffset = -1;
        return;
    }

    QList<TgObject> messagesRows;
    messagesRows.reserve(messages.size());

    for (qint32 i = messages.size() - 1; i >= 0; --i) {
        TgObject message = messages[i].toMap();
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
            sender = m_peer;
        }

        messagesRows.append(createRow(message, sender, users, chats));
    }

    qint32 oldOffset = m_upOffset;
    qint32 newOffset = messages.last().toMap()["id"].toInt();
    if (m_upOffset != newOffset && messages.size() == BATCH_SIZE) {
        m_upOffset = newOffset;
    } else {
        m_upOffset = -1;
    }

    qint32 oldSize = m_history.size();

    beginInsertRows(QModelIndex(), 0, messagesRows.size() - 1);
    m_history = messagesRows + m_history;
    endInsertRows();

    if (oldSize > 0) {
        emit dataChanged(index(messagesRows.size()), index(messagesRows.size()));
    }

    // aka it is the first time when history is loaded in chat
    if (qMax(m_peer["read_inbox_max_id"].toInt(), m_peer["read_outbox_max_id"].toInt()) == oldOffset) {
        emit scrollTo(m_history.size() - 1);
    } else {
        emit scrollTo(messagesRows.size());
    }

    if (m_avatarDownloader) {
        for (qint32 i = 0; i < users.size(); ++i) {
            m_avatarDownloader->downloadAvatar(users[i].toMap());
        }
        for (qint32 i = 0; i < chats.size(); ++i) {
            m_avatarDownloader->downloadAvatar(chats[i].toMap());
        }
        for (qint32 i = 0; i < messagesRows.size(); ++i) {
            m_avatarDownloader->downloadPhoto(messagesRows[i]["_photoToDownload"].toMap());
        }
    }
}

TgObject MessagesModel::createRow(TgObject message, TgObject sender, TgList users, TgList chats)
{
    TgObject row;
    row["messageId"] = message["id"];

    if (TgClient::isUser(sender)) {
        row["senderName"] = QString(sender["first_name"].toString() + " " + sender["last_name"].toString());
    } else {
        row["senderName"] = sender["title"].toString();
    }

    //TODO post author

    row["thumbnailColor"] = AvatarDownloader::userColor(sender["id"].toLongLong());
    row["thumbnailText"] = AvatarDownloader::getAvatarText(row["senderName"].toString());
    row["avatar"] = "";
    row["photoId"] = sender["photo"].toMap()["photo_id"];

    row["senderName"] = QString("<html><span style=\"color: "
                                + AvatarDownloader::userColor(sender["id"]).name()
                                + "\">"
                                + row["senderName"].toString()
                                + "</span></html>");

    row["date"] = message["date"];
    row["grouped_id"] = message["grouped_id"];
    //TODO 12-hour format
    row["messageTime"] = QDateTime::fromTime_t(qMax(message["date"].toInt(), message["edit_date"].toInt())).toString("hh:mm");
    //TODO replies support
    row["messageText"] = messageToHtml(message["message"].toString(), message["entities"].toList());
    row["sender"] = TgClient::toInputPeer(sender);

    TgObject fwdFrom = message["fwd_from"].toMap();
    row["forwardedFrom"] = "";
    if (EXISTS(fwdFrom)) {
        QString forwardedFrom = fwdFrom["from_name"].toString();

        if (forwardedFrom.isEmpty()) {
            TgObject fwdPeer = fwdFrom["from_id"].toMap();

            if (TgClient::isUser(fwdPeer)) for (qint32 i = 0; i < users.size(); ++i) {
                    TgObject realPeer = users[i].toMap();
                    if (TgClient::peersEqual(fwdPeer, realPeer)) {
                        forwardedFrom = QString(realPeer["first_name"].toString() + " " + realPeer["last_name"].toString());
                        break;
                    }
                }
            if (TgClient::isChat(fwdPeer)) for (qint32 i = 0; i < chats.size(); ++i) {
                    TgObject realPeer = chats[i].toMap();
                    if (TgClient::peersEqual(fwdPeer, realPeer)) {
                        forwardedFrom = realPeer["title"].toString();
                        break;
                    }
                }
        }

        row["forwardedFrom"] = forwardedFrom;
    }

    TgObject media = message["media"].toMap();
    row["hasMedia"] = GETID(media) != 0;
    row["mediaDownloadable"] = false;
    row["mediaSpoiler"] = false;
    row["photoSpoiler"] = false;
    row["mediaUrl"] = "";

    switch (GETID(media)) {
    case MessageMediaPhoto:
    {
        row["hasMedia"] = false;
        row["photoFile"] = "";
        row["_photoToDownload"] = media["photo"].toMap();
        row["photoFileId"] = media["photo"].toMap()["id"].toLongLong();
        row["hasPhoto"] = row["photoFileId"].toLongLong() != 0;
        row["photoSpoiler"] = media["spoiler"].toBool();
        break;
    }
    case MessageMediaContact:
    {
        row["mediaImage"] = "../../img/media/account.png";
        QString contactName;

        contactName += media["first_name"].toString();
        contactName += " ";
        contactName += media["last_name"].toString();

        row["mediaTitle"] = contactName;
        row["mediaText"] = media["phone_number"].toString();
        break;
    }
    case MessageMediaUnsupported:
        row["mediaImage"] = "../../img/media/file.png";
        row["mediaTitle"] = "Unsupported media";
        row["mediaText"] = "update your app";
        break;
    case MessageMediaDocument:
    {
        row["mediaImage"] = "../../img/media/file.png";
        row["mediaDownloadable"] = true;
        row["mediaDownload"] = media;

        TgObject document = media["document"].toMap();
        QString documentName = "Unknown file";

        TgList attributes = document["attributes"].toList();
        for (qint32 i = 0; i < attributes.size(); ++i) {
            TgObject attribute = attributes[i].toMap();
            if (GETID(attribute) == DocumentAttributeFilename) {
                documentName = attribute["file_name"].toString();
                break;
            }
        }

        qint64 size = document["size"].toLongLong();
        QString sizeString;

        if (size > 1073741824) {
            sizeString = QString::number((long double) size / 1073741824L, 'f', 2);
            sizeString += " GB";
        } else if (size > 1048576) {
            sizeString = QString::number((long double) size / 1048576L, 'f', 2);
            sizeString += " MB";
        } else if (size > 1024) {
            sizeString = QString::number((long double) size / 1024L, 'f', 2);
            sizeString += " KB";
        } else {
            sizeString = QString::number((long double) size, 'f', 2);
            sizeString += " B";
        }

        row["mediaTitle"] = documentName;
        row["mediaFileName"] = documentName;
        row["mediaText"] = sizeString;
        row["mediaSpoiler"] = media["spoiler"].toBool();
        break;
    }
    case MessageMediaWebPage:
        row["mediaImage"] = "../../img/media/web.png";
        row["mediaTitle"] = "Webpage";
        row["mediaText"] = media["webpage"].toMap()["title"].toString();
        if (row["mediaText"].toString().isEmpty()) row["mediaText"] = "unknown link";
        row["mediaUrl"] = media["webpage"].toMap()["url"].toString();
        break;
    case MessageMediaVenue:
        row["mediaImage"] = "../../img/media/map-marker.png";
        row["mediaTitle"] = "Venue";
        row["mediaText"] = media["title"].toString();
        break;
    case MessageMediaGame:
        row["mediaImage"] = "../../img/media/gamepad-square.png";
        row["mediaTitle"] = "Game";
        row["mediaText"] = media["game"].toMap()["title"].toString();
        break;
    case MessageMediaInvoice:
        row["mediaImage"] = "../../img/media/receipt-text.png";
        row["mediaTitle"] = media["title"].toString();
        row["mediaText"] = media["description"].toString();
        break;
    case MessageMediaGeo:
    case MessageMediaGeoLive:
    {
        row["mediaImage"] = "../../img/media/map-marker.png";
        row["mediaTitle"] = GETID(media) == MessageMediaGeoLive ? "Live geolocation" : "Geolocation";

        TgObject geo = media["geo"].toMap();
        QString geoText;
        geoText += geo["long"].toString();
        geoText += ", ";
        geoText += geo["lat"].toString();

        row["mediaText"] = geoText;
        break;
    }
    case MessageMediaPoll:
        row["mediaImage"] = "../../img/media/poll.png";
        row["mediaTitle"] = "Poll";
        row["mediaText"] = media["poll"].toMap()["public_voters"].toBool() ? "public" : "anonymous";
        break;
    case MessageMediaDice:
        row["mediaImage"] = "../../img/media/dice-multiple.png";
        row["mediaTitle"] = "Dice";
        row["mediaText"] = media["value"].toString();
        break;
    }

    //TODO special bubble for service messages
    handleMessageAction(row, message, sender, users, chats);

    return row;
}

void MessagesModel::linkActivated(QString link, qint32 listIndex)
{
    QMutexLocker lock(&m_mutex);

    QUrl url(link);
    Q_UNUSED(link); //TODO

    TgObject listItem = m_history[listIndex];
    QDomDocument dom;

    if (!dom.setContent(listItem["messageText"].toString(), false)) {
        return;
    }

    QDomNodeList list = dom.elementsByTagName("a");
    for (qint32 i = 0; i < list.count(); ++i) {
        QDomNode parent = list.at(i).toElement();

        if (parent.toElement().attribute("href") != link) {
            continue;
        }

        while (!parent.isNull()) {
            if (parent.toElement().attribute("href").startsWith("kutegram://spoiler/")) {
                parent.toElement().removeAttribute("href");

                QList<QDomNode> list;
                list << parent;
                while (!list.isEmpty()) {
                    QDomNode parent = list.takeLast();
                    QDomNodeList nodeList = parent.childNodes();
                    for (qint32 j = 0; j < nodeList.count(); ++j)
                        list << nodeList.at(j);
                    parent.toElement().removeAttribute("color");
                }

                listItem["messageText"] = dom.toString(-1);
                m_history[listIndex] = listItem;

                emit dataChanged(index(listIndex), index(listIndex));
                return;
            }

            parent = parent.parentNode();
        }
    }

    qDebug() << "OPEN URL:" << link;
}

void MessagesModel::avatarDownloaded(TgLongVariant photoId, QString filePath)
{
    QMutexLocker lock(&m_mutex);

    for (qint32 i = 0; i < m_history.size(); ++i) {
        TgObject message = m_history[i];

        if (message["photoId"] != photoId) {
            continue;
        }

        message["avatar"] = filePath;
        m_history[i] = message;

        emit dataChanged(index(i), index(i));
    }
}

void MessagesModel::photoDownloaded(TgLongVariant photoId, QString filePath)
{
    QMutexLocker lock(&m_mutex);

    for (qint32 i = 0; i < m_history.size(); ++i) {
        TgObject message = m_history[i];

        if (message["photoFileId"] != photoId) {
            continue;
        }

        message["photoFile"] = filePath;
        m_history[i] = message;

        emit dataChanged(index(i), index(i));
    }
}

void MessagesModel::downloadFile(qint32 index)
{
    QMutexLocker lock(&m_mutex);

    if (!m_client || !m_client->isAuthorized() || TgClient::commonPeerType(m_peer) == 0 || index == -1) {
        return;
    }

    cancelDownload(index);

    QDir::home().mkdir("Kutegram");

    QString fileName = m_history[index]["mediaFileName"].toString();
    if (fileName.isEmpty()) fileName = QString::number(QDateTime::currentDateTime().toMSecsSinceEpoch());

    QStringList split = fileName.split('.');
    QString fileNameBefore;
    QString fileNameAfter;

    if (split.length() == 1) {
        fileNameBefore = split.first();
    } else {
        fileNameBefore = QStringList(split.mid(0, split.length() - 1)).join(".");
        fileNameAfter = split.last();
    }

    if (!fileNameAfter.isEmpty()) {
        fileNameAfter = "." + fileNameAfter;
    }

#if QT_VERSION < 0x050000
    QDir dir(QDir::homePath() + "/Downloads/");
#else
    QDir dir(QStandardPaths::writableLocation(QStandardPaths::DownloadLocation));
#endif
    dir.mkpath("Kutegram/");

    QString indexedFileName = fileName;
    QString indexedFilePath = dir.absoluteFilePath("Kutegram/" + indexedFileName);
    qint32 fileIndex = 0;

    while (QFile(indexedFilePath).exists()) {
        ++fileIndex;
        indexedFileName = fileNameBefore + " (" + QString::number(fileIndex) + ")" + fileNameAfter;
        indexedFilePath = dir.absoluteFilePath("Kutegram/" + indexedFileName);
    }

    qint32 messageId = m_history[index]["messageId"].toInt();
    qint64 requestId = m_client->downloadFile(indexedFilePath, m_history[index]["mediaDownload"].toMap()).toLongLong();
    m_downloadRequests.insert(requestId, messageId);
    emit downloadUpdated(messageId, 0, "");
}

void MessagesModel::cancelDownload(qint32 index)
{
    QMutexLocker lock(&m_mutex);

    if (!m_client) {
        return;
    }

    qint32 messageId = m_history[index]["messageId"].toInt();
    qint64 requestId = m_downloadRequests.key(messageId);
    m_downloadRequests.remove(requestId);
    m_client->cancelDownload(requestId);
    emit downloadUpdated(messageId, -1, "");
}

void MessagesModel::fileDownloaded(TgLongVariant fileId, QString filePath)
{
    QMutexLocker lock(&m_mutex);

    TgVariant messageId = m_downloadRequests.take(fileId.toLongLong());

    if (messageId.isNull()) {
        return;
    }

    emit downloadUpdated(messageId.toInt(), 1, "file:///" + filePath);
}

void MessagesModel::fileDownloadCanceled(TgLongVariant fileId, QString filePath)
{
    Q_UNUSED(filePath)
    QMutexLocker lock(&m_mutex);

    TgVariant messageId = m_downloadRequests.take(fileId.toLongLong());

    if (messageId.isNull()) {
        return;
    }

    emit downloadUpdated(messageId.toInt(), -1, "");
}

void MessagesModel::gotMessageUpdate(TgObject update, TgLongVariant messageId)
{
    QMutexLocker lock(&m_mutex);

    if (m_downOffset != -1) {
        return;
    }

    TgObject peerId;
    TgObject fromId;
    qint64 fromIdNumeric;

    //TODO this should be handled by singleton object with data from DB
    if (ID(update) == TLType::UpdateShortSentMessage) { //TODO what if we don't know about message?
        if (!m_sentMessages.contains(messageId.toLongLong())) {
            return;
        }

        if (TgClient::isChannel(m_peer)) {
            ID_PROPERTY(peerId) = TLType::PeerChannel;
            peerId["channel_id"] = TgClient::getPeerId(m_peer);
        } else if (TgClient::isChat(m_peer)) {
            ID_PROPERTY(peerId) = TLType::PeerChat;
            peerId["chat_id"] = TgClient::getPeerId(m_peer);
        } else {
            ID_PROPERTY(peerId) = TLType::PeerUser;
            peerId["user_id"] = TgClient::getPeerId(m_peer);
        }

        fromIdNumeric = m_client->getUserId().toLongLong();

        update["message"] = m_sentMessages.take(messageId.toLongLong());

        update["peer_id"] = peerId;
        emit sentMessageUpdate(update, messageId);
    } else if (update["user_id"].toLongLong() == TgClient::getPeerId(m_peer) && TgClient::isUser(m_peer)) {
        ID_PROPERTY(peerId) = TLType::PeerUser;
        peerId["user_id"] = update["user_id"];

        fromIdNumeric = update["out"].toBool() ? m_client->getUserId().toLongLong() : update["user_id"].toLongLong();
    } else if (update["chat_id"].toLongLong() == TgClient::getPeerId(m_peer) && TgClient::isChat(m_peer)) {
        ID_PROPERTY(peerId) = TLType::PeerChat;
        peerId["chat_id"] = update["chat_id"];

        fromIdNumeric = update["from_id"].toLongLong();
    } else {
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

    qint32 oldSize = m_history.size();

    beginInsertRows(QModelIndex(), m_history.size(), m_history.size());
    TgObject messageRow = createRow(update, sender, globalUsers(), globalChats());
    m_history.append(messageRow);
    endInsertRows();

    if (oldSize > 0) {
        emit dataChanged(index(oldSize - 1), index(oldSize - 1));
    }

    if (m_avatarDownloader) {
        m_avatarDownloader->downloadAvatar(sender);
        m_avatarDownloader->downloadPhoto(messageRow["_photoToDownload"].toMap());
    }

    emit scrollForNew();
    qDebug() << Q_FUNC_INFO;
}

void MessagesModel::gotUpdate(TgObject update, TgLongVariant messageId, TgList users, TgList chats, qint32 date, qint32 seq, qint32 seqStart)
{
    Q_UNUSED(messageId);
    Q_UNUSED(date);
    Q_UNUSED(seq);
    Q_UNUSED(seqStart);

    QMutexLocker lock(&m_mutex);

    //We should avoid duplicates. (implement DB)
    //    _globalUsers.append(users);
    //    _globalChats.append(chats);

    switch (ID(update)) {
    case TLType::UpdateNewMessage:
    case TLType::UpdateNewChannelMessage:
    {
        if (m_downOffset != -1) {
            return;
        }

        TgObject message = update["message"].toMap();

        if (!TgClient::peersEqual(m_peer, message["peer_id"].toMap())) {
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
            sender = m_peer;
        }

        message["out"] = TgClient::getPeerId(sender) == m_client->getUserId();

        qint32 oldSize = m_history.size();

        beginInsertRows(QModelIndex(), m_history.size(), m_history.size());
        TgObject messageRow = createRow(message, sender, users, chats);
        m_history.append(messageRow);
        endInsertRows();

        if (oldSize > 0) {
            emit dataChanged(index(oldSize - 1), index(oldSize - 1));
        }

        if (m_avatarDownloader) {
            m_avatarDownloader->downloadAvatar(sender);
            m_avatarDownloader->downloadPhoto(messageRow["_photoToDownload"].toMap());
        }

        emit scrollForNew();
        break;
    }
    case TLType::UpdateEditMessage:
    case TLType::UpdateEditChannelMessage:
    {
        TgObject message = update["message"].toMap();

        if (!TgClient::peersEqual(m_peer, message["peer_id"].toMap())) {
            return;
        }

        qint32 rowIndex = -1;
        for (qint32 i = 0; i < m_history.size(); ++i) {
            if (m_history[i]["messageId"] == message["id"]) {
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
            sender = m_peer;
        }

        message["out"] = TgClient::getPeerId(sender) == m_client->getUserId();

        TgObject messageRow = createRow(message, sender, users, chats);
        m_history.replace(rowIndex, messageRow);

        emit dataChanged(index(rowIndex), index(rowIndex));

        if (m_avatarDownloader) {
            m_avatarDownloader->downloadAvatar(sender);
            m_avatarDownloader->downloadPhoto(messageRow["_photoToDownload"].toMap());
        }
        break;
    }
    case TLType::UpdateDeleteChannelMessages:
        if (!TgClient::isChat(m_peer) || TgClient::getPeerId(m_peer) != update["channel_id"].toLongLong()) {
            return;
        }

        //fallthrough
    case TLType::UpdateDeleteMessages:
        TgList ids = update["messages"].toList();
        for (qint32 i = 0; i < m_history.size(); ++i) {
            if (ids.removeOne(m_history[i]["messageId"])) {
                beginRemoveRows(QModelIndex(), i, i);
                m_history.removeAt(i);
                endRemoveRows();
                --i;
            }
        }

        break;
    }
}

void MessagesModel::sendMessage(QString message)
{
    if (!m_client || !m_client->isAuthorized() || TgClient::commonPeerType(m_inputPeer) == 0 || m_uploadId.toLongLong() || (message.isEmpty() && GETID(m_media["file"].toMap()) == 0)) {
        return;
    }

    m_sentMessages.insert(m_client->messagesSendMessage(m_inputPeer, message, m_media).toLongLong(), message);
    cancelUpload();
}

void MessagesModel::uploadFile()
{
    if(!m_client) {
        return;
    }

    cancelUpload();

//TODO: fixme
    qWarning() << Q_FUNC_INFO << "NOT READY!";
    QStringList selectedFiles = QStringList();
    if (selectedFiles.isEmpty()) {
        return;
    }

    QString selected = selectedFiles.first();
    if (selected.isEmpty()) {
        return;
    }

    if (selected.endsWith(".jpg") || selected.endsWith(".jpeg") || selected.endsWith(".png")) {
        ID_PROPERTY(m_media) = TLType::InputMediaUploadedPhoto;
    } else {
        ID_PROPERTY(m_media) = TLType::InputMediaUploadedDocument;

        TGOBJECT(TLType::DocumentAttributeFilename, fileName);
        fileName["file_name"] = selected.split('/').last();

        TgList attributes;
        attributes << fileName;
        m_media["attributes"] = attributes;
    }

    m_uploadId = m_client->uploadFile(selected);

    emit uploadingProgress(0);
}

void MessagesModel::cancelUpload()
{
    if(!m_client) {
        return;
    }

    m_media = TgObject();
    if (m_uploadId.toLongLong()) {
        m_client->cancelUpload(m_uploadId);
    }
    m_uploadId = 0;

    emit uploadingProgress(-1);
}

void MessagesModel::fileUploadCanceled(TgLongVariant fileId)
{
    if (m_uploadId != fileId) {
        return;
    }

    m_uploadId = 0;
    cancelUpload();
}

void MessagesModel::fileUploaded(TgLongVariant fileId, TgObject inputFile)
{
    if (m_uploadId != fileId) {
        return;
    }

    m_uploadId = 0;
    m_media["file"] = inputFile;

    emit uploadingProgress(100);
}

void MessagesModel::fileUploading(TgLongVariant fileId, TgLongVariant processedLength, TgLongVariant totalLength, qint32 progressPercentage)
{
    Q_UNUSED(fileId);
    Q_UNUSED(processedLength);
    Q_UNUSED(totalLength);

    if (m_uploadId != fileId) {
        return;
    }

    emit uploadingProgress(progressPercentage);
}
