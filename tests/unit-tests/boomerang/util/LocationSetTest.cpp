#pragma region License
/*
 * This file is part of the Boomerang Decompiler.
 *
 * See the file "LICENSE.TERMS" for information on usage and
 * redistribution of this file, and for a DISCLAIMER OF ALL
 * WARRANTIES.
 */
#pragma endregion License
#include "LocationSetTest.h"


#include "boomerang/db/exp/Location.h"
#include "boomerang/db/exp/RefExp.h"
#include "boomerang/db/exp/Terminal.h"
#include "boomerang/db/statements/Assign.h"
#include "boomerang/util/LocationSet.h"


void LocationSetTest::initTestCase()
{
}


void LocationSetTest::testAssign()
{
    LocationSet set1, set2;

    set1 = set2;
    QCOMPARE(set1.prints(), "");

    set2.insert(Location::regOf(PENT_REG_EAX));
    set1 = set2;
    QCOMPARE(set1.prints(), "r24");
}


void LocationSetTest::testCompare()
{
    LocationSet set1, set2;
    QVERIFY(set1 == set2);
    QVERIFY(!(set1 != set2));

    set1.insert(Location::regOf(PENT_REG_ECX));
    QVERIFY(!(set1 == set2));
    QVERIFY(set1 != set2);

    set2.insert(Location::regOf(PENT_REG_ECX));
    QVERIFY(set1 == set2);
    QVERIFY(!(set1 != set2));

    set1.insert(Location::regOf(PENT_REG_EDI));
    QVERIFY(!(set1 == set2));
    QVERIFY(set1 != set2);
}


void LocationSetTest::testEmpty()
{
    LocationSet set;
    QVERIFY(set.empty());

    set.insert(Location::regOf(PENT_REG_EDI));
    QVERIFY(!set.empty());
}


void LocationSetTest::testSize()
{
    LocationSet set;
    QVERIFY(set.size() == 0);

    set.insert(Location::regOf(PENT_REG_ESI));
    QVERIFY(set.size() == 1);

    set.insert(Location::regOf(PENT_REG_EDI));
    QVERIFY(set.size() == 2);
}


void LocationSetTest::testClear()
{
    LocationSet set;

    set.clear();
    QVERIFY(set.empty());

    set.insert(Location::regOf(PENT_REG_ESI));
    set.clear();
    QVERIFY(set.empty());
}


void LocationSetTest::testInsert()
{
    LocationSet set;

    set.insert(Location::regOf(PENT_REG_ESI));
    QCOMPARE(set.prints(), "r30");

    set.insert(Location::regOf(PENT_REG_ESI));
    QCOMPARE(set.prints(), "r30");

    set.insert(Location::regOf(PENT_REG_EDI));
    QCOMPARE(set.prints(), "r30, r31");
}


void LocationSetTest::testRemove()
{
    LocationSet set;
    set.remove(nullptr);
    QVERIFY(set.empty());

    set.insert(Location::regOf(PENT_REG_ESI));
    set.remove(Location::regOf(PENT_REG_ESI));
    QVERIFY(set.empty());

    set.insert(Location::regOf(PENT_REG_ESI));
    set.insert(Location::regOf(PENT_REG_EDI));
    set.remove(Location::regOf(PENT_REG_EDI));
    QCOMPARE(set.prints(), "r30");

    // removing element that does not exist
    set.remove(Location::regOf(PENT_REG_EDI));
    QCOMPARE(set.prints(), "r30");
}


void LocationSetTest::testContains()
{
    LocationSet set;

    QVERIFY(!set.contains(nullptr));

    set.insert(Location::regOf(PENT_REG_ESI));
    QVERIFY(set.contains(Location::regOf(PENT_REG_ESI)));
    QVERIFY(!set.contains(Location::regOf(PENT_REG_EDI)));
}


void LocationSetTest::testContainsImplicit()
{
    LocationSet set;
    QVERIFY(!set.containsImplicit(nullptr));

    set.insert(Location::regOf(PENT_REG_ESI));
    QVERIFY(!set.containsImplicit(Location::regOf(PENT_REG_ESI)));
    QVERIFY(!set.containsImplicit(Location::regOf(PENT_REG_EDI)));

    set.insert(RefExp::get(Location::regOf(PENT_REG_EDI), nullptr));
    QVERIFY(set.containsImplicit(Location::regOf(PENT_REG_EDI)));
    QVERIFY(!set.containsImplicit(Location::regOf(PENT_REG_ESI)));
}


