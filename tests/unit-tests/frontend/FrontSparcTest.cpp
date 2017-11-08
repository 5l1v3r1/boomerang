#pragma region License
/*
 * This file is part of the Boomerang Decompiler.
 *
 * See the file "LICENSE.TERMS" for information on usage and
 * redistribution of this file, and for a DISCLAIMER OF ALL
 * WARRANTIES.
 */
#pragma endregion License
#include "FrontSparcTest.h"


#include "boomerang/core/Boomerang.h"

#include "boomerang/db/BasicBlock.h"
#include "boomerang/db/CFG.h"
#include "boomerang/db/proc/UserProc.h"
#include "boomerang/db/Prog.h"
#include "boomerang/db/RTL.h"
#include "boomerang/core/Project.h"
#include "boomerang/util/Types.h"
#include "boomerang/util/Log.h"

#include "boomerang/frontend/sparc/sparcfrontend.h"

#include <QDebug>


#define HELLO_SPARC     (Boomerang::get()->getSettings()->getDataDirectory().absoluteFilePath("samples/sparc/hello"))
#define BRANCH_SPARC    (Boomerang::get()->getSettings()->getDataDirectory().absoluteFilePath("samples/sparc/branch"))


void FrontSparcTest::initTestCase()
{
    Boomerang::get()->getSettings()->setDataDirectory(BOOMERANG_TEST_BASE "share/boomerang/");
    Boomerang::get()->getSettings()->setPluginDirectory(BOOMERANG_TEST_BASE "lib/boomerang/plugins/");
}


void FrontSparcTest::test1()
{
    QString     expected;
    QString     actual;
    QTextStream strm(&actual);

    IProject& project = *Boomerang::get()->getOrCreateProject();

    project.loadBinaryFile(HELLO_SPARC);
    IFileLoader *pBF = project.getBestLoader(HELLO_SPARC);
    QVERIFY(pBF != nullptr);
    QVERIFY(pBF->getMachine() == Machine::SPARC);

    Prog      *prog = new Prog("HELLO_SPARC");
    IFrontEnd *pFE  = new SparcFrontEnd(pBF, prog);
    prog->setFrontEnd(pFE);

    bool    gotMain;
    Address addr = pFE->getMainEntryPoint(gotMain);
    QVERIFY(addr != Address::INVALID);

    // Decode first instruction
    DecodeResult inst;
    pFE->decodeInstruction(addr, inst);
    QVERIFY(inst.rtl != nullptr);
    inst.rtl->print(strm);

    expected = "0x00010684    0 *32* tmp := r14 - 112\n"
               "              0 *32* m[r14] := r16\n"
               "              0 *32* m[r14 + 4] := r17\n"
               "              0 *32* m[r14 + 8] := r18\n"
               "              0 *32* m[r14 + 12] := r19\n"
               "              0 *32* m[r14 + 16] := r20\n"
               "              0 *32* m[r14 + 20] := r21\n"
               "              0 *32* m[r14 + 24] := r22\n"
               "              0 *32* m[r14 + 28] := r23\n"
               "              0 *32* m[r14 + 32] := r24\n"
               "              0 *32* m[r14 + 36] := r25\n"
               "              0 *32* m[r14 + 40] := r26\n"
               "              0 *32* m[r14 + 44] := r27\n"
               "              0 *32* m[r14 + 48] := r28\n"
               "              0 *32* m[r14 + 52] := r29\n"
               "              0 *32* m[r14 + 56] := r30\n"
               "              0 *32* m[r14 + 60] := r31\n"
               "              0 *32* r24 := r8\n"
               "              0 *32* r25 := r9\n"
               "              0 *32* r26 := r10\n"
               "              0 *32* r27 := r11\n"
               "              0 *32* r28 := r12\n"
               "              0 *32* r29 := r13\n"
               "              0 *32* r30 := r14\n"
               "              0 *32* r31 := r15\n"
               "              0 *32* r14 := tmp\n";
    QCOMPARE(actual, expected);
    actual.clear();

    addr += inst.numBytes;
    pFE->decodeInstruction(addr, inst);
    inst.rtl->print(strm);
    expected = QString("0x00010688    0 *32* r8 := 0x10400\n");
    QCOMPARE(actual, expected);
    actual.clear();

    addr += inst.numBytes;
    pFE->decodeInstruction(addr, inst);
    inst.rtl->print(strm);
    expected = QString("0x0001068c    0 *32* r8 := r8 | 848\n");
    QCOMPARE(actual, expected);
    actual.clear();

    delete pFE;
    // delete pBF;
}


