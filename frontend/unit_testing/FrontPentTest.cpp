/***************************************************************************//**
 * \file       FrontPentTest.cc
 * OVERVIEW:   Provides the implementation for the FrontPentTest class, which
 *                tests the sparc front end
 *============================================================================*/
/*
 * $Revision$
 *
 * 05 Apr 02 - Mike: Created
 * 21 May 02 - Mike: Mods for gcc 3.1
 */

#include <QProcessEnvironment>
#include "types.h"
#include "rtl.h"
#include "FrontPentTest.h"
#include "prog.h"
#include "frontend.h"
#include "pentiumfrontend.h"
#include "BinaryFile.h"
#include "BinaryFileStub.h"
#include "decoder.h"
#include "boomerang.h"
#include "log.h"

#define HELLO_PENT        "test/pentium/hello"
#define BRANCH_PENT        "test/pentium/branch"
#define FEDORA2_TRUE    "test/pentium/fedora2_true"
#define FEDORA3_TRUE    "test/pentium/fedora3_true"
#define SUSE_TRUE        "test/pentium/suse_true"


/***************************************************************************//**
 * FUNCTION:        FrontPentTest::setUp
 * OVERVIEW:        Set up anything needed before all tests
 * NOTE:            Called before any tests
 * NOTE:            Also appears to be called before all tests!
 * PARAMETERS:        <none>
 *
 *============================================================================*/
static bool logset = false;
static std::string TEST_BASE;
void FrontPentTest::SetUp () {
    if (!logset) {
        logset = true;
        TEST_BASE = QProcessEnvironment::systemEnvironment().value("BOOMERANG_TEST_BASE","").toStdString();
        Boomerang::get()->setProgPath(TEST_BASE);
        Boomerang::get()->setPluginPath(TEST_BASE+"/out");
        if(TEST_BASE.empty())
            fprintf(stderr,"BOOMERANG_TEST_BASE environment variable net set, many test will fail\n");
        Boomerang::get()->setLogger(new NullLogger());
    }
}
/***************************************************************************//**
 * FUNCTION:        FrontPentTest::tearDown
 * OVERVIEW:        Delete objects created in setUp
 * NOTE:            Called after all tests
 * PARAMETERS:        <none>
 *
 *============================================================================*/
void FrontPentTest::TearDown () {
}

/***************************************************************************//**
 * FUNCTION:        FrontPentTest::test1
 * OVERVIEW:        Test decoding some pentium instructions
 *============================================================================*/
TEST_F(FrontPentTest,test1) {
    std::ostringstream ost;

    BinaryFileFactory bff;
    BinaryFile *pBF = bff.Load(HELLO_PENT);
    if (pBF == nullptr)
        pBF = new BinaryFileStub();
    ASSERT_TRUE(pBF != 0);
    ASSERT_TRUE(pBF->GetMachine() == MACHINE_PENTIUM);
    Prog* prog = new Prog;
    FrontEnd *pFE = new PentiumFrontEnd(pBF, prog, &bff);
    prog->setFrontEnd(pFE);

    bool gotMain;
    ADDRESS addr = pFE->getMainEntryPoint(gotMain);
    ASSERT_TRUE (addr != NO_ADDRESS);

    // Decode first instruction
    DecodeResult inst = pFE->decodeInstruction(addr);
    inst.rtl->print(ost);

    std::string expected(
        "08048328    0 *32* m[r28 - 4] := r29\n"
        "            0 *32* r28 := r28 - 4\n");
    ASSERT_EQ(expected, std::string(ost.str()));

    std::ostringstream o2;
    addr += inst.numBytes;
    inst = pFE->decodeInstruction(addr);
    inst.rtl->print(o2);
    expected = std::string("08048329    0 *32* r29 := r28\n");
    ASSERT_EQ(expected, std::string(o2.str()));

    std::ostringstream o3;
    addr = 0x804833b;
    inst = pFE->decodeInstruction(addr);
    inst.rtl->print(o3);
    expected = std::string(
        "0804833b    0 *32* m[r28 - 4] := 0x80483fc\n"
        "            0 *32* r28 := r28 - 4\n");
    ASSERT_EQ(expected, std::string(o3.str()));

    delete pFE;
    // delete pBF;
}

TEST_F(FrontPentTest,test2) {
    DecodeResult inst;
    std::string expected;

    BinaryFileFactory bff;
    BinaryFile *pBF = bff.Load(HELLO_PENT);
    if (pBF == nullptr)
        pBF = new BinaryFileStub();
    ASSERT_TRUE(pBF != 0);
    ASSERT_TRUE(pBF->GetMachine() == MACHINE_PENTIUM);
    Prog* prog = new Prog;
    FrontEnd *pFE = new PentiumFrontEnd(pBF, prog, &bff);
    prog->setFrontEnd(pFE);

    std::ostringstream o1;
    inst = pFE->decodeInstruction(ADDRESS::g(0x8048345));
    inst.rtl->print(o1);
    expected = std::string(
        "08048345    0 *32* tmp1 := r28\n"
        "            0 *32* r28 := r28 + 16\n"
        "            0 *v* %flags := ADDFLAGS32( tmp1, 16, r28 )\n");
    ASSERT_EQ(expected, std::string(o1.str()));

    std::ostringstream o2;
    inst = pFE->decodeInstruction(ADDRESS::g(0x8048348));
    inst.rtl->print(o2);
    expected = std::string(
        "08048348    0 *32* r24 := 0\n");
    ASSERT_EQ(expected, std::string(o2.str()));

    std::ostringstream o3;
    inst = pFE->decodeInstruction(ADDRESS::g(0x8048329));
    inst.rtl->print(o3);
    expected = std::string("08048329    0 *32* r29 := r28\n");
    ASSERT_EQ(expected, std::string(o3.str()));

    delete pFE;
    // delete pBF;
}

