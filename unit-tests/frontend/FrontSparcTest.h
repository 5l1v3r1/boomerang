#include <QtTest/QTest>

class FrontSparcTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase();

	/***************************************************************************/ /**
	 * FUNCTION:        FrontSparcTest::test1
	 * OVERVIEW:        Test decoding some sparc instructions
	 *============================================================================*/
	void test1();

	void test2();

	void test3();

	void testBranch();

	void testDelaySlot();
};
