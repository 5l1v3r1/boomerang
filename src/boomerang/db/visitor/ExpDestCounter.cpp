#pragma region License
/*
 * This file is part of the Boomerang Decompiler.
 *
 * See the file "LICENSE.TERMS" for information on usage and
 * redistribution of this file, and for a DISCLAIMER OF ALL
 * WARRANTIES.
 */
#pragma endregion License
#include "ExpDestCounter.h"


#include "boomerang/db/exp/RefExp.h"
#include "boomerang/db/statements/Statement.h"


ExpDestCounter::ExpDestCounter(ExpDestCounter::ExpCountMap& dc)
    : m_destCounts(dc)
{
}

bool ExpDestCounter::visit(const std::shared_ptr<RefExp>& exp, bool& visitChildren)
{
    if (Statement::canPropagateToExp(*exp)) {
        m_destCounts[exp->clone()]++;
    }

    visitChildren = true;
    return true;
}
