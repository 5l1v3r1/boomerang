#pragma region License
/*
 * This file is part of the Boomerang Decompiler.
 *
 * See the file "LICENSE.TERMS" for information on usage and
 * redistribution of this file, and for a DISCLAIMER OF ALL
 * WARRANTIES.
 */
#pragma endregion License
#pragma once


#include "boomerang/util/Log.h"

#include <QtTest/QTest>
#include <iostream>


/**
 * Tests the Dta Flow based type analysis code
 */
class DfaTest : public QObject
{
	Q_OBJECT

private slots:
   	void initTestCase();

    /// Test meeting IntegerTypes with various other types
	void testMeetInt();

    /// Test meeting SizeTypes with various other types
	void testMeetSize();

    /// Test meeting PointerTypes with various other types
	void testMeetPointer();

    /// Test meeting Unions with various other types
	void testMeetUnion();
};
