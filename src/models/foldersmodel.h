#ifndef FOLDERSMODEL_H
#define FOLDERSMODEL_H

#include <QAbstractListModel>
#include <QVariant>
#include <QMutex>
#include "tgclient.h"

class FoldersModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(QObject* client READ client WRITE setClient)

public:
    explicit FoldersModel(QObject *parent = 0);
    virtual ~FoldersModel();
    void resetState();

    QHash<int, QByteArray> roleNames() const;

    void setClient(QObject *client);
    QObject* client() const;

    int rowCount(const QModelIndex& parent = QModelIndex()) const;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const;

    TgObject createRow(TgObject filter);
    QList<TgObject> folders();

signals:
    void foldersChanged(QList<TgObject> folders);

public slots:
    void authorized(TgLongVariant userId);
    void messagesGetDialogFiltersResponse(TgVector data, TgLongVariant messageId);

    void refresh();

    bool canFetchMoreDownwards() const;
    void fetchMoreDownwards();

    static bool matchesFilter(TgObject filter, TgObject peer);

private:
    QMutex m_mutex;
    QList<TgObject> m_folders;

    TgClient* m_client;
    TgLongVariant m_userId;

    TgLongVariant m_requestId;

    enum FolderRoles {
        TitleRole = Qt::UserRole + 1,
        IconRole,
        FolderIndexRole
    };

};

#endif // FOLDERSMODEL_H
