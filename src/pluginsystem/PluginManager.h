/*  This file was part of the KDE libraries

    SPDX-FileCopyrightText: 2021 Tomaz Canabrava <tcanabrava@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef PLUGINMANAGER_H
#define PLUGINMANAGER_H

#include <QObject>

#include "koncrepeapp_export.h"
#include <memory>

namespace Koncrepe
{
class IKoncrepePlugin;
class MainWindow;

struct PluginManagerPrivate;

class KONCREPEAPP_EXPORT PluginManager : public QObject
{
    Q_OBJECT
public:
    PluginManager();
    ~PluginManager() override;
    void loadAllPlugins();
    void registerMainWindow(Koncrepe::MainWindow *window);

    std::vector<IKoncrepePlugin *> plugins() const;

private:
    std::unique_ptr<PluginManagerPrivate> d;
};

}
#endif
