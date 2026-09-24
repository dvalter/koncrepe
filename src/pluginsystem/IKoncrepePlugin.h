/*  This file was part of the KDE libraries

    SPDX-FileCopyrightText: 2021 Tomaz Canabrava <tcanabrava@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef IKONCREPEPLUGIN_H
#define IKONCREPEPLUGIN_H

#include <QList>
#include <QObject>

#include <terminalDisplay/TerminalDisplay.h>

#include <memory>

#include "koncrepeapp_export.h"

namespace Koncrepe
{
class MainWindow;

class KONCREPEAPP_EXPORT IKoncrepePlugin : public QObject
{
    Q_OBJECT
public:
    IKoncrepePlugin(QObject *parent, const QVariantList &args);
    ~IKoncrepePlugin() override;

    QString name() const;

    // Usable only from PluginManager, please don't use.
    void addMainWindow(Koncrepe::MainWindow *mainWindow);
    void removeMainWindow(Koncrepe::MainWindow *mainWindow);

    virtual void createWidgetsForMainWindow(Koncrepe::MainWindow *mainWindow) = 0;
    virtual void activeViewChanged(Koncrepe::SessionController *controller, Koncrepe::MainWindow *mainWindow) = 0;

    virtual QList<QAction *> menuBarActions(Koncrepe::MainWindow *mainWindow) const
    {
        Q_UNUSED(mainWindow)
        return {};
    }

protected:
    void setName(const QString &pluginName);

private:
    struct Private;
    std::unique_ptr<Private> d;
};

}
#endif
