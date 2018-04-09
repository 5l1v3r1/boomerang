#pragma region License
/*
 * This file is part of the Boomerang Decompiler.
 *
 * See the file "LICENSE.TERMS" for information on usage and
 * redistribution of this file, and for a DISCLAIMER OF ALL
 * WARRANTIES.
 */
#pragma endregion License
#include "StatementTest.h"


#include "boomerang/db/CFG.h"
#include "boomerang/core/Boomerang.h"
#include "boomerang/core/Project.h"
#include "boomerang/db/exp/Const.h"
#include "boomerang/db/exp/Location.h"
#include "boomerang/db/exp/RefExp.h"
#include "boomerang/db/exp/Terminal.h"
#include "boomerang/db/exp/Ternary.h"
#include "boomerang/db/statements/Assign.h"
#include "boomerang/db/statements/ImplicitAssign.h"
#include "boomerang/db/statements/CallStatement.h"
#include "boomerang/db/statements/CaseStatement.h"
#include "boomerang/db/statements/BranchStatement.h"
#include "boomerang/db/statements/BoolAssign.h"
#include "boomerang/db/statements/PhiAssign.h"
#include "boomerang/db/RTL.h"
#include "boomerang/db/signature/Signature.h"
#include "boomerang/db/BasicBlock.h"
#include "boomerang/db/Prog.h"
#include "boomerang/db/proc/UserProc.h"
#include "boomerang/passes/PassManager.h"
#include "boomerang/util/Log.h"
#include "boomerang/frontend/pentium/pentiumfrontend.h"
#include "boomerang/type/type/IntegerType.h"


#include <sstream>
#include <map>


#define HELLO_PENTIUM      (Boomerang::get()->getSettings()->getDataDirectory().absoluteFilePath("samples/pentium/hello"))
#define GLOBAL1_PENTIUM    (Boomerang::get()->getSettings()->getDataDirectory().absoluteFilePath("samples/pentium/global1"))


void compareStrings(const QString& actual, const QString& expected)
{
    QStringList actualList = actual.split('\n');
    QStringList expectedList = expected.split('\n');

    for (int i = 0; i < std::min(actualList.length(), expectedList.length()); i++) {
        QCOMPARE(actualList[i], expectedList[i]);
    }

    QVERIFY(actualList.length() == expectedList.length());
}


void StatementTest::initTestCase()
{
    Boomerang::get()->getSettings()->setDataDirectory(BOOMERANG_TEST_BASE "share/boomerang/");
    Boomerang::get()->getSettings()->setPluginDirectory(BOOMERANG_TEST_BASE "lib/boomerang/plugins/");
}


void StatementTest::cleanupTestCase()
{
    Boomerang::destroy();
}


void StatementTest::testEmpty()
{
    // Force "verbose" flag (-v)
    SETTING(verboseOutput) = true;
    Boomerang::get()->getSettings()->setOutputDirectory("./unit_test/");

    Project project;
    QVERIFY(project.loadBinaryFile(HELLO_PENTIUM));

    Prog *prog = project.getProg();

    const auto& m = *prog->getModuleList().begin();
    QVERIFY(m != nullptr);

    // create UserProc
    UserProc *proc = static_cast<UserProc *>(m->createFunction("test", Address(0x00000123)));

    // create CFG
    Cfg                    *cfg   = proc->getCFG();
    std::unique_ptr<RTLList> bbRTLs(new RTLList);
    bbRTLs->push_back(std::unique_ptr<RTL>(new RTL(Address(0x00000123), { })));

    BasicBlock *entryBB = cfg->createBB(BBType::Ret, std::move(bbRTLs));
    cfg->setEntryAndExitBB(entryBB);
    proc->setDecoded(); // We manually "decoded"

    // compute dataflow
    proc->decompile();

    // print cfg to a string
    QString     actual;
    QTextStream st(&actual);
    cfg->print(st);

    QString expected = QString(
            "Control Flow Graph:\n"
            "Ret BB:\n"
            "  in edges: \n"
            "  out edges: \n"
            "0x00000123\n\n"
        );

    QCOMPARE(actual, expected);
}


void StatementTest::testFlow()
{
    Project project;
    QVERIFY(project.loadBinaryFile(HELLO_PENTIUM));

    Prog *prog = project.getProg();

    // create UserProc
    UserProc    *proc = static_cast<UserProc *>(prog->createFunction(Address(0x00000123)));
    proc->setSignature(Signature::instantiate(Platform::PENTIUM, CallConv::C, "test"));

    Cfg *cfg   = proc->getCFG();

    Assign *a1 = new Assign(Location::regOf(24), std::make_shared<Const>(5));
    a1->setProc(proc);
    a1->setNumber(1);

    std::unique_ptr<RTLList> bbRTLs(new RTLList);
    bbRTLs->push_back(std::unique_ptr<RTL>(new RTL(Address(0x1000), { a1 })));

    BasicBlock *first = cfg->createBB(BBType::Fall, std::move(bbRTLs));

    ReturnStatement *rs = new ReturnStatement;
    rs->setNumber(2);
    Assign *a2 = new Assign(Location::regOf(24), std::make_shared<Const>(5));
    a2->setProc(proc);
    rs->addReturn(a2);

    bbRTLs.reset(new RTLList);
    bbRTLs->push_back(std::unique_ptr<RTL>(new RTL(Address(0x1010), { rs })));

    BasicBlock *ret = cfg->createBB(BBType::Ret, std::move(bbRTLs));
    QVERIFY(ret);

    // first was empty before
    first->addSuccessor(ret);
    ret->addPredecessor(first);
    cfg->setEntryAndExitBB(first); // Also sets exitBB; important!
    proc->setDecoded();

    // compute dataflow
    proc->decompile();

    // print cfg to a string
    QString     actual;
    QTextStream st(&actual);

    cfg->print(st);

    // The assignment to 5 gets propagated into the return, and the assignment
    // to r24 is removed
    QString expected =
        "Control Flow Graph:\n"
        "Fall BB:\n"
        "  in edges: \n"
        "  out edges: 0x00001010 \n"
        "0x00001000\n"
        "Ret BB:\n"
        "  in edges: 0x00001000(0x00001000) \n"
        "  out edges: \n"
        "0x00001010    1 RET *v* r24 := 5\n"
        "              Modifieds: \n"
        "              Reaching definitions: r24=5\n"
        "\n";

    compareStrings(actual, expected);

    // clean up
    delete a1;
}


