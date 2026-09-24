/*
  SPDX-FileCopyrightText: 2012 Jekyll Wu <adaptee@gmail.com>

  SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#ifndef WINDOWSYSTEMINFO_H
#define WINDOWSYSTEMINFO_H

#include "koncrepeprivate_export.h"

namespace Koncrepe
{
class KONCREPEPRIVATE_EXPORT WindowSystemInfo
{
public:
    static bool HAVE_TRANSPARENCY;

    static bool compositingActive();
};
}

#endif // WINDOWSYSTEMINFO_H
