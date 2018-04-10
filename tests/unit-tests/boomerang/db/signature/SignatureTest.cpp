#pragma region License
/*
 * This file is part of the Boomerang Decompiler.
 *
 * See the file "LICENSE.TERMS" for information on usage and
 * redistribution of this file, and for a DISCLAIMER OF ALL
 * WARRANTIES.
 */
#pragma endregion License
#include "SignatureTest.h"


#include "boomerang/db/exp/Binary.h"
#include "boomerang/db/exp/Const.h"
#include "boomerang/db/exp/Location.h"
#include "boomerang/db/exp/RefExp.h"
#include "boomerang/db/signature/Signature.h"
#include "boomerang/db/statements/Assign.h"
#include "boomerang/type/type/IntegerType.h"
#include "boomerang/type/type/PointerType.h"
#include "boomerang/type/type/VoidType.h"
#include "boomerang/util/StatementList.h"


void SignatureTest::testClone()
{
    std::shared_ptr<Signature> sig(new Signature("test"));
    sig->addParameter("firstParam", Location::regOf(26), IntegerType::get(32, 1));
    sig->addReturn(IntegerType::get(32, 1), Location::regOf(24));

    std::shared_ptr<Signature> cloned = sig->clone();
    QCOMPARE(cloned->getName(), QString("test"));
    QCOMPARE(cloned->getNumParams(), 1);
    QCOMPARE(cloned->getParamName(0), QString("firstParam"));
    QCOMPARE(cloned->getNumReturns(), 1);
    QVERIFY(*cloned->getReturnType(0) == *IntegerType::get(32, 1));
}


void SignatureTest::testCompare()
{
    Signature sig1("test1");
    Signature sig2("test2");
    QVERIFY(sig1 == sig2);

    sig1.addParameter(Location::regOf(26));
    QVERIFY(sig1 != sig2);

    sig2.addParameter(Location::regOf(24));
    QVERIFY(sig1 != sig2); // different paarameters

    sig2.addParameter(Location::regOf(26));
    sig1.addParameter(Location::regOf(24));
    QVERIFY(sig1 != sig2); // swapped parameters

    sig1.removeParameter(0);
    sig1.removeParameter(0);
    sig2.removeParameter(0);
    sig2.removeParameter(0);

    QVERIFY(sig1 == sig2);

    sig1.addReturn(VoidType::get(), Location::regOf(28));
    QVERIFY(sig1 != sig2);
    sig2.addReturn(IntegerType::get(32, 1), Location::regOf(25));
    QVERIFY(sig1 != sig2);
}


void SignatureTest::testAddReturn()
{
    Signature sig("test");
    sig.addReturn(IntegerType::get(32, 1), Location::regOf(24));
    QVERIFY(*sig.getReturnExp(0) == *Location::regOf(24));
}


void SignatureTest::testGetReturnExp()
{
    Signature sig("test");

    sig.addReturn(Location::regOf(24));
    QVERIFY(*sig.getReturnExp(0) == *Location::regOf(24));
}


void SignatureTest::testGetReturnType()
{
    Signature sig("test");

    sig.addReturn(Location::regOf(24));
    QVERIFY(*sig.getReturnType(0) == *PointerType::get(VoidType::get()));

    sig.addReturn(IntegerType::get(32, 1), Location::regOf(25));
    QVERIFY(*sig.getReturnType(1) == *IntegerType::get(32, 1));
}


void SignatureTest::testGetNumReturns()
{
    Signature sig("test");
    QCOMPARE(sig.getNumReturns(), 0);

    sig.addReturn(Location::regOf(24));
    QCOMPARE(sig.getNumReturns(), 1);
}


void SignatureTest::testFindReturn()
{
    Signature sig("test");
    QCOMPARE(sig.findReturn(nullptr), -1);

    sig.addReturn(IntegerType::get(32, 1), Location::regOf(24));
    QCOMPARE(sig.findReturn(Location::regOf(24)), 0);
    QCOMPARE(sig.findReturn(Location::regOf(25)), -1);
}


void SignatureTest::testAddParameter()
{
    Signature sig("test");

    sig.addParameter(Location::regOf(25));
    QCOMPARE(sig.getNumParams(), 1);
    QVERIFY(*sig.getParamType(0) == *VoidType::get());

    sig.addParameter(Location::regOf(25), IntegerType::get(32, 1));
    QCOMPARE(sig.getNumParams(), 2);
    QVERIFY(*sig.getParamType(1) == *IntegerType::get(32, 1));

    // test parameter name collision detection
    sig.setParamName(1, "param3");
    sig.addParameter("", Location::regOf(27)); // name = "param3" (taken) -> "param4"
    QCOMPARE(sig.getParamName(2), QString("param4"));
}


