// This file was part of the KDE libraries
// SPDX-FileCopyrightText: 2022 Tao Guo <guotao945@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef QUICKCOMMANDSPLUGIN_H
#define QUICKCOMMANDSPLUGIN_H

#include <pluginsystem/IKoncrepePlugin.h>

namespace Koncrepe
{
class SessionController;
class MainWindow;
}

class QuickCommandsPlugin : public Koncrepe::IKoncrepePlugin
{
    Q_OBJECT

public:
    QuickCommandsPlugin(QObject *object, const QVariantList &args);
    ~QuickCommandsPlugin() override;

    void createWidgetsForMainWindow(Koncrepe::MainWindow *mainWindow) override;
    void activeViewChanged(Koncrepe::SessionController *controller, Koncrepe::MainWindow *mainWindow) override;
    QList<QAction *> menuBarActions(Koncrepe::MainWindow *mainWindow) const override;

private:
    struct Private;
    std::unique_ptr<Private> priv;
};

#endif // QUICKCOMMANDSPLUGIN_H
