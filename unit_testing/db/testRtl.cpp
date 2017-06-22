/***************************************************************************/ /**
 * \file       testRtl.cpp
 * OVERVIEW:   Command line test of the Rtl class
 *============================================================================*/

/*
 * $Revision$
 * 15 Jul 02 - Mike: Created from testDbase
 * 29 Jul 03 - Mike: Created from testAll
 */

#include "boomerang/cppunit/TextTestResult.h"
#include "boomerang/cppunit/TestSuite.h"

#include "boomerang/RtlTest.h"

#include <sstream>
#include <iostream>

int main(int argc, char **argv)
{
	CppUnit::TestSuite suite;

	RtlTest expt("RtlTest");

	expt.registerTests(&suite);

	CppUnit::TextTestResult res;

	suite.run(&res);
	std::cout << res << std::endl;

	return 0;
}
