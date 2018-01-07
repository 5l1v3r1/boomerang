#pragma region License
/*
 * This file is part of the Boomerang Decompiler.
 *
 * See the file "LICENSE.TERMS" for information on usage and
 * redistribution of this file, and for a DISCLAIMER OF ALL
 * WARRANTIES.
 */
#pragma endregion License
#include "CFGTest.h"


#include "boomerang/core/Boomerang.h"
#include "boomerang/db/BasicBlock.h"
#include "boomerang/db/CFG.h"
#include "boomerang/db/RTL.h"
#include "boomerang/db/proc/UserProc.h"
#include "boomerang/db/statements/Assign.h"
#include "boomerang/db/exp/Terminal.h"
#include "boomerang/type/type/VoidType.h"

#include <QDebug>


std::unique_ptr<RTLList> createRTLs(Address baseAddr, int numRTLs)
{
    std::unique_ptr<RTLList> rtls(new RTLList);

    for (int i = 0; i < numRTLs; i++) {
        rtls->push_back(new RTL(baseAddr + i,
            { new Assign(VoidType::get(), Terminal::get(opNil), Terminal::get(opNil)) }));
    }

    return rtls;
}


void CFGTest::initTestCase()
{
    Boomerang::get()->getSettings()->setDataDirectory(BOOMERANG_TEST_BASE "share/boomerang/");
    Boomerang::get()->getSettings()->setPluginDirectory(BOOMERANG_TEST_BASE "lib/boomerang/plugins/");
}


void CFGTest::testCreateBB()
{
    UserProc proc(Address(0x1000), "test", nullptr);
    Cfg *cfg = proc.getCFG();

    BasicBlock *bb = cfg->createBB(BBType::Oneway, createRTLs(Address(0x1000), 1));
    QVERIFY(bb != nullptr);
    QVERIFY(bb->isType(BBType::Oneway));
    QCOMPARE(bb->getLowAddr(), Address(0x1000));
    QCOMPARE(bb->getHiAddr(),  Address(0x1000));
    QCOMPARE(bb->getRTLs()->size(), (size_t)1);

    QCOMPARE(cfg->getNumBBs(), 1);
}


void CFGTest::testCreateIncompleteBB()
{
    UserProc proc(Address(0x1000), "test", nullptr);
    Cfg *cfg = proc.getCFG();

    BasicBlock *bb = cfg->createIncompleteBB(Address(0x1000));
    QVERIFY(bb->isIncomplete());
    QCOMPARE(bb->getLowAddr(), Address(0x1000));
    QCOMPARE(cfg->getNumBBs(), 1);
}


void CFGTest::testRemoveBB()
{
    UserProc proc(Address(0x1000), "test", nullptr);
    Cfg *cfg = proc.getCFG();

    cfg->removeBB(nullptr);
    QCOMPARE(cfg->getNumBBs(), 0);

    // remove incomplete BB
    BasicBlock *bb = cfg->createIncompleteBB(Address(0x1000));
    cfg->removeBB(bb);
    QCOMPARE(cfg->getNumBBs(), 0);

    // remove complete BB
    bb = cfg->createBB(BBType::Oneway, createRTLs(Address(0x1000), 1));
    cfg->removeBB(bb);
    QCOMPARE(cfg->getNumBBs(), 0);
}


void CFGTest::testAddEdge()
{
    QSKIP("Not yet implemented.");
}


void CFGTest::testIsWellFormed()
{
    QSKIP("Not yet implemented.");
}


QTEST_MAIN(CFGTest)
