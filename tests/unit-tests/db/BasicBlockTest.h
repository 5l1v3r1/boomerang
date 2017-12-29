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


#include <QtTest/QTest>

class BasicBlockTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void testGetType();
    void testPredecessor();
    void testSuccessor();
};
