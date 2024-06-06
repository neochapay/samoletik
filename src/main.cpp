/*
 * Copyright (C) 2024 Chupligin Sergey <neochapay@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifdef QT_QML_DEBUG
#include <QtQuick>
#endif

#include <QGuiApplication>
#include <QQmlContext>
#include <QQuickView>
#include <QScopedPointer>

#include <sailfishapp.h>
#include <tgclient.h>

#include "avatardownloader.h"
#include "models/dialogsmodel.h"
#include "models/foldersmodel.h"
#include "models/messagesmodel.h"

int main(int argc, char* argv[])
{
    QScopedPointer<QGuiApplication> application(SailfishApp::application(argc, argv));
    application->setOrganizationName(QStringLiteral("ru.neochapay"));
    application->setApplicationName(QStringLiteral("samoletik"));

    TgClient::registerQML();
    qmlRegisterType<AvatarDownloader>("ru.neochapay.samoletik", 1, 0, "AvatarDownloader");
    qmlRegisterType<DialogsModel>("ru.neochapay.samoletik", 1, 0, "DialogsModel");
    qmlRegisterType<FoldersModel>("ru.neochapay.samoletik", 1, 0, "FoldersModel");
    qmlRegisterType<MessagesModel>("ru.neochapay.samoletik", 1, 0, "MessagesModel");

    QScopedPointer<QQuickView> view(SailfishApp::createView());

    view->setSource(SailfishApp::pathTo("qml/Samoletik.qml"));
    view->show();

    return application->exec();
}
