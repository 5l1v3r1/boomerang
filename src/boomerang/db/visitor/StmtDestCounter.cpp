#pragma region License
/*
 * This file is part of the Boomerang Decompiler.
 *
 * See the file "LICENSE.TERMS" for information on usage and
 * redistribution of this file, and for a DISCLAIMER OF ALL
 * WARRANTIES.
 */
#pragma endregion License
#include "StmtDestCounter.h"


#include "boomerang/db/statements/PhiAssign.h"
#include "boomerang/db/visitor/ExpDestCounter.h"


StmtDestCounter::StmtDestCounter(ExpDestCounter* edc)
    : StmtExpVisitor(edc)
{
}


bool StmtDestCounter::visit(PhiAssign * /*stmt*/, bool& dontVisitChildren)
{
    dontVisitChildren = false;
    return true;
}
