#include <QtTest/QTest>
class FrontSparcTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase();
	void test1();
	void test2();
	void test3();
	void testBranch();
	void testDelaySlot();
};
