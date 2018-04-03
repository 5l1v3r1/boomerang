#pragma region License
/*
 * This file is part of the Boomerang Decompiler.
 *
 * See the file "LICENSE.TERMS" for information on usage and
 * redistribution of this file, and for a DISCLAIMER OF ALL
 * WARRANTIES.
 */
#pragma endregion License
#include "Localiser.h"


#include "boomerang/db/exp/RefExp.h"
#include "boomerang/db/exp/Location.h"
#include "boomerang/db/exp/Terminal.h"
#include "boomerang/db/statements/CallStatement.h"


Localiser::Localiser(CallStatement* call)
    : m_call(call)
{
}


SharedExp Localiser::preModify(const std::shared_ptr<RefExp>& exp, bool& visitChildren)
{
    visitChildren    = false; // Don't recurse into already subscripted variables
    m_mask <<= 1;
    return exp;
}


SharedExp Localiser::preModify(const std::shared_ptr<Location>& exp, bool& visitChildren)
{
    visitChildren    = true;
    m_mask <<= 1;
    return exp;
}


SharedExp Localiser::postModify(const std::shared_ptr<Location>& exp)
{
    SharedExp ret = exp;

    if (!(m_unchanged & m_mask)) {
        ret = exp->simplify();
    }

    m_mask >>= 1;
    SharedExp r = m_call->findDefFor(ret);

    if (r) {
        ret = r->clone();

        ret          = ret->bypass();
        m_unchanged &= ~m_mask;
        m_mod        = true;
    }
    else {
        ret = RefExp::get(ret, nullptr); // No definition reaches, so subscript with {-}
    }

    return ret;
}


SharedExp Localiser::postModify(const std::shared_ptr<Terminal>& exp)
{
    SharedExp ret = exp;

    if (!(m_unchanged & m_mask)) {
        ret = exp->simplify();
    }

    m_mask >>= 1;
    SharedExp r = m_call->findDefFor(ret);

    if (r) {
        ret          = r->clone()->bypass();
        m_unchanged &= ~m_mask;
        m_mod        = true;
    }
    else {
        ret = RefExp::get(ret, nullptr); // No definition reaches, so subscript with {-}
    }

    return ret;
}
