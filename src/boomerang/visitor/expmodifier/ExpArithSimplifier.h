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


#include "boomerang/visitor/expmodifier/ExpModifier.h"

#include <list>


class ExpArithSimplifier : public ExpModifier
{
public:
    /// \copydoc ExpModifier::preModify
    SharedExp preModify(const std::shared_ptr<Unary> & exp, bool& visitChildren) override;

    /// \copydoc ExpModifier::postModify
    SharedExp postModify(const std::shared_ptr<Binary> & exp) override;

private:
    /// Remove pairs of expressions that appear both in \p left and \p right
    void cancelDuplicates(std::list<SharedExp>& left, std::list<SharedExp>& right);
};
