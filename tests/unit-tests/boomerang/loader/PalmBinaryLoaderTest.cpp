#pragma region License
/*
 * This file is part of the Boomerang Decompiler.
 *
 * See the file "LICENSE.TERMS" for information on usage and
 * redistribution of this file, and for a DISCLAIMER OF ALL
 * WARRANTIES.
 */
#pragma endregion License
#include "PalmBinaryLoaderTest.h"


#include "boomerang/core/Boomerang.h"
#include "boomerang/db/binary/BinaryImage.h"
#include "boomerang/db/binary/BinarySection.h"
#include "boomerang/core/Project.h"
#include "boomerang/util/Log.h"

#define STARTER_PALM    (Boomerang::get()->getSettings()->getDataDirectory().absoluteFilePath("samples/mc68328/Starter.prc"))


void PalmBinaryLoaderTest::initTestCase()
{
    Boomerang::get()->getSettings()->setDataDirectory(BOOMERANG_TEST_BASE "share/boomerang/");
    Boomerang::get()->getSettings()->setPluginDirectory(BOOMERANG_TEST_BASE "lib/boomerang/plugins/");
}


void PalmBinaryLoaderTest::testPalmLoad()
{
    IProject *project = Boomerang::get()->getOrCreateProject();
    QVERIFY(project->loadBinaryFile(STARTER_PALM));

    BinaryImage *image = project->getLoadedBinaryFile()->getImage();

    QCOMPARE(image->getNumSections(), 8);
    QCOMPARE(image->getSectionByIndex(0)->getName(), QString("code1"));
    QCOMPARE(image->getSectionByIndex(1)->getName(), QString("MBAR1000"));
    QCOMPARE(image->getSectionByIndex(2)->getName(), QString("tFRM1000"));
    QCOMPARE(image->getSectionByIndex(3)->getName(), QString("Talt1001"));
    QCOMPARE(image->getSectionByIndex(4)->getName(), QString("data0"));
    QCOMPARE(image->getSectionByIndex(5)->getName(), QString("code0"));
    QCOMPARE(image->getSectionByIndex(6)->getName(), QString("tAIN1000"));
    QCOMPARE(image->getSectionByIndex(7)->getName(), QString("tver1000"));
}


QTEST_MAIN(PalmBinaryLoaderTest)