void StatementTest::testKill()
{
    Project project;
    QVERIFY(project.loadBinaryFile(HELLO_PENTIUM));

    Prog *prog = project.getProg();

    // create UserProc
    QString  name  = "test";
    UserProc *proc = static_cast<UserProc *>(prog->createFunction(Address(0x00000123)));
    proc->setSignature(Signature::instantiate(Platform::PENTIUM, CallConv::C, name));

    // create CFG
    Cfg              *cfg   = proc->getCFG();

    Assign *e1     = new Assign(Location::regOf(24), Const::get(5));
    e1->setNumber(1);
    e1->setProc(proc);

    Assign *e2 = new Assign(Location::regOf(24), Const::get(6));
    e2->setNumber(2);
    e2->setProc(proc);

    std::unique_ptr<RTLList> bbRTLs(new RTLList);
    bbRTLs->push_back(std::unique_ptr<RTL>(new RTL(Address(0x1000), { e1, e2 })));
    BasicBlock *first = cfg->createBB(BBType::Fall, std::move(bbRTLs));

    ReturnStatement *rs = new ReturnStatement;
    rs->setNumber(3);

    Assign *e = new Assign(Location::regOf(24), Const::get(0));
    e->setProc(proc);
    rs->addReturn(e);

    bbRTLs.reset(new RTLList);
    bbRTLs->push_back(std::unique_ptr<RTL>(new RTL(Address(0x1010), { rs })));

    BasicBlock *ret = cfg->createBB(BBType::Ret, std::move(bbRTLs));
    first->addSuccessor(ret);
    ret->addPredecessor(first);
    cfg->setEntryAndExitBB(first);
    proc->setDecoded();

    // compute dataflow
    proc->decompile();

    // print cfg to a string
    QString     actual;
    QTextStream st(&actual);

    cfg->print(st);
    QString expected =
        "Control Flow Graph:\n"
        "Fall BB:\n"
        "  in edges: \n"
        "  out edges: 0x00001010 \n"
        "0x00001000\n"
        "Ret BB:\n"
        "  in edges: 0x00001000(0x00001000) \n"
        "  out edges: \n"
        "0x00001010    1 RET *v* r24 := 0\n"
        "              Modifieds: \n"
        "              Reaching definitions: r24=6\n\n";

    compareStrings(actual, expected);

    // clean up
    delete e1;
    delete e2;
}


void StatementTest::testUse()
{
    Project project;
    QVERIFY(project.loadBinaryFile(HELLO_PENTIUM));
    Prog *prog = project.getProg();

    UserProc    *proc = static_cast<UserProc *>(prog->createFunction(Address(0x00000123)));
    proc->setSignature(Signature::instantiate(Platform::PENTIUM, CallConv::C, "test"));

    Cfg *cfg   = proc->getCFG();

    Assign *a1 = new Assign(Location::regOf(24), Const::get(5));
    a1->setNumber(1);
    a1->setProc(proc);

    Assign *a2 = new Assign(Location::regOf(28), Location::regOf(24));
    a2->setNumber(2);
    a2->setProc(proc);

    std::unique_ptr<RTLList> bbRTLs(new RTLList);
    bbRTLs->push_back(std::unique_ptr<RTL>(new RTL(Address(0x1000), { a1, a2 })));
    BasicBlock *first = cfg->createBB(BBType::Fall, std::move(bbRTLs));

    ReturnStatement *rs = new ReturnStatement;
    rs->setNumber(3);
    Assign *a = new Assign(Location::regOf(28), Const::get(1000));
    a->setProc(proc);
    rs->addReturn(a);
    bbRTLs.reset(new RTLList);
    bbRTLs->push_back(std::unique_ptr<RTL>(new RTL(Address(0x1010), { rs })));

    BasicBlock *ret = cfg->createBB(BBType::Ret, std::move(bbRTLs));
    first->addSuccessor(ret);
    ret->addPredecessor(first);
    cfg->setEntryAndExitBB(first);
    proc->setDecoded();

    // compute dataflow
    proc->decompile();
    // print cfg to a string
    QString     actual;
    QTextStream st(&actual);
    cfg->print(st);

    QString expected =
        "Control Flow Graph:\n"
        "Fall BB:\n"
        "  in edges: \n"
        "  out edges: 0x00001010 \n"
        "0x00001000\n"
        "Ret BB:\n"
        "  in edges: 0x00001000(0x00001000) \n"
        "  out edges: \n"
        "0x00001010    1 RET *v* r28 := 1000\n"
        "              Modifieds: \n"
        "              Reaching definitions: r24=5,   r28=5\n\n";

    compareStrings(actual, expected);

    // clean up
    delete a1;
    delete a2;
}


void StatementTest::testUseOverKill()
{
    Project project;
    QVERIFY(project.loadBinaryFile(HELLO_PENTIUM));
    Prog *prog = project.getProg();

    UserProc *proc = static_cast<UserProc *>(prog->createFunction(Address(0x00000123)));
    proc->setSignature(Signature::instantiate(Platform::PENTIUM, CallConv::C, "test"));
    Cfg *cfg = proc->getCFG();

    Assign *e1 = new Assign(Location::regOf(24), Const::get(5));
    e1->setNumber(1);
    e1->setProc(proc);

    Assign *e2 = new Assign(Location::regOf(24), Const::get(6));
    e2->setNumber(2);
    e2->setProc(proc);

    Assign *e3 = new Assign(Location::regOf(28), Location::regOf(24));
    e3->setNumber(3);
    e3->setProc(proc);

    std::unique_ptr<RTLList> bbRTLs(new RTLList);
    bbRTLs->push_back(std::unique_ptr<RTL>(new RTL(Address(0x1000), { e1, e2, e3 })));
    BasicBlock *first = cfg->createBB(BBType::Fall, std::move(bbRTLs));

    ReturnStatement *rs = new ReturnStatement;
    rs->setNumber(4);
    Assign *e = new Assign(Location::regOf(24), Const::get(0));
    e->setProc(proc);
    rs->addReturn(e);

    bbRTLs.reset(new RTLList);
    bbRTLs->push_back(std::unique_ptr<RTL>(new RTL(Address(0x1010), { rs })));
    BasicBlock *ret = cfg->createBB(BBType::Ret, std::move(bbRTLs));

    first->addSuccessor(ret);
    ret->addPredecessor(first);
    cfg->setEntryAndExitBB(first);
    proc->setDecoded();

    // compute dataflow
    proc->decompile();

    // print cfg to a string
    QString     actual;
    QTextStream st(&actual);
    cfg->print(st);

    // compare it to expected
    QString expected =
        "Control Flow Graph:\n"
        "Fall BB:\n"
        "  in edges: \n"
        "  out edges: 0x00001010 \n"
        "0x00001000\n"
        "Ret BB:\n"
        "  in edges: 0x00001000(0x00001000) \n"
        "  out edges: \n"
        "0x00001010    1 RET *v* r24 := 0\n"
        "              Modifieds: \n"
        "              Reaching definitions: r24=6,   r28=6\n\n";

    compareStrings(actual, expected);

    // clean up
    delete e1;
    delete e2;
    delete e3;
}