void FrontSparcTest::test2()
{
    DecodeResult inst;
    QString      expected;
    QString      actual;
    QTextStream  strm(&actual);

    IProject& project = *Boomerang::get()->getOrCreateProject();

    project.loadBinaryFile(HELLO_SPARC);
    IFileLoader *loader = project.getBestLoader(HELLO_SPARC);
    QVERIFY(loader != nullptr);

    Prog *prog = new Prog("HELLO_SPARC");
    QVERIFY(loader->getMachine() == Machine::SPARC);
    IFrontEnd *pFE = new SparcFrontEnd(loader, prog);
    prog->setFrontEnd(pFE);

    pFE->decodeInstruction(Address(0x00010690), inst);
    inst.rtl->print(strm);
    // This call is to out of range of the program's text limits (to the Program Linkage Table (PLT), calling printf)
    // This is quite normal.
    expected = QString("0x00010690    0 CALL printf(\n"
                       "              )\n"
                       "              Reaching definitions: \n"
                       "              Live variables: \n");
    QCOMPARE(actual, expected);
    actual.clear();

    pFE->decodeInstruction(Address(0x00010694), inst);
    inst.rtl->print(strm);
    expected = QString("0x00010694\n");
    QCOMPARE(actual, expected);
    actual.clear();

    pFE->decodeInstruction(Address(0x00010698), inst);
    inst.rtl->print(strm);
    expected = QString("0x00010698    0 *32* r8 := 0\n");
    QCOMPARE(actual, expected);
    actual.clear();

    pFE->decodeInstruction(Address(0x0001069C), inst);
    inst.rtl->print(strm);
    expected = QString("0x0001069c    0 *32* r24 := r8\n");
    QCOMPARE(actual, expected);

    delete pFE;
    // delete pBF;
}


void FrontSparcTest::test3()
{
    DecodeResult inst;
    QString      expected;
    QString      actual;
    QTextStream  strm(&actual);

    IProject& project = *Boomerang::get()->getOrCreateProject();

    project.loadBinaryFile(HELLO_SPARC);
    IFileLoader *loader = project.getBestLoader(HELLO_SPARC);
    QVERIFY(loader != nullptr);

    Prog *prog = new Prog("HELLO_SPARC");
    QVERIFY(loader->getMachine() == Machine::SPARC);
    IFrontEnd *pFE = new SparcFrontEnd(loader, prog);
    prog->setFrontEnd(pFE);

    pFE->decodeInstruction(Address(0x000106a0), inst);
    inst.rtl->print(strm);
    expected = QString("0x000106a0\n");
    QCOMPARE(actual, expected);
    actual.clear();
    pFE->decodeInstruction(Address(0x000106a4), inst);
    inst.rtl->print(strm);
    expected = QString("0x000106a4    0 RET\n"
                       "              Modifieds: \n"
                       "              Reaching definitions: \n");
    QCOMPARE(actual, expected);
    actual.clear();

    pFE->decodeInstruction(Address(0x000106a8), inst);
    inst.rtl->print(strm);
    expected = QString("0x000106a8    0 *32* tmp := 0\n"
                       "              0 *32* r8 := r24\n"
                       "              0 *32* r9 := r25\n"
                       "              0 *32* r10 := r26\n"
                       "              0 *32* r11 := r27\n"
                       "              0 *32* r12 := r28\n"
                       "              0 *32* r13 := r29\n"
                       "              0 *32* r14 := r30\n"
                       "              0 *32* r15 := r31\n"
                       "              0 *32* r0 := tmp\n"
                       "              0 *32* r16 := m[r14]\n"
                       "              0 *32* r17 := m[r14 + 4]\n"
                       "              0 *32* r18 := m[r14 + 8]\n"
                       "              0 *32* r19 := m[r14 + 12]\n"
                       "              0 *32* r20 := m[r14 + 16]\n"
                       "              0 *32* r21 := m[r14 + 20]\n"
                       "              0 *32* r22 := m[r14 + 24]\n"
                       "              0 *32* r23 := m[r14 + 28]\n"
                       "              0 *32* r24 := m[r14 + 32]\n"
                       "              0 *32* r25 := m[r14 + 36]\n"
                       "              0 *32* r26 := m[r14 + 40]\n"
                       "              0 *32* r27 := m[r14 + 44]\n"
                       "              0 *32* r28 := m[r14 + 48]\n"
                       "              0 *32* r29 := m[r14 + 52]\n"
                       "              0 *32* r30 := m[r14 + 56]\n"
                       "              0 *32* r31 := m[r14 + 60]\n"
                       "              0 *32* r0 := tmp\n");
    QCOMPARE(actual, expected);

    delete pFE;
    // delete pBF;
}


