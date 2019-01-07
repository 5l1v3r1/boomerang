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


#include "TestUtils.h"

class UnionTest : public BoomerangTest
{
    Q_OBJECT

private slots:
    void testConstruct();
    void testAddType();
    void testGetNumTypes();
    void testHasType();
    // meetWith is tested separately
};
