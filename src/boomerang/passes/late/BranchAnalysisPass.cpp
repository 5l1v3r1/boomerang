#pragma region License
/*
 * This file is part of the Boomerang Decompiler.
 *
 * See the file "LICENSE.TERMS" for information on usage and
 * redistribution of this file, and for a DISCLAIMER OF ALL
 * WARRANTIES.
 */
#pragma endregion License
#include "BranchAnalysisPass.h"


#include "boomerang/db/exp/Binary.h"
#include "boomerang/db/proc/UserProc.h"
#include "boomerang/db/statements/BranchStatement.h"
#include "boomerang/db/statements/PhiAssign.h"


BranchAnalysisPass::BranchAnalysisPass()
    : IPass("BranchAnalysis", PassID::BranchAnalysis)
{
}


bool BranchAnalysisPass::execute(UserProc *proc)
{
    bool removedBBs = doBranchAnalysis(proc);
    fixUglyBranches(proc);
    return removedBBs;
}


bool BranchAnalysisPass::doBranchAnalysis(UserProc *proc)
{
    StatementList stmts;
    proc->getStatements(stmts);

    std::set<BasicBlock *> bbsToRemove;

    for (Statement *stmt : stmts) {
        if (!stmt->isBranch()) {
            continue;
        }

        BranchStatement *firstBranch = static_cast<BranchStatement *>(stmt);

        if (!firstBranch->getFallBB() || !firstBranch->getTakenBB()) {
            continue;
        }

        StatementList fallstmts;
        firstBranch->getFallBB()->appendStatementsTo(fallstmts);
        Statement *nextAfterBranch = !fallstmts.empty() ? fallstmts.front() : nullptr;

        if (nextAfterBranch && nextAfterBranch->isBranch()) {
            BranchStatement *secondBranch = static_cast<BranchStatement *>(nextAfterBranch);

            //   branch to A if cond1
            //   branch to B if cond2
            // A: something
            // B:
            // ->
            //   branch to B if !cond1 && cond2
            // A: something
            // B:
            //
            if ((secondBranch->getFallBB() == firstBranch->getTakenBB()) &&
                (secondBranch->getBB()->getNumPredecessors() == 1)) {

                SharedExp cond =
                    Binary::get(opAnd, Unary::get(opNot, firstBranch->getCondExpr()), secondBranch->getCondExpr()->clone());
                firstBranch->setCondExpr(cond->simplify());

                firstBranch->setDest(secondBranch->getFixedDest());
                firstBranch->setTakenBB(secondBranch->getTakenBB());
                firstBranch->setFallBB(secondBranch->getFallBB());

                // remove second branch BB
                BasicBlock *secondBranchBB = secondBranch->getBB();

                assert(secondBranchBB->getNumPredecessors() == 0);
                assert(secondBranchBB->getNumSuccessors() == 2);
                BasicBlock *succ1 = secondBranch->getBB()->getSuccessor(BTHEN);
                BasicBlock *succ2 = secondBranch->getBB()->getSuccessor(BELSE);

                secondBranchBB->removeSuccessor(succ1);
                secondBranchBB->removeSuccessor(succ2);
                succ1->removePredecessor(secondBranchBB);
                succ2->removePredecessor(secondBranchBB);

                bbsToRemove.insert(secondBranchBB);
            }

            //   branch to B if cond1
            //   branch to B if cond2
            // A: something
            // B:
            // ->
            //   branch to B if cond1 || cond2
            // A: something
            // B:
            if ((secondBranch->getTakenBB() == firstBranch->getTakenBB()) &&
                (secondBranch->getBB()->getNumPredecessors() == 1)) {

                SharedExp cond = Binary::get(opOr, firstBranch->getCondExpr(), secondBranch->getCondExpr()->clone());
                firstBranch->setCondExpr(cond->simplify());

                firstBranch->setFallBB(secondBranch->getFallBB());

                BasicBlock *secondBranchBB = secondBranch->getBB();
                assert(secondBranchBB->getNumPredecessors() == 0);
                assert(secondBranchBB->getNumSuccessors() == 2);
                BasicBlock *succ1 = secondBranchBB->getSuccessor(BTHEN);
                BasicBlock *succ2 = secondBranchBB->getSuccessor(BELSE);

                secondBranchBB->removeSuccessor(succ1);
                secondBranchBB->removeSuccessor(succ2);
                succ1->removePredecessor(secondBranchBB);
                succ2->removePredecessor(secondBranchBB);

                bbsToRemove.insert(secondBranchBB);
            }
        }
    }

    const bool removedBBs = !bbsToRemove.empty();
    for (BasicBlock *bb : bbsToRemove) {
        proc->getCFG()->removeBB(bb);
    }

    return removedBBs;
}


void BranchAnalysisPass::fixUglyBranches(UserProc *proc)
{
    StatementList stmts;
    proc->getStatements(stmts);

    for (auto stmt : stmts) {
        if (!stmt->isBranch()) {
            continue;
        }

        SharedExp hl = static_cast<BranchStatement *>(stmt)->getCondExpr();

        // of the form: x{n} - 1 >= 0
        if (hl && (hl->getOper() == opGtrEq) && hl->getSubExp2()->isIntConst() &&
            (hl->access<Const, 2>()->getInt() == 0) && (hl->getSubExp1()->getOper() == opMinus) &&
            hl->getSubExp1()->getSubExp2()->isIntConst() && (hl->access<Const, 1, 2>()->getInt() == 1) &&
            hl->getSubExp1()->getSubExp1()->isSubscript()) {
            Statement *n = hl->access<RefExp, 1, 1>()->getDef();

            if (n && n->isPhi()) {
                PhiAssign *p = static_cast<PhiAssign *>(n);

                for (const auto& phi : *p) {
                    if (!phi.getDef()->isAssign()) {
                        continue;
                    }

                    Assign *a = static_cast<Assign *>(phi.getDef());

                    if (*a->getRight() == *hl->getSubExp1()) {
                        hl->setSubExp1(RefExp::get(a->getLeft(), a));
                        break;
                    }
                }
            }
        }
    }
}



