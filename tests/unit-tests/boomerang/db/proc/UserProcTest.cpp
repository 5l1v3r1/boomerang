#pragma region License
/*
 * This file is part of the Boomerang Decompiler.
 *
 * See the file "LICENSE.TERMS" for information on usage and
 * redistribution of this file, and for a DISCLAIMER OF ALL
 * WARRANTIES.
 */
#pragma endregion License
#include "UserProcTest.h"


#define SAMPLE(path)    (m_project.getSettings()->getDataDirectory().absoluteFilePath("samples/" path))
#define HELLO_PENTIUM   SAMPLE("pentium/hello")


#include "boomerang/db/CFG.h"
#include "boomerang/db/exp/Binary.h"
#include "boomerang/db/exp/Location.h"
#include "boomerang/db/exp/RefExp.h"
#include "boomerang/db/proc/UserProc.h"
#include "boomerang/db/Prog.h"
#include "boomerang/db/RTL.h"
#include "boomerang/db/signature/PentiumSignature.h"
#include "boomerang/db/statements/Assign.h"
#include "boomerang/db/statements/ReturnStatement.h"
#include "boomerang/type/type/IntegerType.h"
#include "boomerang/type/type/VoidType.h"


void UserProcTest::testRemoveStatement()
{
    UserProc proc(Address::INVALID, "test", nullptr);

    Assign *asgn = new Assign(VoidType::get(), Location::regOf(REG_PENT_EAX), Location::regOf(REG_PENT_ECX));

    QVERIFY(!proc.removeStatement(nullptr));
    QVERIFY(!proc.removeStatement(asgn));

    std::unique_ptr<RTLList> bbRTLs(new RTLList);
    bbRTLs->push_back(std::unique_ptr<RTL>(new RTL(Address(0x00000123), { asgn })));
    proc.getCFG()->createBB(BBType::Fall, std::move(bbRTLs));
    QVERIFY(proc.removeStatement(asgn));

    // todo: test that proven true cache is updated
}


void UserProcTest::testInsertAssignAfter()
{
    UserProc proc(Address(0x1000), "test", nullptr);

    std::unique_ptr<RTLList> bbRTLs(new RTLList);
    bbRTLs->push_back(std::unique_ptr<RTL>(new RTL(Address(0x1000), { })));
    BasicBlock *entryBB = proc.getCFG()->createBB(BBType::Fall, std::move(bbRTLs));
    proc.setEntryBB();

    Assign *as = proc.insertAssignAfter(nullptr, Location::regOf(REG_PENT_EAX), Location::regOf(REG_PENT_ECX));
    QVERIFY(as != nullptr);
    QVERIFY(as->getProc() == &proc);
    QVERIFY(as->getBB() == entryBB);

    QVERIFY(proc.getEntryBB()->getRTLs()->front()->size() == 1);
    QVERIFY(*proc.getEntryBB()->getRTLs()->front()->begin() == as);

    Assign *as2 = proc.insertAssignAfter(as, Location::regOf(REG_PENT_EBX), Location::regOf(REG_PENT_EDX));
    QVERIFY(as2 != nullptr);
    QVERIFY(as->getProc() == &proc);
    QVERIFY(as->getBB() == entryBB);
    QVERIFY(proc.getEntryBB()->getRTLs()->front()->size() == 2);
    QVERIFY(*proc.getEntryBB()->getRTLs()->front()->begin() == as);
    QVERIFY(*std::next(proc.getEntryBB()->getRTLs()->front()->begin()) == as2);
}


void UserProcTest::testInsertStatementAfter()
{
    UserProc proc(Address(0x1000), "test", nullptr);

    std::unique_ptr<RTLList> bbRTLs(new RTLList);
    bbRTLs->push_back(std::unique_ptr<RTL>(new RTL(Address(0x1000), { })));
    BasicBlock *entryBB = proc.getCFG()->createBB(BBType::Fall, std::move(bbRTLs));
    proc.setEntryBB();

    Assign *as = proc.insertAssignAfter(nullptr, Location::regOf(REG_PENT_EAX), Location::regOf(REG_PENT_ECX));
    Assign *as2 = new Assign(VoidType::get(), Location::regOf(REG_PENT_EDX), Location::regOf(REG_PENT_EBX));

    proc.insertStatementAfter(as, as2);
    QVERIFY(as2->getBB() == entryBB);
    QVERIFY(proc.getEntryBB()->getRTLs()->front()->size() == 2);
    QVERIFY(*proc.getEntryBB()->getRTLs()->front()->begin() == as);
    QVERIFY(*std::next(proc.getEntryBB()->getRTLs()->front()->begin()) == as2);
}


