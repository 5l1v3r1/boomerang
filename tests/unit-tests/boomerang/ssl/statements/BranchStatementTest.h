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


class BranchStatementTest : public BoomerangTestWithProject
{
    Q_OBJECT

private slots:
    void testClone();
    void testGetDefinitions();
    void testDefinesLoc();
    void testSearch();
    void testSearchAll();
    void testSearchAndReplace();
    void testFallTakenFragment();
    void testToString();
    void testToString_data();
};
