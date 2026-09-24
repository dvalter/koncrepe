/*  This file was part of the KDE libraries

    SPDX-FileCopyrightText: 2021 Tomaz Canabrava <tcanabrava@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "IKoncrepePlugin.h"

#include "MainWindow.h"
#include <QMainWindow>

namespace Koncrepe
{
struct IKoncrepePlugin::Private {
    QString name;
    QList<QMainWindow *> mainWindowList;
};

IKoncrepePlugin::IKoncrepePlugin(QObject *parent, const QVariantList &args)
    : d(std::make_unique<IKoncrepePlugin::Private>())
{
    Q_UNUSED(parent)
    Q_UNUSED(args)
}

IKoncrepePlugin::~IKoncrepePlugin() = default;

void IKoncrepePlugin::setName(const QString &name)
{
    d->name = name;
}

QString IKoncrepePlugin::name() const
{
    return d->name;
}

void IKoncrepePlugin::addMainWindow(Koncrepe::MainWindow *mainWindow)
{
    d->mainWindowList.append(mainWindow);
    createWidgetsForMainWindow(mainWindow);
}

void IKoncrepePlugin::removeMainWindow(Koncrepe::MainWindow *mainWindow)
{
    d->mainWindowList.removeOne(mainWindow);
}

}

#include "moc_IKoncrepePlugin.cpp"
