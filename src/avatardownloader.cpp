#include "avatardownloader.h"

#include <QImage>
#include <QPainter>
#include <QBrush>
#include <QCoreApplication>

AvatarDownloader::AvatarDownloader(QObject *parent)
    : QObject(parent)
    , _mutex(QMutex::Recursive)
    , _client(0)
    , _userId(0)
    , _requestsAvatars()
    , _requestsPhotos()
    , _downloadedAvatars()
    , _downloadedPhotos()
{
}

void AvatarDownloader::saveDatabase()
{
    if (!_client) {
        return;
    }

    QSettings settings(QSettings::IniFormat, QSettings::UserScope, QCoreApplication::organizationName(), QCoreApplication::applicationName() + "_cache");
    settings.setValue("DownloadedAvatars", _downloadedAvatars);
    settings.setValue("DownloadedPhotos", _downloadedPhotos);
}

void AvatarDownloader::readDatabase()
{
    if (!_client) {
        return;
    }

    QSettings settings(QSettings::IniFormat, QSettings::UserScope, QCoreApplication::organizationName(), QCoreApplication::applicationName() + "_cache");
    _downloadedAvatars = settings.value("DownloadedAvatars").toList();
    _downloadedPhotos = settings.value("DownloadedPhotos").toList();
}

void AvatarDownloader::setClient(QObject *client)
{
    QMutexLocker lock(&_mutex);

    if (_client) {
        _client->disconnect(this);
        saveDatabase();
    }

    _client = dynamic_cast<TgClient*>(client);
    _userId = _client->getUserId();

    _requestsAvatars.clear();
    _requestsPhotos.clear();
    readDatabase();

    if (!_client) return;

    _client->sessionDirectory().mkdir("Kutegram_avatars");
    _client->sessionDirectory().mkdir("Kutegram_photos");

    connect(_client, SIGNAL(authorized(TgLongVariant)), this, SLOT(authorized(TgLongVariant)));
    connect(_client, SIGNAL(fileDownloaded(TgLongVariant,QString)), this, SLOT(fileDownloaded(TgLongVariant,QString)));
    connect(_client, SIGNAL(fileDownloadCanceled(TgLongVariant,QString)), this, SLOT(fileDownloadCanceled(TgLongVariant,QString)));
}

void AvatarDownloader::authorized(TgLongVariant userId)
{
    QMutexLocker lock(&_mutex);

    if (_userId != userId) {
        _requestsAvatars.clear();
        _requestsPhotos.clear();
        _userId = userId;
    }
}

QObject* AvatarDownloader::client() const
{
    return _client;
}

qint64 AvatarDownloader::downloadPhoto(TgObject photo)
{
    QMutexLocker lock(&_mutex);

    if (!_client || !_client->isAuthorized() || GETID(photo) == 0) {
        return 0;
    }

    qint64 photoId = photo["id"].toLongLong();

    QString relativePath = "Kutegram_photos/" + QString::number(photoId) + ".jpg";
    QString avatarFilePath = _client->sessionDirectory().absoluteFilePath(relativePath);

    if (!_downloadedPhotos.contains(photoId)) {
        qint64 loadingId = _client->downloadFile(avatarFilePath, photo).toLongLong();
        _requestsPhotos[loadingId] = photoId;
    } else {
#if QT_VERSION >= 0x050000
        emit photoDownloaded(photoId, "file:///" + avatarFilePath);
#else
        emit photoDownloaded(photoId, avatarFilePath);
#endif
    }

    return photoId;
}

qint64 AvatarDownloader::downloadAvatar(TgObject peer)
{
    QMutexLocker lock(&_mutex);

    if (!_client || !_client->isAuthorized() || TgClient::commonPeerType(peer) == 0) {
        return 0;
    }

    TgObject photo = peer["photo"].toMap();
    if (GETID(photo) == 0) {
        return 0;
    }

    qint64 photoId = photo["photo_id"].toLongLong();

    QString relativePath = "Kutegram_avatars/" + QString::number(photoId) + ".jpg";
    QString avatarFilePath = _client->sessionDirectory().absoluteFilePath(relativePath);

    if (!_downloadedAvatars.contains(photoId)) {
        qint64 loadingId = _client->downloadFile(avatarFilePath, peer).toLongLong();
        _requestsAvatars[loadingId] = photoId;
    } else {
#if QT_VERSION >= 0x050000
        emit avatarDownloaded(photoId, "file:///" + avatarFilePath + ".png");
#else
        emit avatarDownloaded(photoId, avatarFilePath + ".png");
#endif
    }

    return photoId;
}

void AvatarDownloader::fileDownloaded(TgLongVariant fileId, QString filePath)
{
    QMutexLocker lock(&_mutex);

    TgLongVariant photoId = _requestsAvatars.take(fileId.toLongLong());
    if (!photoId.isNull()) {
        QFile file(filePath);
        if (!file.open(QFile::ReadOnly)) {
            return;
        }

        QImage roundedImage(160, 160, QImage::Format_ARGB32);
        roundedImage.fill(Qt::transparent);
        QPainter painter(&roundedImage);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setBrush(QBrush(QImage::fromData(file.readAll())));
        painter.setPen(Qt::transparent);
        file.close();
        painter.drawRoundedRect(0, 0, 160, 160, 80, 80);
        filePath += ".png";
        if (!roundedImage.save(filePath)) {
            return;
        }

        _downloadedAvatars.append(photoId);
        saveDatabase();
#if QT_VERSION >= 0x050000
        emit avatarDownloaded(photoId, "file:///" + filePath);
#else
        emit avatarDownloaded(photoId, filePath);
#endif
        return;
    }

    photoId = _requestsPhotos.take(fileId.toLongLong());
    if (!photoId.isNull()) {
        QFile file(filePath);
        if (!file.open(QFile::ReadOnly)) {
            return;
        }

        QImage scaledImage = QImage::fromData(file.readAll());
        if (scaledImage.height() > scaledImage.width()) {
            scaledImage = scaledImage.scaledToHeight(280, Qt::SmoothTransformation);
        } else {
            scaledImage = scaledImage.scaledToWidth(280, Qt::SmoothTransformation);
        }
        file.close();
        if (!scaledImage.save(filePath + ".thumbnail.jpg")) {
            return;
        }

        _downloadedPhotos.append(photoId);
        saveDatabase();
        emit photoDownloaded(photoId, "file:///" + filePath);
        return;
    }
}

void AvatarDownloader::fileDownloadCanceled(TgLongVariant fileId, QString filePath)
{
    Q_UNUSED(filePath);
    QMutexLocker lock(&_mutex);

    _requestsAvatars.remove(fileId.toLongLong());
    _requestsPhotos.remove(fileId.toLongLong());
}

QString AvatarDownloader::getAvatarText(QString title)
{
    QStringList split = title.split(" ", QString::SkipEmptyParts);
    QString result;

    for (qint32 i = 0; i < split.size(); ++i) {
        QString item = split[i];
        for (qint32 j = 0; j < item.length(); ++j) {
            if (item[j].isLetterOrNumber()) {
                result += item[j].toUpper();
                break;
            }
        }

        if (result.size() > 1) {
            break;
        }
    }

    if (result.isEmpty() && !title.isEmpty())
        result += title[0].toUpper();

    return result;
}

QColor AvatarDownloader::userColor(TgLongVariant id)
{
    return QColor::fromHsl(id.toLongLong() % 360, 160, 120);
}
