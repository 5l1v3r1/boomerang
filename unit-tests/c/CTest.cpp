/**
 * \file CTest.cpp Provides the implementation for the CTest class, which
 * tests the c parser
 */

#include "CTest.h"

#include "boomerang/c/ansi-c-parser.h"

#include <sstream>

void CTest::testSignature()
{
	std::istringstream os("int printf(char *fmt, ...);");
	AnsiCParser        *p = new AnsiCParser(os, false);
	p->yyparse(Platform::PENTIUM, CallConv::C);

    QCOMPARE(p->signatures.size(), size_t(1));

	auto sig = p->signatures.front();
	QCOMPARE(sig->getName(), QString("printf"));

	// The functions have two return parameters :
	// 0 - ESP
	// 1 - Actual return
	QCOMPARE(sig->getNumReturns(), size_t(2));
	QVERIFY(sig->getReturnType(1)->resolvesToInteger());
	SharedType t = PointerType::get(CharType::get());

    // Pentium signatures used to have esp prepended to the list of parameters; no more?
	QCOMPARE(sig->getNumParams(), size_t(1));
	QVERIFY(*sig->getParamType(0) == *t);
	QCOMPARE(sig->getParamName(0), QString("fmt"));
	QVERIFY(sig->hasEllipsis());
	delete p;
}


QTEST_MAIN(CTest)
