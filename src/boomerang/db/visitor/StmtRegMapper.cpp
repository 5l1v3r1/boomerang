#pragma region License
/*
 * This file is part of the Boomerang Decompiler.
 *
 * See the file "LICENSE.TERMS" for information on usage and
 * redistribution of this file, and for a DISCLAIMER OF ALL
 * WARRANTIES.
 */
#pragma endregion License
#include "StmtRegMapper.h"


#include "boomerang/db/exp/RefExp.h"
#include "boomerang/db/statements/Assign.h"
#include "boomerang/db/statements/BoolAssign.h"
#include "boomerang/db/statements/ImplicitAssign.h"
#include "boomerang/db/statements/PhiAssign.h"
#include "boomerang/db/visitor/ExpRegMapper.h"


StmtRegMapper::StmtRegMapper(ExpRegMapper* erm)
    : StmtExpVisitor(erm)
{
}


bool StmtRegMapper::common(Assignment *stmt, bool& dontVisitChildren)
{
    // In case lhs is a reg or m[reg] such that reg is otherwise unused
    SharedExp lhs = stmt->getLeft();
    auto      re  = RefExp::get(lhs, stmt);

    re->accept((ExpRegMapper *)ev);
    dontVisitChildren = false;
    return true;
}


bool StmtRegMapper::visit(Assign *stmt, bool& dontVisitChildren)
{
    return common(stmt, dontVisitChildren);
}


bool StmtRegMapper::visit(PhiAssign *stmt, bool& dontVisitChildren)
{
    return common(stmt, dontVisitChildren);
}


bool StmtRegMapper::visit(ImplicitAssign *stmt, bool& dontVisitChildren)
{
    return common(stmt, dontVisitChildren);
}


bool StmtRegMapper::visit(BoolAssign *stmt, bool& dontVisitChildren)
{
    return common(stmt, dontVisitChildren);
}