void StatementTest::testUseOverBB()
{
    Project project;
    QVERIFY(project.loadBinaryFile(HELLO_PENTIUM));

    Prog *prog = project.getProg();

    // create UserProc
    UserProc *proc = static_cast<UserProc *>(prog->createFunction(Address(0x00001000)));
    Cfg *cfg       = proc->getCFG();

    Assign *a1 = new Assign(Location::regOf(24), Const::get(5));
    a1->setNumber(1);
    a1->setProc(proc);

    Assign *a2 = new Assign(Location::regOf(24), Const::get(6));
    a2->setNumber(2);
    a2->setProc(proc);

    std::unique_ptr<RTLList> bbRTLs(new RTLList);
    bbRTLs->push_back(std::unique_ptr<RTL>(new RTL(Address(0x1000), { a1, a2 })));
    BasicBlock *first = cfg->createBB(BBType::Fall, std::move(bbRTLs));

    Assign *a3  = new Assign(Location::regOf(28), Location::regOf(24));
    a3->setNumber(3);
    a3->setProc(proc);
    bbRTLs.reset(new RTLList);
    bbRTLs->push_back(std::unique_ptr<RTL>(new RTL(Address(0x1010), { a3 })));


    ReturnStatement *rs = new ReturnStatement;
    rs->setNumber(4);

    Assign *a = new Assign(Location::regOf(24), Const::get(0));
    a->setProc(proc);
    rs->addReturn(a);
    bbRTLs->push_back(std::unique_ptr<RTL>(new RTL(Address(0x00001012), { rs })));
    BasicBlock *ret = cfg->createBB(BBType::Ret, std::move(bbRTLs));

    first->addSuccessor(ret);
    ret->addPredecessor(first);
    cfg->setEntryAndExitBB(first);
    proc->setDecoded();

    // compute dataflow
    proc->decompile();

    // print cfg to a string
    QString     actual;
    QTextStream st(&actual);
    cfg->print(st);

    QString expected =
        "Control Flow Graph:\n"
        "Fall BB:\n"
        "  in edges: \n"
        "  out edges: 0x00001010 \n"
        "0x00001000\n"
        "Ret BB:\n"
        "  in edges: 0x00001000(0x00001000) \n"
        "  out edges: \n"
        "0x00001010\n"
        "0x00001012    1 RET *v* r24 := 0\n"
        "              Modifieds: \n"
        "              Reaching definitions: r24=6,   r28=6\n\n";

    compareStrings(actual, expected);

    // clean up
    delete a1;
    delete a2;
    delete a3;
}


void StatementTest::testUseKill()
{
    Project project;
    QVERIFY(project.loadBinaryFile(HELLO_PENTIUM));

    Prog *prog = project.getProg();

    UserProc    *proc = static_cast<UserProc *>(prog->createFunction(Address(0x00000123)));
    Cfg *cfg   = proc->getCFG();

    Assign *a1 = new Assign(Location::regOf(24), Const::get(5));
    a1->setNumber(1);
    a1->setProc(proc);

    Assign *a2 = new Assign(Location::regOf(24), Binary::get(opPlus, Location::regOf(24), Const::get(1)));
    a2->setNumber(2);
    a2->setProc(proc);

    std::unique_ptr<RTLList> bbRTLs(new RTLList);
    bbRTLs->push_back(std::unique_ptr<RTL>(new RTL(Address(0x1000), { a1, a2 })));
    BasicBlock *first = cfg->createBB(BBType::Fall, std::move(bbRTLs));

    ReturnStatement *rs = new ReturnStatement;
    rs->setNumber(3);
    Assign *a = new Assign(Location::regOf(24), Const::get(0));
    a->setProc(proc);
    rs->addReturn(a);
    bbRTLs.reset(new RTLList);
    bbRTLs->push_back(std::unique_ptr<RTL>(new RTL(Address(0x1010), { rs })));
    BasicBlock *ret = cfg->createBB(BBType::Ret, std::move(bbRTLs));

    first->addSuccessor(ret);
    ret->addPredecessor(first);
    cfg->setEntryAndExitBB(first);
    proc->setDecoded();

    // compute dataflow
    proc->decompile();

    // print cfg to a string
    QString     actual;
    QTextStream st(&actual);
    cfg->print(st);

    QString expected =
        "Control Flow Graph:\n"
        "Fall BB:\n"
        "  in edges: \n"
        "  out edges: 0x00001010 \n"
        "0x00001000\n"
        "Ret BB:\n"
        "  in edges: 0x00001000(0x00001000) \n"
        "  out edges: \n"
        "0x00001010    1 RET *v* r24 := 0\n"
        "              Modifieds: \n"
        "              Reaching definitions: r24=6\n\n";

    compareStrings(actual, expected);

    // clean up
    delete a1;
    delete a2;
}