void SignatureTest::testRemoveParameter()
{
    Signature sig("test");

    // verify it does not crash
    sig.removeParameter(nullptr);
    QCOMPARE(sig.getNumParams(), 0);

    sig.removeParameter(0);
    QCOMPARE(sig.getNumParams(), 0);

    sig.addParameter(Location::regOf(25));
    sig.removeParameter(0);
    QCOMPARE(sig.getNumParams(), 0);

    sig.addParameter(Location::regOf(25), IntegerType::get(32, 1));
    sig.addParameter(Location::regOf(26));
    sig.removeParameter(Location::regOf(25));
    QCOMPARE(sig.getNumParams(), 1);
    QVERIFY(*sig.getParamExp(0) == *Location::regOf(26));
}


void SignatureTest::testSetNumParams()
{
    Signature sig("test");

    sig.setNumParams(0);
    QCOMPARE(sig.getNumParams(), 0);

    sig.addParameter("foo", Location::regOf(25));
    sig.addParameter("bar", Location::regOf(24));

    sig.setNumParams(1);
    QCOMPARE(sig.getNumParams(), 1);
}


void SignatureTest::testGetParamName()
{
    Signature sig("test");

    sig.addParameter("testParam", Location::regOf(25), VoidType::get());
    QCOMPARE(sig.getParamName(0), QString("testParam"));
}


void SignatureTest::testGetParamExp()
{
    Signature sig("test");

    sig.addParameter(Location::regOf(25));
    QVERIFY(*sig.getParamExp(0) == *Location::regOf(25));
}


void SignatureTest::testGetParamType()
{
    Signature sig("test");

    QVERIFY(sig.getParamType(0) == nullptr);

    sig.addParameter(Location::regOf(25), IntegerType::get(32, 1));
    QVERIFY(*sig.getParamType(0) == *IntegerType::get(32, 1));
}


void SignatureTest::testGetParamBoundMax()
{
    Signature sig("test");
    QCOMPARE(sig.getParamBoundMax(0), QString());

    sig.addParameter(Location::regOf(25), IntegerType::get(32, 1));
    QCOMPARE(sig.getParamBoundMax(0), QString());

    sig.addParameter("testParam", Location::regOf(26), IntegerType::get(32, 1), "r25");
    QCOMPARE(sig.getParamBoundMax(1), QString("r25"));
}


void SignatureTest::testSetParamType()
{
    Signature sig("test");

    sig.addParameter("testParam", Location::regOf(25), IntegerType::get(32, 1));
    sig.setParamType(0, VoidType::get());
    QVERIFY(*sig.getParamType(0) == *VoidType::get());

    sig.setParamType("testParam", IntegerType::get(32, 0));
    QVERIFY(*sig.getParamType(0) == *IntegerType::get(32, 0));
}


void SignatureTest::testFindParam()
{
    Signature sig("test");
    QCOMPARE(sig.findParam(Location::regOf(24)), -1);
    QCOMPARE(sig.findParam("testParam"), -1);

    sig.addParameter("testParam", Location::regOf(25), IntegerType::get(32, 1));
    QCOMPARE(sig.findParam(Location::regOf(25)), 0);
    QCOMPARE(sig.findParam(Location::regOf(24)), -1);
    QCOMPARE(sig.findParam("testParam"), 0);
    QCOMPARE(sig.findParam("Foo"), -1);
}


void SignatureTest::testRenameParam()
{
    Signature sig("test");
    QVERIFY(!sig.renameParam("", ""));

    sig.addParameter("testParam", Location::regOf(25));
    QVERIFY(sig.renameParam("testParam", ""));
    QCOMPARE(sig.getParamName(0), QString());

    QVERIFY(sig.renameParam("", ""));
    QVERIFY(sig.renameParam("", "foo"));
    QVERIFY(!sig.renameParam("bar", "baz"));
    QCOMPARE(sig.getParamName(0), QString("foo"));
}


void SignatureTest::testGetArgumentExp()
{
    Signature sig("test");

    sig.addParameter(Location::regOf(25));
    QVERIFY(*sig.getArgumentExp(0) == *Location::regOf(25));
}


