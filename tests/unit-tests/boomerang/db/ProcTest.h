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
#include <memory>

#include "boomerang/core/Project.h"


class ProcTest : public QObject
{
private slots:
    /// Test setting and reading name, constructor, native address
    void testName();

private:
    Project m_project;
};