void StatementTest::testEndlessLoop()
{
    //
    // BB1 -> BB2 _
    //       ^_____|

    Project project;
    QVERIFY(project.loadBinaryFile(HELLO_PENTIUM));
    Prog *prog = project.getProg();

    UserProc *proc = static_cast<UserProc *>(prog->createFunction(Address(0x00001000)));
    Cfg *cfg   = proc->getCFG();


    // r[24] := 5
    Assign *a1 = new Assign(Location::regOf(24), Const::get(5));
    a1->setProc(proc);
    std::unique_ptr<RTLList> bbRTLs(new RTLList);
    bbRTLs->push_back(std::unique_ptr<RTL>(new RTL(Address(0x1000), { a1 })));

    BasicBlock *first = cfg->createBB(BBType::Fall, std::move(bbRTLs));


    // r24 := r24 + 1
    Assign *a2 = new Assign(Location::regOf(24), Binary::get(opPlus, Location::regOf(24), Const::get(1)));
    a2->setProc(proc);
    bbRTLs.reset(new RTLList);
    bbRTLs->push_back(std::unique_ptr<RTL>(new RTL(Address(0x1010), { a2 })));

    BasicBlock *body = cfg->createBB(BBType::Oneway, std::move(bbRTLs));

    first->addSuccessor(body);
    body->addPredecessor(first);
    body->addSuccessor(body);
    body->addPredecessor(body);
    cfg->setEntryAndExitBB(first);
    proc->setDecoded();

    // compute dataflow
    proc->decompile();

    QString     actual;
    QTextStream st(&actual);

    // print cfg to a string
    cfg->print(st);

    // int i = 5; do { i++; } while (true);
    // TODO: is the phi really needed?
    QString expected = "Control Flow Graph:\n"
                       "Fall BB:\n"
                       "  in edges: \n"
                       "  out edges: 0x00001010 \n"
                       "0x00001000    1 *i32* r24 := 5\n"
                       "Oneway BB:\n"
                       "  in edges: 0x00001000(0x00001000) 0x00001010(0x00001010) \n"
                       "  out edges: 0x00001010 \n"
                       "0x00000000    2 *i32* r24 := phi{1 3}\n"
                       "0x00001010    3 *i32* r24 := r24{2} + 1\n"
                       "\n";

    compareStrings(actual, expected);
}


void StatementTest::testLocationSet()
{
    Location    rof(opRegOf, Const::get(12), nullptr); // r12
    Const&      theReg = *std::dynamic_pointer_cast<Const>(rof.getSubExp1());
    LocationSet ls;


    ls.insert(rof.clone()); // ls has r12
    theReg.setInt(8);
    ls.insert(rof.clone()); // ls has r8 r12
    theReg.setInt(31);
    ls.insert(rof.clone()); // ls has r8 r12 r31
    theReg.setInt(24);
    ls.insert(rof.clone()); // ls has r8 r12 r24 r31
    theReg.setInt(12);
    ls.insert(rof.clone()); // Note: r12 already inserted

    QCOMPARE(ls.size(), 4);
    theReg.setInt(8);
    auto ii = ls.begin();
    QVERIFY(rof == **ii); // First element should be r8

    theReg.setInt(12);
    SharedExp e = *(++ii);
    QVERIFY(rof == *e); // Second should be r12

    theReg.setInt(24);
    e = *(++ii);
    QVERIFY(rof == *e); // Next should be r24
    theReg.setInt(31);
    e = *(++ii);
    QVERIFY(rof == *e);                                                                      // Last should be r31

    Location mof(opMemOf, Binary::get(opPlus, Location::regOf(14), Const::get(4)), nullptr); // m[r14 + 4]
    ls.insert(mof.clone());                                                                  // ls should be r8 r12 r24 r31 m[r14 + 4]
    ls.insert(mof.clone());

    QCOMPARE(ls.size(), 5); // Should have 5 elements

    ii = --ls.end();
    QVERIFY(mof == **ii);   // Last element should be m[r14 + 4] now
    LocationSet ls2 = ls;
    SharedExp   e2  = *ls2.begin();
    QVERIFY(!(e2 == *ls.begin())); // Must be cloned
    QCOMPARE(ls2.size(), 5);

    theReg.setInt(8);
    QVERIFY(rof == **ls2.begin()); // First elements should compare equal

    theReg.setInt(12);
    e = *(++ls2.begin());          // Second element
    QVERIFY(rof == *e);            // ... should be r12

    Assign s10(Const::get(0), Const::get(0));
    Assign s20(Const::get(0), Const::get(0));
    s10.setNumber(10);
    s20.setNumber(20);

    std::shared_ptr<RefExp> r1 = RefExp::get(Location::regOf(8), &s10);
    std::shared_ptr<RefExp> r2 = RefExp::get(Location::regOf(8), &s20);
    ls.insert(r1); // ls now m[r14 + 4] r8 r12 r24 r31 r8{10} (not sure where r8{10} appears)

    QCOMPARE(ls.size(), 6);
    SharedExp dummy;
    QVERIFY(!ls.findDifferentRef(r1, dummy));
    QVERIFY(ls.findDifferentRef(r2, dummy));

    SharedExp r8 = Location::regOf(8);
    QVERIFY(!ls.containsImplicit(r8));

    std::shared_ptr<RefExp> r3(new RefExp(Location::regOf(8), nullptr));
    ls.insert(r3);
    QVERIFY(ls.containsImplicit(r8));
    ls.remove(r3);

    ImplicitAssign          zero(r8);
    std::shared_ptr<RefExp> r4(new RefExp(Location::regOf(8), &zero));
    ls.insert(r4);
    QVERIFY(ls.containsImplicit(r8));
}