void SignatureTest::testEllipsis()
{
    Signature sig("test");

    QVERIFY(!sig.hasEllipsis());
    sig.setHasEllipsis(true);
    QVERIFY(sig.hasEllipsis());
    sig.setHasEllipsis(false);
    QVERIFY(!sig.hasEllipsis());
}


void SignatureTest::testIsNoReturn()
{
    Signature sig("test");
    QVERIFY(!sig.isNoReturn());
}


void SignatureTest::testIsPromoted()
{
    Signature sig("test");
    QVERIFY(!sig.isPromoted());
}


void SignatureTest::testPromote()
{
    QSKIP("Not implemented.");
}


void SignatureTest::testGetStackRegister()
{
    Signature sig("test");
    QCOMPARE(sig.getStackRegister(), -1);
}


void SignatureTest::testIsStackLocal()
{
    Signature sig("test");

    QVERIFY(sig.isStackLocal(28, Location::memOf(Location::regOf(28))));
    QVERIFY(!sig.isStackLocal(28, Location::regOf(28)));

    SharedExp spPlus4  = Binary::get(opPlus, Location::regOf(28), Const::get(4));
    SharedExp spMinus4 = Binary::get(opMinus, Location::regOf(28), Const::get(4));
    QVERIFY(!sig.isStackLocal(28, Location::memOf(spPlus4)));
    QVERIFY(sig.isStackLocal(28, Location::memOf(spMinus4)));

    spPlus4  = Binary::get(opMinus, Location::regOf(28), Const::get(-4));
    spMinus4 = Binary::get(opPlus, Location::regOf(28), Const::get(-4));
    QVERIFY(!sig.isStackLocal(28, Location::memOf(spPlus4)));
    QVERIFY(sig.isStackLocal(28, Location::memOf(spMinus4)));

    // Check if the subscript is ignored correctly
    QVERIFY(!sig.isStackLocal(28, RefExp::get(Location::memOf(spPlus4), nullptr)));
    QVERIFY(sig.isStackLocal(28, RefExp::get(Location::memOf(spMinus4), nullptr)));

    SharedExp spMinusPi = Binary::get(opMinus, Location::regOf(28), Const::get(3.14156));
    QVERIFY(!sig.isStackLocal(28, Location::memOf(spMinusPi)));
}


void SignatureTest::testIsAddrOfStackLocal()
{
    Signature sig("test");

    QVERIFY(sig.isAddrOfStackLocal(28, Location::regOf(28)));
    QVERIFY(!sig.isAddrOfStackLocal(28, Location::memOf(Location::regOf(28))));

    SharedExp spPlus4  = Binary::get(opPlus, Location::regOf(28), Const::get(4));
    SharedExp spMinus4 = Binary::get(opPlus, Location::regOf(28), Const::get(-4));
    QVERIFY(!sig.isAddrOfStackLocal(28, spPlus4));
    QVERIFY(sig.isAddrOfStackLocal(28, spMinus4));

    spPlus4  = Binary::get(opMinus, Location::regOf(28), Const::get(-4));
    spMinus4 = Binary::get(opMinus, Location::regOf(28), Const::get(4));
    QVERIFY(!sig.isAddrOfStackLocal(28, spPlus4));
    QVERIFY(sig.isAddrOfStackLocal(28, spMinus4));

    SharedExp spMinusPi = Binary::get(opMinus, Location::regOf(28), Const::get(3.14156));
    QVERIFY(!sig.isAddrOfStackLocal(28, spMinusPi));

    // m[sp{4} - 10] is not a stack local
    Assign asgn(Location::regOf(28), Location::regOf(24));
    asgn.setNumber(4);

    SharedExp sp4Minus10 = Binary::get(opMinus, RefExp::get(Location::regOf(28), &asgn), Const::get(10));
    QVERIFY(!sig.isAddrOfStackLocal(28, sp4Minus10));

    // verify a[...] and m[...] cancel out
    QVERIFY(sig.isAddrOfStackLocal(28, Unary::get(opAddrOf, Location::memOf(spMinus4))));
}


void SignatureTest::testIsLocalOffsetNegative()
{
    Signature sig("test");
    QVERIFY(sig.isLocalOffsetNegative());
}


void SignatureTest::testIsLocalOffsetPositive()
{
    Signature sig("test");
    QVERIFY(!sig.isLocalOffsetPositive());
}