void UserProcTest::testAddParameterToSignature()
{
    UserProc proc(Address(0x1000), "test", nullptr);

    proc.addParameterToSignature(Location::memOf(Binary::get(opPlus,
        Location::regOf(REG_PENT_ESP), Const::get(4)), &proc),
        VoidType::get());

    QCOMPARE(proc.getSignature()->getNumParams(), 1);

    // try to add the same parameter again
    proc.addParameterToSignature(Location::memOf(Binary::get(opPlus,
        Location::regOf(REG_PENT_ESP), Const::get(4)), &proc),
        VoidType::get());

    QCOMPARE(proc.getSignature()->getNumParams(), 1);
}


void UserProcTest::testInsertParameter()
{
    UserProc proc(Address(0x1000), "test", nullptr);

    proc.insertParameter(Location::memOf(Binary::get(opPlus,
        Location::regOf(REG_PENT_ESP), Const::get(4)), &proc),
        VoidType::get());

    QCOMPARE(proc.getParameters().size(), (size_t)1);
    QCOMPARE(proc.getSignature()->getNumParams(), 1);

    // try to add the same parameter again
    proc.insertParameter(Location::memOf(Binary::get(opPlus,
        Location::regOf(REG_PENT_ESP), Const::get(4)), &proc),
        VoidType::get());

    QCOMPARE(proc.getParameters().size(), (size_t)1);
    QCOMPARE(proc.getSignature()->getNumParams(), 1);
}


void UserProcTest::testParamType()
{
    UserProc proc(Address(0x1000), "test", nullptr);

    QVERIFY(proc.getParamType("invalidParam") == nullptr);

    proc.insertParameter(Location::memOf(Binary::get(opPlus,
        Location::regOf(REG_PENT_ESP), Const::get(4)), &proc),
        VoidType::get());

    SharedConstType ty = proc.getParamType("param1");
    QVERIFY(ty != nullptr);
    QCOMPARE(ty->toString(), VoidType::get()->toString());

    proc.setParamType("param1", IntegerType::get(32, Sign::Signed));
    QCOMPARE(proc.getParamType("param1")->toString(), IntegerType::get(32, Sign::Signed)->toString());

    proc.setParamType(5, VoidType::get()); // proc only has 1 parameter
    QCOMPARE(proc.getParamType("param1")->toString(), IntegerType::get(32, Sign::Signed)->toString());

    proc.setParamType(0, VoidType::get());
    QCOMPARE(proc.getParamType("param1")->toString(), VoidType::get()->toString());
}


void UserProcTest::testLookupParam()
{
    UserProc proc(Address(0x1000), "test", nullptr);

    std::unique_ptr<RTLList> bbRTLs(new RTLList);
    bbRTLs->push_back(std::unique_ptr<RTL>(new RTL(Address(0x1000), { })));
    proc.getCFG()->createBB(BBType::Fall, std::move(bbRTLs));
    proc.setEntryBB();

    SharedExp paramExp = Location::memOf(Binary::get(opPlus,
        Location::regOf(REG_PENT_ESP), Const::get(4)), &proc);

    Statement *ias = proc.getCFG()->findOrCreateImplicitAssign(paramExp->clone());
    proc.insertParameter(RefExp::get(paramExp->clone(), ias), VoidType::get());
    proc.mapSymbolTo(RefExp::get(paramExp->clone(), ias), Location::param("param1", &proc));
    proc.addParameterToSignature(paramExp->clone(), VoidType::get());

    QCOMPARE(proc.lookupParam(paramExp), QString("param1"));
    QCOMPARE(proc.lookupParam(Location::regOf(REG_PENT_ECX)), QString(""));
}