void FrontSparcTest::testBranch()
{
    DecodeResult inst;
    QString      expected;
    QString      actual;
    QTextStream  strm(&actual);

    IProject& project = *Boomerang::get()->getOrCreateProject();

    project.loadBinaryFile(BRANCH_SPARC);
    IFileLoader *loader = project.getBestLoader(BRANCH_SPARC);
    QVERIFY(loader != nullptr);

    Prog *prog = new Prog("BRANCH_SPARC");
    QVERIFY(loader->getMachine() == Machine::SPARC);
    IFrontEnd *pFE = new SparcFrontEnd(loader, prog);
    prog->setFrontEnd(pFE);

    // bne
    pFE->decodeInstruction(Address(0x00010ab0), inst);
    inst.rtl->print(strm);
    expected = QString("0x00010ab0    0 BRANCH 0x00010ac8, condition not equals\n"
                       "High level: %flags\n");
    QCOMPARE(actual, expected);
    actual.clear();

    // bg
    pFE->decodeInstruction(Address(0x00010af8), inst);
    inst.rtl->print(strm);
    expected = QString("0x00010af8    0 BRANCH 0x00010b10, condition "
                       "signed greater\n"
                       "High level: %flags\n");
    QCOMPARE(actual, expected);
    actual.clear();

    // bleu
    pFE->decodeInstruction(Address(0x00010b44), inst);
    inst.rtl->print(strm);
    expected = QString("0x00010b44    0 BRANCH 0x00010b54, condition unsigned less or equals\n"
                       "High level: %flags\n");
    QCOMPARE(actual, expected);
    actual.clear();

    delete pFE;
    // delete pBF;
}