void StatementTest::testWildLocationSet()
{
    Location rof12(opRegOf, Const::get(12), nullptr);
    Location rof13(opRegOf, Const::get(13), nullptr);
    Assign   a10, a20;

    a10.setNumber(10);
    a20.setNumber(20);
    std::shared_ptr<RefExp> r12_10(new RefExp(rof12.clone(), &a10));
    std::shared_ptr<RefExp> r12_20(new RefExp(rof12.clone(), &a20));
    std::shared_ptr<RefExp> r12_0(new RefExp(rof12.clone(), nullptr));
    std::shared_ptr<RefExp> r13_10(new RefExp(rof13.clone(), &a10));
    std::shared_ptr<RefExp> r13_20(new RefExp(rof13.clone(), &a20));
    std::shared_ptr<RefExp> r13_0(new RefExp(rof13.clone(), nullptr));
    std::shared_ptr<RefExp> r11_10(new RefExp(Location::regOf(11), &a10));
    std::shared_ptr<RefExp> r22_10(new RefExp(Location::regOf(22), &a10));

    LocationSet ls;
    ls.insert(r12_10);
    ls.insert(r12_20);
    ls.insert(r12_0);
    ls.insert(r13_10);
    ls.insert(r13_20);
    ls.insert(r13_0);

    std::shared_ptr<RefExp> wildr12(new RefExp(rof12.clone(), STMT_WILD));
    QVERIFY(ls.contains(wildr12));
    std::shared_ptr<RefExp> wildr13(new RefExp(rof13.clone(), STMT_WILD));
    QVERIFY(ls.contains(wildr13));
    std::shared_ptr<RefExp> wildr10(new RefExp(Location::regOf(10), STMT_WILD));
    QVERIFY(!ls.contains(wildr10));

    // Test findDifferentRef
    SharedExp x;
    QVERIFY(ls.findDifferentRef(r13_10, x));
    QVERIFY(ls.findDifferentRef(r13_20, x));
    QVERIFY(ls.findDifferentRef(r13_0, x));
    QVERIFY(ls.findDifferentRef(r12_10, x));
    QVERIFY(ls.findDifferentRef(r12_20, x));
    QVERIFY(ls.findDifferentRef(r12_0, x));

    // Next 4 should fail
    QVERIFY(!ls.findDifferentRef(r11_10, x));
    QVERIFY(!ls.findDifferentRef(r22_10, x));
    ls.insert(r11_10);
    ls.insert(r22_10);
    QVERIFY(!ls.findDifferentRef(r11_10, x));
    QVERIFY(!ls.findDifferentRef(r22_10, x));
}


void StatementTest::testRecursion()
{
    QSKIP("Disabled.");

    Project project;
    project.loadBinaryFile(HELLO_PENTIUM);

    Prog *prog = project.getProg();

    IFrontEnd *fe = new PentiumFrontEnd(project.getLoadedBinaryFile(), prog);
    prog->setFrontEnd(fe);

    UserProc *proc = new UserProc(Address::ZERO, "test", prog->getOrInsertModule("test"));
    Cfg *cfg   = proc->getCFG();

    // push bp
    // r28 := r28 + -4
    Assign *a1 = new Assign(Location::regOf(28), Binary::get(opPlus, Location::regOf(28), Const::get(-4)));

    // m[r28] := r29
    Assign *a2 = new Assign(Location::memOf(Location::regOf(28)), Location::regOf(29));
    std::unique_ptr<RTLList> bbRTLs(new RTLList);
    bbRTLs->push_back(std::unique_ptr<RTL>(new RTL(Address::ZERO, { a1, a2 })));
    bbRTLs.reset(); // ???

    // push arg+1
    // r28 := r28 + -4
    Assign *a3 = new Assign(Location::regOf(28), Binary::get(opPlus, Location::regOf(28), Const::get(-4)));

    // Reference our parameter. At esp+0 is this arg; at esp+4 is old bp;
    // esp+8 is return address; esp+12 is our arg
    // m[r28] := m[r28+12] + 1
    Assign *a4 = new Assign(
        Location::memOf(Location::regOf(28)),
        Binary::get(opPlus, Location::memOf(
                        Binary::get(opPlus, Location::regOf(28), Const::get(12))), Const::get(1)));

    a4->setProc(proc);
    bbRTLs.reset(new RTLList);
    bbRTLs->push_back(std::unique_ptr<RTL>(new RTL(Address(0x1002), { a3, a4 })));
    BasicBlock *first = cfg->createBB(BBType::Fall, std::move(bbRTLs));

    // The call BB
    bbRTLs.reset(new RTLList);

    // r28 := r28 + -4
    Assign *a5 = new Assign(Location::regOf(28), Binary::get(opPlus, Location::regOf(28), Const::get(-4)));
    // m[r28] := pc
    Assign *a6 = new Assign(Location::memOf(Location::regOf(28)), Terminal::get(opPC));
    // %pc := (%pc + 5) + 135893848
    Assign *a7 = new Assign(Terminal::get(opPC),
                   Binary::get(opPlus,
                               Binary::get(opPlus, Terminal::get(opPC), Const::get(5)),
                               Const::get(135893848)));
    a7->setProc(proc);

    CallStatement *c = new CallStatement;
    c->setDestProc(proc); // Just call self
    bbRTLs->push_back(std::unique_ptr<RTL>(new RTL(Address(0x0001), { a5, a6, a7, c })));
    BasicBlock *callbb = cfg->createBB(BBType::Call, std::move(bbRTLs));

    first->addSuccessor(callbb);
    callbb->addPredecessor(first);
    callbb->addSuccessor(callbb);
    callbb->addPredecessor(callbb);

    bbRTLs.reset(new RTLList);
    ReturnStatement *retStmt = new ReturnStatement;
    // This ReturnStatement requires the following two sets of semantics to pass the
    // tests for standard Pentium calling convention
    // pc = m[r28]
    a1 = new Assign(Terminal::get(opPC), Location::memOf(Location::regOf(28)));
    // r28 = r28 + 4
    a2 = new Assign(Location::regOf(28), Binary::get(opPlus, Location::regOf(28), Const::get(4)));

    bbRTLs->push_back(std::unique_ptr<RTL>(new RTL(Address(0x00000123), { retStmt, a1, a2 })));

    BasicBlock *ret = cfg->createBB(BBType::Ret, std::move(bbRTLs));
    callbb->addSuccessor(ret);
    ret->addPredecessor(callbb);
    cfg->setEntryAndExitBB(first);

    // decompile the "proc"
    prog->decompile();

    // print cfg to a string
    QString     actual;
    QTextStream st(&actual);
    cfg->print(st);

    QString expected = "Control Flow Graph:\n"
                       "Fall BB: reach in: \n"
                       "00000000 ** r[24] := 5   uses:    used by: ** r[24] := r[24] + 1, \n"
                       "00000000 ** r[24] := 5   uses:    used by: ** r[24] := r[24] + 1, \n"
                       "Call BB: reach in: ** r[24] := 5, ** r[24] := r[24] + 1, \n"
                       "00000001 ** r[24] := r[24] + 1   uses: ** r[24] := 5, "
                       "** r[24] := r[24] + 1,    used by: ** r[24] := r[24] + 1, \n"
                       "cfg reachExit: \n";
    QCOMPARE(actual, expected);
}


