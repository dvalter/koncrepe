/*
 SPDX-FileCopyrightText: 2017 Kurt Hindenburg <kurt.hindenburg@gmail.com>

 SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#include "demo_koncrepepart.h"

#include <QApplication>
#include <QDebug>
#include <QMenu>
#include <QMenuBar>

#include <KPluginFactory>
#include <KWindowEffects>

demo_koncrepepart::demo_koncrepepart()
    : KMainWindow()
    , _terminalPart(nullptr)
    , _terminal(nullptr)
{
    // Set the WA_NativeWindow attribute to force the creation of the QWindow.
    // Without this QWidget::windowHandle() returns 0.
    // See https://phabricator.kde.org/D23108
    setAttribute(Qt::WA_NativeWindow);

    setAttribute(Qt::WA_TranslucentBackground, true);
    setAttribute(Qt::WA_NoSystemBackground, false);

    // Create terminal part and embed in into the main window
    _terminalPart = createPart();
    if (_terminalPart == nullptr) {
        return;
    }

    QMenu *fileMenu = menuBar()->addMenu(QStringLiteral("File"));
    QAction *manageProfilesAction = fileMenu->addAction(QStringLiteral("Manage Profiles..."));
    connect(manageProfilesAction, &QAction::triggered, this, &demo_koncrepepart::manageProfiles);
    QAction *quitAction = fileMenu->addAction(QStringLiteral("Quit"));
    connect(quitAction, &QAction::triggered, this, &demo_koncrepepart::quit);

    connect(_terminalPart, &KParts::ReadOnlyPart::destroyed, this, &demo_koncrepepart::quit);

    setCentralWidget(_terminalPart->widget());
    _terminal = qobject_cast<TerminalInterface *>(_terminalPart);

    // Test if blur is enabled for profile
    bool blurEnabled = false;
    bool invoked = QMetaObject::invokeMethod(_terminalPart, "isBlurEnabled", Qt::DirectConnection, Q_RETURN_ARG(bool, blurEnabled));
    if (!invoked) {
        qWarning() << "isBlurEnabled invocation failed; using default:" << blurEnabled;
    } else {
        qWarning() << "blur enabled: " << blurEnabled;
    }
    KWindowEffects::enableBlurBehind(windowHandle(), blurEnabled);
}

demo_koncrepepart::~demo_koncrepepart()
{
    if (_terminalPart != nullptr) {
        disconnect(_terminalPart, &KParts::ReadOnlyPart::destroyed, this, &demo_koncrepepart::quit);
    }
}

KParts::ReadOnlyPart *demo_koncrepepart::createPart()
{
    const KPluginMetaData metaData(QStringLiteral("kf6/parts/koncrepepart"), KPluginMetaData::AllowEmptyMetaData);
    Q_ASSERT(metaData.isValid());

    KPluginFactory::Result<KParts::ReadOnlyPart> result = KPluginFactory::instantiatePlugin<KParts::ReadOnlyPart>(metaData, this);
    Q_ASSERT(result);

    return result.plugin;
}

void demo_koncrepepart::manageProfiles()
{
    QMetaObject::invokeMethod(_terminalPart, "showManageProfilesDialog", Qt::QueuedConnection, Q_ARG(QWidget *, QApplication::activeWindow()));
}

void demo_koncrepepart::quit()
{
    close();
}

#include "moc_demo_koncrepepart.cpp"