void FrontSparcTest::testDelaySlot()
{
    IProject& project = *Boomerang::get()->getOrCreateProject();

    project.loadBinaryFile(BRANCH_SPARC);
    IFileLoader *loader = project.getBestLoader(BRANCH_SPARC);
    QVERIFY(loader != nullptr);

    Prog *prog = new Prog("BRANCH_SPARC");
    QVERIFY(loader->getMachine() == Machine::SPARC);
    IFrontEnd *pFE = new SparcFrontEnd(loader, prog);
    prog->setFrontEnd(pFE);
    // decode calls readLibraryCatalog(), which needs to have definitions for non-sparc architectures cleared
    Type::clearNamedTypes();
    pFE->decode(prog);

    bool    gotMain;
    Address addr = pFE->getMainEntryPoint(gotMain);
    QVERIFY(addr != Address::INVALID);
    QString     actual;
    QTextStream strm(&actual);
    Module      *m = prog->getOrInsertModule("test");

    UserProc    *pProc = new UserProc(addr, "testDelaySlot", m);
    QString     dum;
    QTextStream dummy(&dum);
    bool        res = pFE->processProc(addr, pProc, dummy, false);

    QVERIFY(res == 1);
    Cfg        *cfg = pProc->getCFG();
    BBIterator it;
    BasicBlock *bb = cfg->getFirstBB(it);
    bb->print(strm);
    QString expected("Call BB:\n"
                     "  in edges: \n"
                     "  out edges: 0x00010a98 \n"
                     "0x00010a80    0 *32* tmp := r14 - 120\n"
                     "              0 *32* m[r14] := r16\n"
                     "              0 *32* m[r14 + 4] := r17\n"
                     "              0 *32* m[r14 + 8] := r18\n"
                     "              0 *32* m[r14 + 12] := r19\n"
                     "              0 *32* m[r14 + 16] := r20\n"
                     "              0 *32* m[r14 + 20] := r21\n"
                     "              0 *32* m[r14 + 24] := r22\n"
                     "              0 *32* m[r14 + 28] := r23\n"
                     "              0 *32* m[r14 + 32] := r24\n"
                     "              0 *32* m[r14 + 36] := r25\n"
                     "              0 *32* m[r14 + 40] := r26\n"
                     "              0 *32* m[r14 + 44] := r27\n"
                     "              0 *32* m[r14 + 48] := r28\n"
                     "              0 *32* m[r14 + 52] := r29\n"
                     "              0 *32* m[r14 + 56] := r30\n"
                     "              0 *32* m[r14 + 60] := r31\n"
                     "              0 *32* r24 := r8\n"
                     "              0 *32* r25 := r9\n"
                     "              0 *32* r26 := r10\n"
                     "              0 *32* r27 := r11\n"
                     "              0 *32* r28 := r12\n"
                     "              0 *32* r29 := r13\n"
                     "              0 *32* r30 := r14\n"
                     "              0 *32* r31 := r15\n"
                     "              0 *32* r14 := tmp\n"
                     "0x00010a84    0 *32* r16 := 0x11400\n"
                     "0x00010a88    0 *32* r16 := r16 | 808\n"
                     "0x00010a8c    0 *32* r8 := r16\n"
                     "0x00010a90    0 *32* tmp := r30\n"
                     "              0 *32* r9 := r30 - 20\n"
                     "0x00010a90    0 CALL scanf(\n"
                     "              )\n"
                     "              Reaching definitions: \n"
                     "              Live variables: \n");

    QCOMPARE(actual, expected);
    actual.clear();

    bb = cfg->getNextBB(it);
    QVERIFY(bb);
    bb->print(strm);
    expected = "Call BB:\n"
               "  in edges: 0x00010a90(0x00010a80) \n"
               "  out edges: 0x00010aa4 \n"
               "0x00010a98    0 *32* r8 := r16\n"
               "0x00010a9c    0 *32* tmp := r30\n"
               "              0 *32* r9 := r30 - 24\n"
               "0x00010a9c    0 CALL scanf(\n"
               "              )\n"
               "              Reaching definitions: \n"
               "              Live variables: \n";

    QCOMPARE(actual, expected);
    actual.clear();

    bb = cfg->getNextBB(it);
    QVERIFY(bb);
    bb->print(strm);
    expected = "Twoway BB:\n"
               "  in edges: 0x00010a9c(0x00010a98) \n"
               "  out edges: 0x00010ac8 0x00010ab8 \n"
               "0x00010aa4    0 *32* r8 := m[r30 - 20]\n"
               "0x00010aa8    0 *32* r16 := 5\n"
               "0x00010aac    0 *32* tmp := r16\n"
               "              0 *32* r0 := r16 - r8\n"
               "              0 *v* %flags := SUBFLAGS( tmp, r8, r0 )\n"
               "0x00010ab0    0 *32* r8 := 0x11400\n"
               "0x00010ab0    0 BRANCH 0x00010ac8, condition not equals\n"
               "High level: %flags\n";
    QCOMPARE(actual, expected);
    actual.clear();

    bb = cfg->getNextBB(it);
    QVERIFY(bb);
    bb->print(strm);
    expected = "L1: Twoway BB:\n"
               "  in edges: 0x00010ab0(0x00010aa4) 0x00010ac4(0x00010ac0) \n"
               "  out edges: 0x00010ad8 0x00010ad0 \n"
               "0x00010ac8    0 *32* r8 := 0x11400\n"
               "0x00010ac8    0 BRANCH 0x00010ad8, condition equals\n"
               "High level: %flags\n";
    QCOMPARE(actual, expected);
    actual.clear();

    bb = cfg->getNextBB(it);
    QVERIFY(bb);
    bb->print(strm);
    expected = "Call BB:\n"
               "  in edges: 0x00010ab0(0x00010aa4) \n"
               "  out edges: 0x00010ac0 \n"
               "0x00010ab8    0 *32* r8 := r8 | 816\n"
               "0x00010ab8    0 CALL printf(\n"
               "              )\n"
               "              Reaching definitions: \n"
               "              Live variables: \n";

    QCOMPARE(actual, expected);

    delete prog;
}


QTEST_MAIN(FrontSparcTest)