void UserProcTest::testFilterParams()
{
    QVERIFY(m_project.loadBinaryFile(HELLO_PENTIUM));
    Prog *prog = m_project.getProg();

    UserProc *mainProc = static_cast<UserProc *>(prog->getOrCreateFunction(Address(0x08048328)));
    QVERIFY(mainProc != nullptr && !mainProc->isLib());

    QVERIFY(mainProc->filterParams(Terminal::get(opPC)));
    QVERIFY(mainProc->filterParams(Location::get(opTemp, Terminal::get(opTrue), mainProc)));
    QVERIFY(mainProc->filterParams(Location::regOf(REG_PENT_ESP)));
    QVERIFY(!mainProc->filterParams(Location::regOf(REG_PENT_EDX)));
    QVERIFY(mainProc->filterParams(Location::memOf(Const::get(0x08048328))));
    QVERIFY(mainProc->filterParams(Location::memOf(RefExp::get(Location::regOf(REG_PENT_ESP), nullptr))));
    QVERIFY(!mainProc->filterParams(Location::memOf(Binary::get(opPlus,
                                                                Location::regOf(REG_PENT_ESP),
                                                                Const::get(4)))));
    QVERIFY(mainProc->filterParams(Location::global("test", mainProc)));
    QVERIFY(!mainProc->filterParams(Const::get(5)));
}


void UserProcTest::testRetStmt()
{
    UserProc proc(Address(0x1000), "test", nullptr);

    QCOMPARE(proc.getRetAddr(), Address::INVALID);

    ReturnStatement retStmt;
    proc.setRetStmt(&retStmt, Address(0x2000));
    QVERIFY(proc.getRetStmt() == &retStmt);
    QCOMPARE(proc.getRetAddr(), Address(0x2000));
}


void UserProcTest::testFilterReturns()
{
    QVERIFY(m_project.loadBinaryFile(HELLO_PENTIUM));
    QVERIFY(m_project.decodeBinaryFile());
    QVERIFY(m_project.decompileBinaryFile());

    Prog *prog = m_project.getProg();

    UserProc *mainProc = static_cast<UserProc *>(prog->getOrCreateFunction(Address(0x08048328)));
    QVERIFY(mainProc != nullptr && !mainProc->isLib());

    // test cached preservation TODO
    QVERIFY(mainProc->getRetStmt());
    QVERIFY(mainProc->prove(Binary::get(opEquals, Location::regOf(REG_PENT_EBP), Location::regOf(REG_PENT_EBP))));
    QVERIFY(mainProc->filterReturns(Location::regOf(REG_PENT_EBP)));

    QVERIFY(mainProc->filterReturns(Terminal::get(opPC)));
    QVERIFY(mainProc->filterReturns(Location::get(opTemp, Terminal::get(opTrue), mainProc)));
    QVERIFY(!mainProc->filterReturns(Location::regOf(REG_PENT_ESP)));
    QVERIFY(!mainProc->filterReturns(Location::regOf(REG_PENT_EDX)));
    QVERIFY(mainProc->filterReturns(Location::memOf(Const::get(0x08048328))));
}


void UserProcTest::testCreateLocal()
{
    UserProc proc(Address(0x1000), "test", nullptr);

    SharedExp exp = proc.createLocal(VoidType::get(), Location::regOf(REG_PENT_EAX), "eax");
    QVERIFY(exp != nullptr);
    QCOMPARE(exp->toString(), QString("eax"));
    QCOMPARE(proc.getLocalType("eax")->toString(), VoidType::get()->toString());
    QVERIFY(proc.getLocals().size() == 1);

    // set type of local
    exp = proc.createLocal(IntegerType::get(32, Sign::Signed), Location::regOf(REG_PENT_EAX), "eax");
    QVERIFY(exp != nullptr);
    QCOMPARE(exp->toString(), QString("eax"));
    QCOMPARE(proc.getLocalType("eax")->toString(), IntegerType::get(32, Sign::Signed)->toString());
    QVERIFY(proc.getLocals().size() == (size_t)1);
}


void UserProcTest::testAddLocal()
{
    UserProc proc(Address(0x1000), "test", nullptr);

    proc.addLocal(VoidType::get(), "eax", Location::regOf(REG_PENT_EAX));
    QVERIFY(proc.getLocals().size() == (size_t)1);
    QVERIFY(proc.getSymbolMap().size() == 1);
    QCOMPARE(proc.findFirstSymbol(Location::regOf(REG_PENT_EAX)), QString("eax"));

    // test for no duplicates
    proc.addLocal(IntegerType::get(32, Sign::Signed), "eax", Location::regOf(REG_PENT_EAX));
    QVERIFY(proc.getLocals().size() == (size_t)1);
    QVERIFY(proc.getSymbolMap().size() == 1);
    QCOMPARE(proc.findFirstSymbol(Location::regOf(REG_PENT_EAX)), QString("eax"));
}