void StatementTest::testClone()
{
    Assign *a1 = new Assign(Location::regOf(8), Binary::get(opPlus, Location::regOf(9), Const::get(99)));
    Assign *a2 = new Assign(IntegerType::get(16, 1), Location::get(opParam, Const::get("x"), nullptr),
                            Location::get(opParam, Const::get("y"), nullptr));
    Assign *a3 = new Assign(IntegerType::get(16, -1), Location::get(opParam, Const::get("z"), nullptr),
                            Location::get(opParam, Const::get("q"), nullptr));

    Statement *c1 = a1->clone();
    Statement *c2 = a2->clone();
    Statement *c3 = a3->clone();

    QString     original, clone;
    QTextStream original_st(&original);
    QTextStream clone_st(&clone);

    a1->print(original_st);
    delete a1; // And c1 should still stand!
    c1->print(clone_st);
    a2->print(original_st);
    c2->print(clone_st);
    a3->print(original_st);
    c3->print(clone_st);

    QString expected("   0 *v* r8 := r9 + 99"
                     "   0 *i16* x := y"
                     "   0 *u16* z := q");

    QCOMPARE(original, expected);
    QCOMPARE(clone, expected);

    delete a2;
    delete a3;
    delete c1;
    delete c2;
    delete c3;
}


void StatementTest::testIsAssign()
{
    QString     actual;
    QTextStream st(&actual);
    // r2 := 99
    Assign a(Location::regOf(2), Const::get(99));

    a.print(st);
    QString expected("   0 *v* r2 := 99");

    QCOMPARE(expected, actual);
    QVERIFY(a.isAssign());

    CallStatement c;
    QVERIFY(!c.isAssign());
}


void StatementTest::testIsFlagAssgn()
{
    // FLAG addFlags(r2 , 99)
    Assign fc(Terminal::get(opFlags),
              Binary::get(opFlagCall, Const::get("addFlags"),
                          Binary::get(opList, Location::regOf(2), Const::get(99))));
    CallStatement   call;
    BranchStatement br;
    Assign          as(Location::regOf(9), Binary::get(opPlus, Location::regOf(10), Const::get(4)));

    QString     actual;
    QString     expected("   0 *v* %flags := addFlags( r2, 99 )");
    QTextStream ost(&actual);

    fc.print(ost);
    QCOMPARE(expected, actual);

    QVERIFY(fc.isFlagAssign());
    QVERIFY(!call.isFlagAssign());
    QVERIFY(!br.isFlagAssign());
    QVERIFY(!as.isFlagAssign());
}


void StatementTest::testAddUsedLocsAssign()
{
    // m[r28-4] := m[r28-8] * r26
    Assign a(Location::memOf(Binary::get(opMinus, Location::regOf(28), Const::get(4))),
                           Binary::get(opMult, Location::memOf(Binary::get(opMinus, Location::regOf(28), Const::get(8))),
                                       Location::regOf(26)));

    a.setNumber(1);
    LocationSet l;
    a.addUsedLocs(l);

    QString     actual;
    QTextStream ost(&actual);
    l.print(ost);
    QString expected = "r26,\tr28,\tm[r28 - 8]";
    QCOMPARE(expected, actual);

    l.clear();
    GotoStatement g;
    g.setNumber(55);
    g.setDest(Location::memOf(Location::regOf(26)));
    g.addUsedLocs(l);

    actual   = "";
    expected = "r26,\tm[r26]";
    l.print(ost);

    QCOMPARE(expected, actual);
}


void StatementTest::testAddUsedLocsBranch()
{
    // BranchStatement with dest m[r26{99}]{55}, condition %flags
    GotoStatement g;

    g.setNumber(55);
    LocationSet     l;
    BranchStatement b;
    b.setNumber(99);
    b.setDest(RefExp::get(Location::memOf(RefExp::get(Location::regOf(26), &b)), &g));
    b.setCondExpr(Terminal::get(opFlags));
    b.addUsedLocs(l);

    QString     actual;
    QString     expected("r26{99},\tm[r26{99}]{55},\t%flags");
    QTextStream ost(&actual);
    l.print(ost);

    QCOMPARE(actual, expected);
}


void StatementTest::testAddUsedLocsCase()
{
    // CaseStatement with dest = m[r26], switchVar = m[r28 - 12]
    LocationSet   l;
    CaseStatement c;

    c.setDest(Location::memOf(Location::regOf(26)));
    SwitchInfo si;
    si.switchExp = Location::memOf(Binary::get(opMinus, Location::regOf(28), Const::get(12)));
    c.setSwitchInfo(&si);
    c.addUsedLocs(l);

    QString expected("r26,\tr28,\tm[r28 - 12],\tm[r26]");
    QString actual;
    QTextStream ost(&actual);
    l.print(ost);

    QCOMPARE(actual, expected);
}


void StatementTest::testAddUsedLocsCall()
{
    // CallStatement with dest = m[r26], params = m[r27], r28{55}, defines r31, m[r24]
    LocationSet   l;
    GotoStatement g;

    g.setNumber(55);
    CallStatement ca;
    ca.setDest(Location::memOf(Location::regOf(26)));
    StatementList argl;
    argl.append(new Assign(Location::regOf(8), Location::memOf(Location::regOf(27))));
    argl.append(new Assign(Location::regOf(9), RefExp::get(Location::regOf(28), &g)));
    ca.setArguments(argl);
    ca.addDefine(new ImplicitAssign(Location::regOf(31)));
    ca.addDefine(new ImplicitAssign(Location::regOf(24)));
    ca.addUsedLocs(l);

    QString expected("r26,\tr27,\tm[r26],\tm[r27],\tr28{55}");
    QString actual;
    QTextStream ost(&actual);
    l.print(ost);

    QCOMPARE(actual, expected);
}


void StatementTest::testAddUsedLocsReturn()
{
    // ReturnStatement with returns r31, m[r24], m[r25]{55} + r[26]{99}]
    LocationSet   l;
    GotoStatement g;
    g.setNumber(55);

    BranchStatement b;
    b.setNumber(99);

    ReturnStatement r;
    r.addReturn(new Assign(Location::regOf(31), Const::get(100)));
    r.addReturn(new Assign(Location::memOf(Location::regOf(24)), Const::get(0)));
    r.addReturn(new Assign(
                     Location::memOf(Binary::get(opPlus, RefExp::get(Location::regOf(25), &g), RefExp::get(Location::regOf(26), &b))),
                     Const::get(5)));
    r.addUsedLocs(l);

    QString     actual;
    QTextStream ost(&actual);
    l.print(ost);

    QString expected("r24,\tr25{55},\tr26{99}");
    QCOMPARE(expected, actual);
}


