#include "HpSomBinaryLoaderTest.h"


#include "boomerang/db/IBinaryImage.h"
#include "boomerang/util/Log.h"
#include "boomerang/db/IBinarySection.h"

#define HELLO_HPPA             (BOOMERANG_TEST_BASE "/tests/inputs/hppa/hello")

static bool logset = false;

void HpSomBinaryLoaderTest::initTestCase()
{
    if (!logset) {
        logset = true;
		Boomerang::get()->setDataDirectory(BOOMERANG_TEST_BASE "/lib/boomerang/");
        Boomerang::get()->setLogger(new NullLogger());
    }
}


void HpSomBinaryLoaderTest::testHppaLoad()
{
    QSKIP("Disabled.");

	// Load HPPA hello world
	BinaryFileFactory bff;
	IFileLoader       *loader = bff.loadFile(HELLO_HPPA);
	QVERIFY(loader != nullptr);
	IBinaryImage *image = Boomerang::get()->getImage();

	QCOMPARE(image->getNumSections(), (size_t)3);
	QCOMPARE(image->getSectionInfo(0)->getName(), QString("$TEXT$"));
	QCOMPARE(image->getSectionInfo(1)->getName(), QString("$DATA$"));
	QCOMPARE(image->getSectionInfo(2)->getName(), QString("$BSS$"));
}

QTEST_MAIN(HpSomBinaryLoaderTest)