void UserProcTest::testEnsureExpIsMappedToLocal()
{
    QVERIFY(m_project.loadBinaryFile(HELLO_PENTIUM));

    UserProc proc(Address(0x1000), "test", m_project.getProg()->getRootModule());

    std::unique_ptr<RTLList> bbRTLs(new RTLList);
    bbRTLs->push_back(std::unique_ptr<RTL>(new RTL(Address(0x1000), { })));
    proc.getCFG()->createBB(BBType::Fall, std::move(bbRTLs));
    proc.setEntryBB();

    // do not create local if nullptr def
    proc.ensureExpIsMappedToLocal(RefExp::get(Location::regOf(REG_PENT_EAX), nullptr));
    QCOMPARE(proc.findLocal(Location::regOf(REG_PENT_EAX), VoidType::get()), QString(""));

    // local does not exist
    Statement *ias1 = proc.getCFG()->findOrCreateImplicitAssign(Location::regOf(REG_PENT_EAX));
    QVERIFY(ias1 != nullptr);
    proc.ensureExpIsMappedToLocal(RefExp::get(Location::regOf(REG_PENT_EAX), ias1));
    QCOMPARE(proc.findLocal(Location::regOf(REG_PENT_EAX), VoidType::get()), QString("eax"));

    // local already exists
    proc.ensureExpIsMappedToLocal(RefExp::get(Location::regOf(REG_PENT_EAX), ias1));
    QCOMPARE(proc.findLocal(Location::regOf(REG_PENT_EAX), VoidType::get()), QString("eax"));


    SharedExp memOf = Location::memOf(Binary::get(opPlus,
                                      Location::regOf(REG_PENT_ESP),
                                      Const::get(4)));
    Statement *ias2 = proc.getCFG()->findOrCreateImplicitAssign(memOf);
    QVERIFY(ias2 != nullptr);
    proc.ensureExpIsMappedToLocal(RefExp::get(memOf->clone(), ias2));
    QCOMPARE(proc.findLocal(memOf->clone(), VoidType::get()), QString("local0"));
}


void UserProcTest::testGetSymbolExp()
{
    UserProc proc(Address(0x1000), "test", nullptr);
    proc.setSignature(std::make_shared<CallingConvention::StdC::PentiumSignature>("test"));

    SharedExp local0 = proc.getSymbolExp(Location::regOf(REG_PENT_EAX), VoidType::get());
    QVERIFY(local0 != nullptr);
    QCOMPARE(local0->toString(), Location::local("local0", &proc)->toString());
    QCOMPARE(proc.getLocalType("local0")->toString(), VoidType::get()->toString());

    SharedExp local0_2 = proc.getSymbolExp(Location::regOf(REG_PENT_EAX), VoidType::get());
    QVERIFY(local0_2 != nullptr);
    QCOMPARE(local0_2->toString(), local0->toString());

    SharedExp spMinus4 = Location::memOf(
        Binary::get(opMinus,
                    RefExp::get(Location::regOf(REG_PENT_ESP), nullptr),
                    Const::get(4)));

    SharedExp spMinus7 = Location::memOf(
        Binary::get(opMinus,
                    RefExp::get(Location::regOf(REG_PENT_ESP), nullptr),
                    Const::get(7)));

    SharedExp local1 = proc.getSymbolExp(spMinus4, VoidType::get(), true);
    QVERIFY(local1 != nullptr);
    QCOMPARE(local1->toString(), QString("local1"));
    QCOMPARE(proc.getLocalType("local1")->toString(), IntegerType::get(STD_SIZE)->toString());



    SharedExp local2 = proc.getSymbolExp(spMinus7, IntegerType::get(8), true);
    QVERIFY(local2 != nullptr);
    QCOMPARE(local2->toString(), Location::memOf(Binary::get(opPlus,
        Unary::get(opAddrOf, Location::local("local1", &proc)), Const::get(3)))->toString());
}


QTEST_GUILESS_MAIN(UserProcTest)
