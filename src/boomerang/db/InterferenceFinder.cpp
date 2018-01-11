#pragma region License
/*
 * This file is part of the Boomerang Decompiler.
 *
 * See the file "LICENSE.TERMS" for information on usage and
 * redistribution of this file, and for a DISCLAIMER OF ALL
 * WARRANTIES.
 */
#pragma endregion License
#include "InterferenceFinder.h"


#include "boomerang/util/ConnectionGraph.h"
#include "boomerang/db/BasicBlock.h"
#include "boomerang/db/CFG.h"
#include "boomerang/core/Boomerang.h"
#include "boomerang/core/Settings.h"
#include "boomerang/db/proc/UserProc.h"
#include "boomerang/util/Log.h"

#include <list>
#include <set>


InterferenceFinder::InterferenceFinder(Cfg* cfg)
    : m_cfg(cfg)
{
}


void InterferenceFinder::findInterferences(ConnectionGraph& cg)
{
    if (m_cfg->getNumBBs() == 0) {
        return;
    }

    std::list<BasicBlock *> workList; // List of BBs still to be processed
    // Set of the same; used for quick membership test
    std::set<BasicBlock *> workSet;
    appendBBs(workList, workSet);

    int count = 0;

    while (!workList.empty() && count < 100000) {
        count++; // prevent infinite loop

        BasicBlock *currBB = workList.back();
        workList.erase(--workList.end());
        workSet.erase(currBB);
        // Calculate live locations and interferences
        bool change = m_livenessAna.calcLiveness(currBB, cg, static_cast<UserProc *>(currBB->getFunction()));

        if (!change) {
            continue;
        }

        if (SETTING(debugLiveness)) {
            Statement *last = currBB->getLastStmt();

            LOG_MSG("Revisiting BB ending with stmt %1 due to change",
                    last ? QString::number(last->getNumber(), 10) : "<none>");
        }

        updateWorkListRev(currBB, workList, workSet);
    }
}


void InterferenceFinder::updateWorkListRev(BasicBlock* currBB, std::list<BasicBlock *>& workList, std::set<BasicBlock *>& workSet)
{
    // Insert inedges of currBB into the worklist, unless already there
    for (BasicBlock *currIn : currBB->getPredecessors()) {
        if (workSet.find(currIn) == workSet.end()) {
            workList.push_front(currIn);
            workSet.insert(currIn);
        }
    }
}


void InterferenceFinder::appendBBs(std::list<BasicBlock *>& worklist, std::set<BasicBlock *>& workset)
{
    // Append my list of BBs to the worklist
    worklist.insert(worklist.end(), m_cfg->begin(), m_cfg->end());

    // Do the same for the workset
    std::copy(m_cfg->begin(), m_cfg->end(), std::inserter(workset, workset.end()));
}