void StatementTest::testAddUsedLocsBool()
{
    // Boolstatement with condition m[r24] = r25, dest m[r26]
    LocationSet l;
    BoolAssign  bs(8);

    bs.setCondExpr(Binary::get(opEquals, Location::memOf(Location::regOf(24)), Location::regOf(25)));
    std::list<Statement *> stmts;
    stmts.push_back(new Assign(Location::memOf(Location::regOf(26)), Terminal::get(opNil)));

    bs.setLeftFromList(stmts);
    bs.addUsedLocs(l);

    QString     actual;
    QTextStream ost(&actual);
    l.print(ost);

    QString expected("r24,\tr25,\tr26,\tm[r24]");
    QCOMPARE(actual, expected);

    qDeleteAll(stmts);
    l.clear();

    // m[local21 + 16] := phi{0, 372}
    SharedExp base = Location::memOf(Binary::get(opPlus, Location::local("local21", nullptr), Const::get(16)));
    Assign    s372(base, Const::get(0));
    s372.setNumber(372);

    PhiAssign pa(base);
    pa.putAt(nullptr, nullptr, base); // 0
    pa.putAt(nullptr, &s372, base);   // 1
    pa.addUsedLocs(l);
    // Note: phis were not considered to use blah if they ref m[blah], so local21 was not considered used

    actual   = "";
    expected = "m[local21 + 16]{372},\tlocal21";
    l.print(ost);

    QCOMPARE(actual, expected);

    // m[r28{-} - 4] := -
    l.clear();
    ImplicitAssign ia(Location::memOf(Binary::get(opMinus,
                                                  RefExp::get(Location::regOf(28), nullptr),
                                                  Const::get(4))));

    ia.addUsedLocs(l);
    actual   = "";
    expected = "r28{-}";
    l.print(ost);
    QCOMPARE(actual, expected);
}


void StatementTest::testSubscriptVars()
{
    SharedExp srch = Location::regOf(28);
    Assign    s9(Const::get(0), Const::get(0));

    s9.setNumber(9);

    // m[r28-4] := m[r28-8] * r26
    Assign a(Location::memOf(Binary::get(opMinus, Location::regOf(28), Const::get(4))),
                           Binary::get(opMult, Location::memOf(Binary::get(opMinus, Location::regOf(28), Const::get(8))),
                                       Location::regOf(26)));
    a.setNumber(1);
    QString     actual;
    QTextStream ost(&actual);

    a.subscriptVar(srch, &s9);
    ost << &a;
    QString expected = "   1 *v* m[r28{9} - 4] := m[r28{9} - 8] * r26";
    QCOMPARE(expected, actual);

    // GotoStatement
    GotoStatement g;
    g.setNumber(55);
    g.setDest(Location::regOf(28));
    g.subscriptVar(srch, &s9);

    actual   = "";
    expected = "  55 GOTO r28{9}";
    ost << &g;

    QCOMPARE(actual, expected);

    // BranchStatement with dest m[r26{99}]{55}, condition %flags
    BranchStatement b;
    b.setNumber(99);
    SharedExp srchb = Location::memOf(RefExp::get(Location::regOf(26), &b));
    b.setDest(RefExp::get(srchb, &g));
    b.setCondExpr(Terminal::get(opFlags));

    b.subscriptVar(srchb, &s9); // Should be ignored now: new behaviour
    b.subscriptVar(Terminal::get(opFlags), &g);

    actual   = "";
    expected = "  99 BRANCH m[r26{99}]{55}, condition equals\n"
               "High level: %flags{55}";
    ost << &b;
    QCOMPARE(actual, expected);

    // CaseStatement with dest = m[r26], switchVar = m[r28 - 12]
    CaseStatement c1;
    c1.setDest(Location::memOf(Location::regOf(26)));
    SwitchInfo si;
    si.switchExp = Location::memOf(Binary::get(opMinus, Location::regOf(28), Const::get(12)));
    c1.setSwitchInfo(&si);

    c1.subscriptVar(srch, &s9);

    actual   = "";
    expected = "   0 SWITCH(m[r28{9} - 12])\n";
    ost << &c1;
    QCOMPARE(actual, expected);

    // CaseStatement (before recog) with dest = r28, switchVar is nullptr
    CaseStatement c2;
    c2.setDest(Location::regOf(28));
    c2.setSwitchInfo(nullptr);

    c2.subscriptVar(srch, &s9);
    actual   = "";
    expected = "   0 CASE [r28{9}]";
    ost << &c2;
    QCOMPARE(expected, actual);

    // CallStatement with dest = m[r26], params = m[r27], r28, defines r28, m[r28]
    CallStatement ca;
    ca.setDest(Location::memOf(Location::regOf(26)));
    StatementList argl;

//    BinaryFile bf(QByteArray{}, nullptr);
    Prog   *prog = new Prog("testSubscriptVars", nullptr);
    Module *mod  = prog->getModuleForSymbol("test");

    argl.append(new Assign(Location::memOf(Location::regOf(27)), Const::get(1)));
    argl.append(new Assign(Location::regOf(28), Const::get(2)));
    ca.setArguments(argl);
    ca.addDefine(new ImplicitAssign(Location::regOf(28)));
    ca.addDefine(new ImplicitAssign(Location::memOf(Location::regOf(28))));

    ReturnStatement retStmt;
    UserProc destProc(Address(0x2000), "dest", mod);
    ca.setDestProc(&destProc);    // Must have a dest to be non-childless
    ca.setCalleeReturn(&retStmt); // So it's not a childless call, and we can see the defs and params
    ca.subscriptVar(srch, &s9);

    actual   = "";
    expected = "   0 {*v* r28, *v* m[r28]} := CALL dest(\n"
               "                *v* m[r27] := 1\n"
               "                *v* r28 := 2\n"
               "              )\n"
               "              Reaching definitions: \n"
               "              Live variables: "; // ? No newline?
    ost << &ca;
    QCOMPARE(expected, actual);

    argl.clear();

    // CallStatement with dest = r28, params = m[r27], r29, defines r31, m[r31]
    CallStatement ca2;
    ca2.setDest(Location::regOf(28));
    argl.append(new Assign(Location::memOf(Location::regOf(27)), Const::get(1)));
    argl.append(new Assign(Location::regOf(29), Const::get(2)));
    ca2.setArguments(argl);
    ca2.addDefine(new ImplicitAssign(Location::regOf(31)));
    ca2.addDefine(new ImplicitAssign(Location::memOf(Location::regOf(31))));
    ReturnStatement retStmt2;
    UserProc dest2(Address(0x2000), "dest", mod);
    ca2.setDestProc(&dest2);        // Must have a dest to be non-childless
    ca2.setCalleeReturn(&retStmt2); // So it's not a childless call, and we can see the defs and params
    ca2.subscriptVar(srch, &s9);

    actual   = "";
    expected = "   0 {*v* r31, *v* m[r31]} := CALL dest(\n"
               "                *v* m[r27] := 1\n"
               "                *v* r29 := 2\n"
               "              )\n"
               "              Reaching definitions: \n"
               "              Live variables: ";
    ost << &ca2;

    QCOMPARE(actual, expected);
    argl.clear();

    // ReturnStatement with returns r28, m[r28], m[r28]{55} + r[26]{99}]
    // FIXME: shouldn't this test have some propagation? Now, it seems it's just testing the print code!
    ReturnStatement r;
    r.addReturn(new Assign(Location::regOf(28), Const::get(1000)));
    r.addReturn(new Assign(Location::memOf(Location::regOf(28)), Const::get(2000)));
    r.addReturn(new Assign(
                     Location::memOf(Binary::get(opPlus, RefExp::get(Location::regOf(28), &g),
                                                 RefExp::get(Location::regOf(26), &b))),
                     Const::get(100)));

    r.subscriptVar(srch, &s9); // New behaviour: gets ignored now

    actual   = "";
    expected = "   0 RET *v* r28 := 1000,   *v* m[r28{9}] := 0x7d0,   *v* m[r28{55} + r26{99}] := 100\n"
               "              Modifieds: \n"
               "              Reaching definitions: ";
    ost << &r;
    QCOMPARE(actual, expected);

    // Boolstatement with condition m[r28] = r28, dest m[r28]
    BoolAssign bs(8);
    bs.setCondExpr(Binary::get(opEquals, Location::memOf(Location::regOf(28)), Location::regOf(28)));
    bs.setLeft(Location::memOf(Location::regOf(28)));

    bs.subscriptVar(srch, &s9);

    actual   = "";
    expected = "   0 BOOL m[r28{9}] := CC(equals)\n"
               "High level: m[r28{9}] = r28{9}\n";
    ost << &bs;
    QCOMPARE(actual, expected);

    delete prog;
}


