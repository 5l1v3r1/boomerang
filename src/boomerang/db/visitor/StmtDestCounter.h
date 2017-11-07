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


#include "boomerang/db/visitor/StmtExpVisitor.h"


class ExpDestCounter;


/**
 * FIXME: do I need to count collectors?
 * All the visitors and modifiers should be refactored to conditionally visit
 * or modify collectors, or not
 */
class StmtDestCounter : public StmtExpVisitor
{
public:
    StmtDestCounter(ExpDestCounter *edc);

    /// \copydoc StmtExpVisitor::visit
    bool visit(PhiAssign *stmt, bool& visitChildren) override;
};
