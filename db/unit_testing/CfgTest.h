#include "cfg.h"
#include <QtTest/QTest>
class CfgTest : public QObject {
    Q_OBJECT
  protected:
    Cfg *m_prog;

  public:
    CfgTest();
  private slots:
    void initTestCase();
    void testDominators();
    void testSemiDominators();
    void testPlacePhi();
    void testPlacePhi2();
    void testRenameVars();
};
