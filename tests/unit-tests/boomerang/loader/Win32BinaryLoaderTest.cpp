#pragma region License
/*
 * This file is part of the Boomerang Decompiler.
 *
 * See the file "LICENSE.TERMS" for information on usage and
 * redistribution of this file, and for a DISCLAIMER OF ALL
 * WARRANTIES.
 */
#pragma endregion License
#include "Win32BinaryLoaderTest.h"


#include "boomerang/core/Boomerang.h"
#include "boomerang/core/Project.h"
#include "boomerang/db/binary/BinaryImage.h"
#include "boomerang/db/binary/BinarySection.h"

#include "boomerang/util/Log.h"


#define SWITCH_BORLAND    (Boomerang::get()->getSettings()->getDataDirectory().absoluteFilePath("samples/windows/switch_borland.exe"))


void Win32BinaryLoaderTest::initTestCase()
{
    Boomerang::get()->getSettings()->setDataDirectory(BOOMERANG_TEST_BASE "share/boomerang/");
    Boomerang::get()->getSettings()->setPluginDirectory(BOOMERANG_TEST_BASE "lib/boomerang/plugins/");
}


void Win32BinaryLoaderTest::testWinLoad()
{
    Project project;
    QVERIFY(project.loadBinaryFile(SWITCH_BORLAND));

    // Borland
    BinaryFile *binary = project.getLoadedBinaryFile();
    QCOMPARE(binary->getMainEntryPoint(), Address(0x00401150));
}


QTEST_MAIN(Win32BinaryLoaderTest)
