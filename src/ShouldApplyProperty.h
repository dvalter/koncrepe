/*
    This source file is part of Koncrepe, a terminal emulator.

    SPDX-FileCopyrightText: 2006-2008 Robert Knight <robertknight@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef SHOULDAPPLYPROPERTY_H
#define SHOULDAPPLYPROPERTY_H

#include "koncrepeprivate_export.h"
#include "profile/Profile.h"

namespace Koncrepe
{
/** Utility class to simplify code in SessionManager::applyProfile(). */
class KONCREPEPRIVATE_EXPORT ShouldApplyProperty
{
public:
    ShouldApplyProperty(const Profile::Ptr &profile, bool modifiedOnly);

    bool shouldApply(Profile::Property property) const;

private:
    const Profile::Ptr _profile;
    bool _modifiedPropertiesOnly;
};
}

#endif
