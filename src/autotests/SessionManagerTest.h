/*
    SPDX-FileCopyrightText: 2008 Robert Knight <robertknight@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef SESSIONMANAGERTEST_H
#define SESSIONMANAGERTEST_H

namespace Koncrepe
{
class SessionManagerTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init();
    void cleanup();

    void testWarnNotImplemented();
};

}

#endif // SESSIONMANAGERTEST_H
