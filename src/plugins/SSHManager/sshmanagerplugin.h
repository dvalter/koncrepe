/*  This file was part of the KDE libraries

    SPDX-FileCopyrightText: 2021 Tomaz Canabrava <tcanabrava@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef SSHMANAGERPLUGIN_H
#define SSHMANAGERPLUGIN_H

#include <pluginsystem/IKoncrepePlugin.h>

#include <memory>

namespace Koncrepe
{
class SessionController;
class MainWindow;
}

class QSortFilterProxyModel;
class QStandardItemModel;

struct SSHManagerPluginPrivate;

class SSHManagerPlugin : public Koncrepe::IKoncrepePlugin
{
    Q_OBJECT
public:
    SSHManagerPlugin(QObject *object, const QVariantList &args);
    ~SSHManagerPlugin() override;

    void createWidgetsForMainWindow(Koncrepe::MainWindow *mainWindow) override;
    void activeViewChanged(Koncrepe::SessionController *controller, Koncrepe::MainWindow *mainWindow) override;
    QList<QAction *> menuBarActions(Koncrepe::MainWindow *mainWindow) const override;

    static void
    requestConnection(QSortFilterProxyModel *filterModel, QStandardItemModel *model, Koncrepe::SessionController *controller, const QModelIndex &idx);

private:
    std::unique_ptr<SSHManagerPluginPrivate> d;
};

#endif
