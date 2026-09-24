/*
 SPDX-FileCopyrightText: 2017 Kurt Hindenburg <kurt.hindenburg@gmail.com>

 SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

// application header
#include "demo_koncrepepart.h"

// KF headers
#include <KAboutData>
#include <KLocalizedString>

// Qt headers
#include <QApplication>
#include <QCommandLineParser>

int main(int argc, char **argv)
{
    auto app = new QApplication(argc, argv);

    KLocalizedString::setApplicationDomain("demo_koncrepepart");

    KAboutData about(QStringLiteral("demo_koncrepepart"),
                     i18nc("@title", "Demo KoncrepePart"),
                     QStringLiteral("1.0"),
                     i18nc("@title", "Terminal emulator"),
                     KAboutLicense::GPL_V2,
                     i18nc("@info:credit", "(c) 2017, The Koncrepe Developers"),
                     QString(),
                     QStringLiteral("https://konsole.kde.org/"));

    KAboutData::setApplicationData(about);

    QSharedPointer<QCommandLineParser> parser(new QCommandLineParser);
    parser->setApplicationDescription(about.shortDescription());
    about.setupCommandLine(parser.data());

    QStringList args = app->arguments();

    parser->process(args);
    about.processCommandLine(parser.data());

    auto demo = new demo_koncrepepart();
    demo->show();

    int ret = app->exec();
    delete app;
    return ret;
}