void StatementTest::testBypass()
{
    QSKIP("Disabled.");

    Project project;
    QVERIFY(project.loadBinaryFile(GLOBAL1_PENTIUM));

    Prog *prog = project.getProg();
    IFrontEnd *fe = new PentiumFrontEnd(project.getLoadedBinaryFile(), prog);

    Type::clearNamedTypes();
    prog->setFrontEnd(fe);

    fe->decode(true);                   // Decode main
    fe->decode(Address::INVALID); // Decode anything undecoded

    bool    gotMain;
    Address addr = fe->getMainEntryPoint(gotMain);
    QVERIFY(addr != Address::INVALID);

    UserProc *proc = static_cast<UserProc *>(prog->findFunction("foo2"));
    QVERIFY(proc != nullptr);

    proc->promoteSignature(); // Make sure it's a PentiumSignature (needed for bypassing)

    PassManager::get()->executePass(PassID::StatementInit, proc);
    PassManager::get()->executePass(PassID::Dominators, proc);

    // Number the statements
    proc->numberStatements();
    PassManager::get()->executePass(PassID::BlockVarRename, proc);

    // Find various needed statements
    StatementList stmts;
    proc->getStatements(stmts);
    StatementList::iterator it = stmts.begin();

    while (it != stmts.end() && !(*it)->isCall()) {
        ++it;
    }
    QVERIFY(it != stmts.end());

    CallStatement *call = static_cast<CallStatement *>(*it); // Statement 18, a call to printf
    call->setDestProc(proc);                    // A recursive call

    Statement *s20 = *std::next(it, 2); // Statement 20
    QVERIFY(s20->getKind() == StmtType::Assign);

    QString     actual;
    QTextStream ost(&actual);
    ost << s20;

    // TODO ???
    QString expected = "20 *32* r28 := r28{15} - 16";

    QCOMPARE(actual, expected);

    // FIXME: Ugh. Somehow, statement 20 has already bypassed the call, and incorrectly from what I can see - MVE
    s20->bypass();        // r28 should bypass the call

    actual = "";
    ost << s20;

    expected = "  20 *32* r28 := r28{-} - 16";
    QCOMPARE(actual, expected);

    delete prog;
}


void StatementTest::testStripSizes()
{
    // *v* r24 := m[zfill(8,32,local5) + param6]*8**8* / 16
    // The double size casting happens as a result of substitution
    SharedExp lhs = Location::regOf(24);
    SharedExp rhs = Binary::get(
        opDiv,
        Binary::get(opSize,
                    Const::get(8),
                    Binary::get(opSize, Const::get(8),
                                Location::memOf(Binary::get(opPlus,
                                                            std::make_shared<Ternary>(opZfill,
                                                                                      Const::get(8),
                                                                                      Const::get(32),
                                                                                      Location::local("local5", nullptr)),
                                                            Location::local("param6", nullptr))))),
        Const::get(16));

    Statement *s = new Assign(lhs, rhs);

    s->stripSizes();
    QString     expected("   0 *v* r24 := m[zfill(8,32,local5) + param6] / 16");
    QString     actual;
    QTextStream ost(&actual);
    ost << s;
    QCOMPARE(actual, expected);

    delete s;
}


void StatementTest::testFindConstants()
{
    Assign a(Location::regOf(24), Binary::get(opPlus, Const::get(3), Const::get(4)));

    std::list<std::shared_ptr<Const>> lc;
    a.findConstants(lc);

    QString     actual;
    QTextStream ost(&actual);

    for (auto it = lc.begin(); it != lc.end();) {
        ost << *it;

        if (++it != lc.end()) {
            ost << ", ";
        }
    }

    QCOMPARE(actual, QString("3, 4"));
}


QTEST_MAIN(StatementTest)
