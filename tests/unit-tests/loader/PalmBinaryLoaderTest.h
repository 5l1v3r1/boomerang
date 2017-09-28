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

class PalmBinaryLoaderTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase();

	/// Test loading the Palm 68328 Starter.prc program
	void testPalmLoad();
};
