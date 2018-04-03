#pragma region License
/*
 * This file is part of the Boomerang Decompiler.
 *
 * See the file "LICENSE.TERMS" for information on usage and
 * redistribution of this file, and for a DISCLAIMER OF ALL
 * WARRANTIES.
 */
#pragma endregion License
#pragma once


#include "boomerang/db/visitor/expmodifier/ExpModifier.h"


class SizeStripper : public ExpModifier
{
public:
    SizeStripper() = default;
    virtual ~SizeStripper() = default;

public:
    /// \copydoc ExpModifier::preVisit
    SharedExp preModify(const std::shared_ptr<Binary>& exp, bool& visitChildren) override;
};