void LocationSetTest::testFindNS()
{
    LocationSet set;
    QVERIFY(set.findNS(nullptr) == nullptr);

    set.insert(Location::regOf(PENT_REG_ESI));
    SharedExp e = set.findNS(Location::regOf(PENT_REG_ESI));
    QVERIFY(e == nullptr);

    set.insert(RefExp::get(Location::regOf(PENT_REG_EDI), nullptr));
    e = set.findNS(Location::regOf(PENT_REG_EDI));
    QVERIFY(e != nullptr);

    QCOMPARE(e->prints(), "r31{-}");
}


void LocationSetTest::testFindDifferentRef()
{
    LocationSet set;

    SharedExp result;
    QVERIFY(!set.findDifferentRef(nullptr, result));

    set.insert(Location::regOf(PENT_REG_EAX));
    QVERIFY(!set.findDifferentRef(RefExp::get(Location::regOf(PENT_REG_EAX), nullptr), result));

    set.insert(RefExp::get(Location::regOf(PENT_REG_EAX), nullptr));
    QVERIFY(!set.findDifferentRef(RefExp::get(Location::regOf(PENT_REG_EAX), nullptr), result));

    Assign as1(Location::regOf(PENT_REG_ECX), Location::regOf(PENT_REG_EDX));
    Assign as2(Location::regOf(PENT_REG_ECX), Location::regOf(PENT_REG_EDX));

    as1.setNumber(10);
    as2.setNumber(20);

    set.insert(RefExp::get(Location::regOf(PENT_REG_ECX), &as1));
    // no other ref
    QVERIFY(!set.findDifferentRef(RefExp::get(Location::regOf(PENT_REG_ECX), &as1), result));

    set.insert(RefExp::get(Location::regOf(PENT_REG_ECX), &as2));
    // return a different ref
    QVERIFY(set.findDifferentRef(RefExp::get(Location::regOf(PENT_REG_ECX), &as1), result));
    QCOMPARE(result->prints(), "r25{20}");

    // should work even when the ref is not in the set
    set.remove(RefExp::get(Location::regOf(PENT_REG_ECX), &as1));
    QVERIFY(set.findDifferentRef(RefExp::get(Location::regOf(PENT_REG_ECX), &as1), result));
    QCOMPARE(result->prints(), "r25{20}");
}


void LocationSetTest::testAddSubscript()
{
    LocationSet set;
    set.addSubscript(nullptr);
    QCOMPARE(set.prints(), "");

    set.insert(Location::regOf(PENT_REG_ECX));
    set.addSubscript(nullptr);
    QCOMPARE(set.prints(), "r25{-}");

    set.insert(Location::regOf(PENT_REG_ECX));
    Assign as(Location::regOf(PENT_REG_ECX), Location::regOf(PENT_REG_EDX));
    as.setNumber(42);
    set.addSubscript(&as);
    QCOMPARE(set.prints(), "r25{-}, r25{42}");
}


void LocationSetTest::testMakeUnion()
{
    LocationSet set1, set2;

    set1.makeUnion(set2);
    QVERIFY(set1.empty());
    QVERIFY(set2.empty());

    set1.insert(Location::regOf(PENT_REG_ECX));
    set1.insert(Location::regOf(PENT_REG_EDX));
    set2.insert(Location::regOf(PENT_REG_EDX));
    set2.insert(Location::regOf(PENT_REG_EBX));

    QCOMPARE(set1.prints(), "r25, r26");
    QCOMPARE(set2.prints(), "r26, r27");

    set1.makeUnion(set2);

    QCOMPARE(set1.prints(), "r25, r26, r27");
    QCOMPARE(set2.prints(), "r26, r27");
}


void LocationSetTest::testMakeDiff()
{
    LocationSet set1, set2;
    set1.makeDiff(set2);
    QVERIFY(set1.empty());
    QVERIFY(set2.empty());

    set1.insert(Location::regOf(PENT_REG_ECX));
    set1.insert(Location::regOf(PENT_REG_EDX));
    set2.insert(Location::regOf(PENT_REG_EDX));
    set2.insert(Location::regOf(PENT_REG_EBX));

    QCOMPARE(set1.prints(), "r25, r26");
    QCOMPARE(set2.prints(), "r26, r27");

    set1.makeDiff(set2);
    QCOMPARE(set1.prints(), "r25");
    QCOMPARE(set2.prints(), "r26, r27");
}


QTEST_GUILESS_MAIN(LocationSetTest)
