/*
    SPDX-FileCopyrightText: 2007-2008 Robert Knight <robertknight@gmail.com>
    SPDX-FileCopyrightText: 2020 Tomaz Canabrava <tcanabrava@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef URLFILTER_H
#define URLFILTER_H

#include "RegExpFilter.h"
#include "koncrepeprivate_export.h"

namespace Koncrepe
{
/** A filter which matches URLs in blocks of text */

// Exported for unittests
class KONCREPEPRIVATE_EXPORT UrlFilter : public RegExpFilter
{
public:
    UrlFilter();

    QSharedPointer<HotSpot> newHotSpot(int startLine, int startColumn, int endLine, int endColumn, const QStringList &capturedTexts) override;

public:
    static const QRegularExpression FullUrlRegExp;
    static const QRegularExpression EmailAddressRegExp;

    // combined OR of FullUrlRegExp and EmailAddressRegExp
    static const QRegularExpression CompleteUrlRegExp;
};

}
#endif
