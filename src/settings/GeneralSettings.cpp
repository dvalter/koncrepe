/*
  SPDX-FileCopyrightText: 2011 Kurt Hindenburg <kurt.hindenburg@gmail.com>

  SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

// Own
#include "GeneralSettings.h"
#include "config-koncrepe.h"

#include <KMessageBox>

using namespace Koncrepe;

GeneralSettings::GeneralSettings(QWidget *aParent)
    : QWidget(aParent)
{
    setupUi(this);
#if !HAVE_DBUS
    kcfg_ShowProgressInTaskBar->hide();
#endif

    connect(enableAllMessagesButton, &QPushButton::clicked, this, &Koncrepe::GeneralSettings::slotEnableAllMessages);
}

GeneralSettings::~GeneralSettings() = default;

void GeneralSettings::slotEnableAllMessages()
{
    KMessageBox::enableAllMessages();
}

#include "moc_GeneralSettings.cpp"
