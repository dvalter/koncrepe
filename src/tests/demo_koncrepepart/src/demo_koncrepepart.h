/*
 SPDX-FileCopyrightText: 2017 Kurt Hindenburg <kurt.hindenburg@gmail.com>

 SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#ifndef DEMO_KONCREPEPART_H
#define DEMO_KONCREPEPART_H

#include <KMainWindow>
#include <KParts/Part>
#include <KParts/ReadOnlyPart>
#include <kde_terminal_interface.h>

class demo_koncrepepart : public KMainWindow
{
    Q_OBJECT
public:
    demo_koncrepepart();

    ~demo_koncrepepart() override;

    void manageProfiles();

public Q_SLOTS:
    void quit();

private:
    Q_DISABLE_COPY(demo_koncrepepart)

    KParts::ReadOnlyPart *createPart();

    KParts::ReadOnlyPart *_terminalPart;
    TerminalInterface *_terminal;
};

#endif