TEST_F(FrontPentTest,test3) {
    DecodeResult inst;
    std::string expected;

    BinaryFileFactory bff;
    BinaryFile *pBF = bff.Load(HELLO_PENT);
    if (pBF == nullptr)
        pBF = new BinaryFileStub();
    ASSERT_TRUE(pBF != 0);
    ASSERT_TRUE(pBF->GetMachine() == MACHINE_PENTIUM);
    Prog* prog = new Prog;
    FrontEnd *pFE = new PentiumFrontEnd(pBF, prog, &bff);
    prog->setFrontEnd(pFE);

    std::ostringstream o1;
    inst = pFE->decodeInstruction(ADDRESS::g(0x804834d));
    inst.rtl->print(o1);
    expected = std::string(
        "0804834d    0 *32* r28 := r29\n"
        "            0 *32* r29 := m[r28]\n"
        "            0 *32* r28 := r28 + 4\n");
    ASSERT_EQ(expected, std::string(o1.str()));

    std::ostringstream o2;
    inst = pFE->decodeInstruction(ADDRESS::g(0x804834e));
    inst.rtl->print(o2);
    expected = std::string(
        "0804834e    0 *32* %pc := m[r28]\n"
        "            0 *32* r28 := r28 + 4\n"
        "            0 RET\n"
        "              Modifieds: \n"
        "              Reaching definitions: \n");

    ASSERT_EQ(expected, std::string(o2.str()));

    delete pFE;
    // delete pBF;
}

TEST_F(FrontPentTest,testBranch) {
    DecodeResult inst;
    std::string expected;

    BinaryFileFactory bff;
    BinaryFile *pBF = bff.Load(BRANCH_PENT);
    if (pBF == nullptr)
        pBF = new BinaryFileStub();
    ASSERT_TRUE(pBF != 0);
    ASSERT_TRUE(pBF->GetMachine() == MACHINE_PENTIUM);
    Prog* prog = new Prog;
    FrontEnd *pFE = new PentiumFrontEnd(pBF, prog, &bff);
    prog->setFrontEnd(pFE);

    // jne
    std::ostringstream o1;
    inst = pFE->decodeInstruction(ADDRESS::g(0x8048979));
    inst.rtl->print(o1);
    expected = std::string("08048979    0 BRANCH 0x8048988, condition "
      "not equals\n"
      "High level: %flags\n");
    ASSERT_EQ(expected, o1.str());

    // jg
    std::ostringstream o2;
    inst = pFE->decodeInstruction(ADDRESS::g(0x80489c1));
    inst.rtl->print(o2);
    expected = std::string(
      "080489c1    0 BRANCH 0x80489d5, condition signed greater\n"
      "High level: %flags\n");
    ASSERT_EQ(expected, std::string(o2.str()));

    // jbe
    std::ostringstream o3;
    inst = pFE->decodeInstruction(ADDRESS::g(0x8048a1b));
    inst.rtl->print(o3);
    expected = std::string(
        "08048a1b    0 BRANCH 0x8048a2a, condition unsigned less or equals\n"
        "High level: %flags\n");
    ASSERT_EQ(expected, std::string(o3.str()));

    delete pFE;
    // delete pBF;
}

TEST_F(FrontPentTest,testFindMain) {
    // Test the algorithm for finding main, when there is a call to __libc_start_main
    // Also tests the loader hack
    BinaryFileFactory bff;
    BinaryFile* pBF = bff.Load(FEDORA2_TRUE);
    ASSERT_TRUE(pBF != nullptr);
    Prog* prog = new Prog;
    FrontEnd* pFE = new PentiumFrontEnd(pBF, prog, &bff);
    prog->setFrontEnd(pFE);
    ASSERT_TRUE(pFE != nullptr);
    bool found;
    ADDRESS addr = pFE->getMainEntryPoint(found);
    ADDRESS expected = ADDRESS::g(0x8048b10);
    ASSERT_EQ(expected, addr);
    pBF->Close();
    bff.UnLoad();

    pBF = bff.Load(FEDORA3_TRUE);
    ASSERT_TRUE(pBF != nullptr);
    pFE = new PentiumFrontEnd(pBF, prog, &bff);
    prog->setFrontEnd(pFE);
    ASSERT_TRUE(pFE != nullptr);
    addr = pFE->getMainEntryPoint(found);
    expected = ADDRESS::g(0x8048c4a);
    ASSERT_EQ(expected, addr);
    pBF->Close();
    bff.UnLoad();

    pBF = bff.Load(SUSE_TRUE);
    ASSERT_TRUE(pBF != nullptr);
    pFE = new PentiumFrontEnd(pBF, prog, &bff);
    prog->setFrontEnd(pFE);
    ASSERT_TRUE(pFE != nullptr);
    addr = pFE->getMainEntryPoint(found);
    expected = ADDRESS::g(0x8048b60);
    ASSERT_EQ(expected, addr);
    pBF->Close();

    delete pFE;
}
