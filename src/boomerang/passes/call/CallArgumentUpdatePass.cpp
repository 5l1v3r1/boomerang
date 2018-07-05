#pragma region License
/*
 * This file is part of the Boomerang Decompiler.
 *
 * See the file "LICENSE.TERMS" for information on usage and
 * redistribution of this file, and for a DISCLAIMER OF ALL
 * WARRANTIES.
 */
#pragma endregion License
#include "CallArgumentUpdatePass.h"


#include "boomerang/core/Boomerang.h"
#include "boomerang/core/Project.h"
#include "boomerang/db/proc/UserProc.h"
#include "boomerang/db/Prog.h"
#include "boomerang/db/statements/CallStatement.h"
#include "boomerang/util/Log.h"


CallArgumentUpdatePass::CallArgumentUpdatePass()
    : IPass("CallArgumentUpdate", PassID::CallArgumentUpdate)
{
}


bool CallArgumentUpdatePass::execute(UserProc *proc)
{
    Boomerang::get()->alertDecompiling(proc);
    const bool experimental = proc->getProg()->getProject()->getSettings()->experimental;

    for (BasicBlock *bb : *proc->getCFG()) {
        BasicBlock::RTLRIterator        rrit;
        StatementList::reverse_iterator srit;
        CallStatement *c = dynamic_cast<CallStatement *>(bb->getLastStmt(rrit, srit));

        // Note: we may have removed some statements, so there may no longer be a last statement!
        if (c == nullptr) {
            continue;
        }

        c->updateArguments(experimental);
        // c->bypass();
        LOG_VERBOSE2("Updated call statement to %1", c);
    }

    return true;
}
