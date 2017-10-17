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

class HpSomBinaryLoaderTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    /// Test loading the sparc hello world program
    void testHppaLoad();
};
