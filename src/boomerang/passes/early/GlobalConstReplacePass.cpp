#pragma region License
/*
 * This file is part of the Boomerang Decompiler.
 *
 * See the file "LICENSE.TERMS" for information on usage and
 * redistribution of this file, and for a DISCLAIMER OF ALL
 * WARRANTIES.
 */
#pragma endregion License
#include "GlobalConstReplacePass.h"

#include "boomerang/db/Prog.h"
#include "boomerang/db/binary/BinaryImage.h"
#include "boomerang/db/proc/UserProc.h"
#include "boomerang/ssl/exp/Const.h"
#include "boomerang/util/log/Log.h"


GlobalConstReplacePass::GlobalConstReplacePass()
    : IPass("GlobalConstReplace", PassID::GlobalConstReplace)
{
}


bool GlobalConstReplacePass::execute(UserProc *proc)
{
    StatementList stmts;
    proc->getStatements(stmts);

    const BinaryImage *image = proc->getProg()->getBinaryFile()->getImage();
    bool changed             = false;

    for (Statement *st : stmts) {
        Assign *assgn = dynamic_cast<Assign *>(st);

        if (assgn == nullptr) {
            continue;
        }

        if (!assgn->getRight()->isMemOf()) {
            continue;
        }

        if (!assgn->getRight()->getSubExp1()->isIntConst()) {
            continue;
        }

        const Address addr = assgn->getRight()->access<Const, 1>()->getAddr();
        if (proc->getProg()->isReadOnly(addr)) {
            switch (assgn->getType()->getSize()) {
            case 8: assgn->setRight(Const::get(image->readNative1(addr))); break;
            case 16: assgn->setRight(Const::get(image->readNative2(addr))); break;
            case 32: assgn->setRight(Const::get(image->readNative4(addr))); break;
            case 64: assgn->setRight(Const::get(image->readNative8(addr))); break;
            case 80: continue; // can't replace float constants just yet
            default: assert(false);
            }

            LOG_VERBOSE("Replaced global constant in assign; now %1", assgn);
            changed = true;
        }
    }

    return changed;
}
