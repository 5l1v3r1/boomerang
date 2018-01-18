#pragma region License
/*
 * This file is part of the Boomerang Decompiler.
 *
 * See the file "LICENSE.TERMS" for information on usage and
 * redistribution of this file, and for a DISCLAIMER OF ALL
 * WARRANTIES.
 */
#pragma endregion License
#include "StringInstructionProcessor.h"


#include "boomerang/db/BasicBlock.h"
#include "boomerang/db/CFG.h"
#include "boomerang/db/proc/UserProc.h"
#include "boomerang/db/RTL.h"
#include "boomerang/util/Address.h"
#include "boomerang/db/statements/BranchStatement.h"
#include "boomerang/util/Log.h"


StringInstructionProcessor::StringInstructionProcessor(UserProc* proc)
    : m_proc(proc)
{
}



bool StringInstructionProcessor::processStringInstructions()
{
    std::list<std::pair<RTL *, BasicBlock *>> stringInstructions;

    for (BasicBlock *bb : *m_proc->getCFG()) {
        RTLList *bbRTLs = bb->getRTLs();

        if (bbRTLs == nullptr) {
            continue;
        }

        Address prev, addr = Address::ZERO;

        for (RTL *rtl : *bbRTLs) {
            prev = addr;
            addr = rtl->getAddress();

            if (!rtl->empty()) {
                Statement *firstStmt = rtl->front();
                if (firstStmt->isAssign()) {
                    SharedExp lhs = ((Assign *)firstStmt)->getLeft();

                    if (lhs->isMachFtr()) {
                        QString str = lhs->access<Const, 1>()->getStr();

                        if (str.startsWith("%SKIP")) {
                            stringInstructions.push_back({ rtl, bb });

                            // Assume there is only 1 string instruction per BB
                            // This might not be true, but can be migitated
                            // by calling processStringInstructions multiple times
                            // to catch all string instructions.
                            break;
                        }
                    }
                }
            }
        }
    }

    for (auto p : stringInstructions) {
        RTL *skipRTL = p.first;
        BasicBlock *bb = p.second;

        BranchStatement *br1 = new BranchStatement;

        assert(skipRTL->size() >= 4); // They vary; at least 5 or 6

        Statement *s1 = *skipRTL->begin();
        Statement *s6 = *(--skipRTL->end());
        if (s1->isAssign()) {
            br1->setCondExpr(((Assign *)s1)->getRight());
        }
        else {
            br1->setCondExpr(nullptr);
        }
        br1->setDest(skipRTL->getAddress() + 2);

        BranchStatement *br2 = new BranchStatement;
        if (s6->isAssign()) {
            br2->setCondExpr(((Assign *)s6)->getRight());
        }
        else {
            br2->setCondExpr(nullptr);
        }
        br2->setDest(skipRTL->getAddress());

        splitForBranch(bb, skipRTL, br1, br2);
    }

    return !stringInstructions.empty();
}


BasicBlock *StringInstructionProcessor::splitForBranch(BasicBlock *bb, RTL *stringRTL, BranchStatement *skipBranch, BranchStatement *rptBranch)
{
    Address stringAddr = stringRTL->getAddress();
    RTLList::iterator stringIt = std::find(bb->getRTLs()->begin(), bb->getRTLs()->end(), stringRTL);
    assert(stringIt != bb->getRTLs()->end());

    const bool haveA = (stringIt != bb->getRTLs()->begin());
    const bool haveB = (std::next(stringIt) != bb->getRTLs()->end());
    BasicBlock *aBB = nullptr;
    BasicBlock *bBB = nullptr;

    const std::vector<BasicBlock *> oldPredecessors = bb->getPredecessors();
    const std::vector<BasicBlock *> oldSuccessors   = bb->getSuccessors();

    if (haveA) {
        aBB = bb;
        bb = m_proc->getCFG()->splitBB(aBB, stringAddr);
        assert(aBB->getLowAddr() < bb->getLowAddr());
    }
    stringIt = bb->getRTLs()->begin();
    if (haveB) {
        Address splitAddr = (*std::next(stringIt))->getAddress();
        bBB = m_proc->getCFG()->splitBB(bb, splitAddr);
        assert(bb->getLowAddr() < bBB->getLowAddr());
    }
    else {
        // this means the original BB has a fallthrough branch to its successor.
        // Just pretend the successor is the split off B bb.
        bBB = bb->getSuccessor(0);
    }

    assert(bb->getRTLs()->size() == 1); // only the string instruction
    assert(bb->getRTLs()->front()->getAddress() == stringAddr);


    // Make an RTL for the skip and the rpt branch instructions.
    std::unique_ptr<RTLList> skipBBRTL(new RTLList({ new RTL(stringAddr, { skipBranch }) }));
    std::unique_ptr<RTLList> rptBBRTL(new RTLList({ new RTL(**stringIt) }));
    rptBBRTL->front()->setAddress(stringAddr + 1);
    rptBBRTL->front()->pop_front();
    rptBBRTL->front()->back() = rptBranch;

    // remove the original string instruction from the CFG.
    for (BasicBlock *pred : bb->getPredecessors()) {
        for (int i = 0; i < pred->getNumSuccessors(); i++) {
            if (pred->getSuccessor(i) == bb) {
                pred->setSuccessor(i, nullptr);
            }
        }
    }
    bb->removeAllPredecessors();

    // remove connection between the string instruction and the B part
    for (BasicBlock *succ : bb->getSuccessors()) {
        bb->removeSuccessor(succ);
        succ->removePredecessor(bb);
    }

    m_proc->getCFG()->removeBB(bb);

    BasicBlock *skipBB = m_proc->getCFG()->createBB(BBType::Twoway, std::move(skipBBRTL));
    BasicBlock *rptBB = m_proc->getCFG()->createBB(BBType::Twoway, std::move(rptBBRTL));

    assert(skipBB && rptBB);

    if (haveA) {
        aBB->removeAllSuccessors();
        aBB->setType(BBType::Fall);
        m_proc->getCFG()->addEdge(aBB, skipBB);
    }
    else {
        // TODO
        assert(false);
    }

    m_proc->getCFG()->addEdge(skipBB, bBB);
    m_proc->getCFG()->addEdge(skipBB, rptBB);
    m_proc->getCFG()->addEdge(rptBB, bBB);
    m_proc->getCFG()->addEdge(rptBB, rptBB);

    return haveB ? bBB : rptBB;
}