void SignatureTest::testIsOpCompatStackLocal()
{
    Signature sig("test");

    QVERIFY(sig.isOpCompatStackLocal(opMinus));
    QVERIFY(!sig.isOpCompatStackLocal(opPlus));
    QVERIFY(!sig.isOpCompatStackLocal(opAddrOf)); // neither plus nor minus
}


void SignatureTest::testGetProven()
{
    Signature sig("test");
    QVERIFY(sig.getProven(SharedExp()) == nullptr);
}


void SignatureTest::testIsPreserved()
{
    Signature sig("test");
    QVERIFY(!sig.isPreserved(SharedExp()));
}


void SignatureTest::testGetLibraryDefines()
{
    Signature sig("test");

    StatementList stmts;
    sig.getLibraryDefines(stmts);
    QVERIFY(stmts.empty());
}


void SignatureTest::testGetABIDefines()
{
    StatementList defs;

    QVERIFY(Signature::getABIDefines(Machine::PENTIUM, defs));
    QVERIFY(defs.size() == 3);
    QVERIFY(defs.findOnLeft(Location::regOf(24)) != nullptr);
    QVERIFY(defs.findOnLeft(Location::regOf(25)) != nullptr);
    QVERIFY(defs.findOnLeft(Location::regOf(26)) != nullptr);
    qDeleteAll(defs);
    defs.clear();

    QVERIFY(Signature::getABIDefines(Machine::SPARC, defs));
    QVERIFY(defs.size() == 7);
    QVERIFY(defs.findOnLeft(Location::regOf( 8)) != nullptr);
    QVERIFY(defs.findOnLeft(Location::regOf( 9)) != nullptr);
    QVERIFY(defs.findOnLeft(Location::regOf(10)) != nullptr);
    QVERIFY(defs.findOnLeft(Location::regOf(11)) != nullptr);
    QVERIFY(defs.findOnLeft(Location::regOf(12)) != nullptr);
    QVERIFY(defs.findOnLeft(Location::regOf(13)) != nullptr);
    QVERIFY(defs.findOnLeft(Location::regOf( 1)) != nullptr);
    qDeleteAll(defs);
    defs.clear();

    QVERIFY(Signature::getABIDefines(Machine::PPC, defs));
    QVERIFY(defs.size() == 10);
    QVERIFY(defs.findOnLeft(Location::regOf( 3)) != nullptr);
    QVERIFY(defs.findOnLeft(Location::regOf( 4)) != nullptr);
    QVERIFY(defs.findOnLeft(Location::regOf( 5)) != nullptr);
    QVERIFY(defs.findOnLeft(Location::regOf( 6)) != nullptr);
    QVERIFY(defs.findOnLeft(Location::regOf( 7)) != nullptr);
    QVERIFY(defs.findOnLeft(Location::regOf( 8)) != nullptr);
    QVERIFY(defs.findOnLeft(Location::regOf( 9)) != nullptr);
    QVERIFY(defs.findOnLeft(Location::regOf(10)) != nullptr);
    QVERIFY(defs.findOnLeft(Location::regOf(11)) != nullptr);
    QVERIFY(defs.findOnLeft(Location::regOf(12)) != nullptr);
    qDeleteAll(defs);
    defs.clear();

    QVERIFY(Signature::getABIDefines(Machine::ST20, defs));
    QVERIFY(defs.size() == 3);

    QVERIFY(!Signature::getABIDefines(Machine::ST20, defs));
    QVERIFY(defs.size() == 3);

    QVERIFY(!Signature::getABIDefines(Machine::PPC, defs));
    QVERIFY(defs.size() == 3);

    // Machine::ST20
    QVERIFY(defs.findOnLeft(Location::regOf(0)) != nullptr);
    QVERIFY(defs.findOnLeft(Location::regOf(1)) != nullptr);
    QVERIFY(defs.findOnLeft(Location::regOf(2)) != nullptr);
    qDeleteAll(defs);
    defs.clear();

    QVERIFY(Signature::getABIDefines(Machine::UNKNOWN, defs));
    QVERIFY(defs.empty());
    QVERIFY(!Signature::getABIDefines(Machine::INVALID, defs));
    QVERIFY(defs.empty());
}


void SignatureTest::testPreferredName()
{
    Signature sig("test");

    QCOMPARE(sig.getPreferredName(), QString());
    sig.setPreferredName("Foo");
    QCOMPARE(sig.getPreferredName(), QString("Foo"));
}


QTEST_GUILESS_MAIN(SignatureTest)
