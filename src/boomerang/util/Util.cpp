/*
 * Copyright (C) 2000-2001, The University of Queensland
 * Copyright (C) 2001, Sun Microsystems, Inc
 * Copyright (C) 2002, Trent Waddington
 *
 * See the file "LICENSE.TERMS" for information on usage and
 * redistribution of this file, and for a DISCLAIMER OF ALL
 * WARRANTIES.
 *
 */

/***************************************************************************/ /**
 * \file       util.cc
 * \brief   This file contains miscellaneous functions that don't belong to
 *             any particular subsystem of UQBT.
 ******************************************************************************/

#include "boomerang/util/Util.h"
#include "boomerang/util/types.h"

#include <QString>
#include <QMap>
#include <QTextStream>
#include <cassert>
#include <string>

// Turn things like newline, return, tab into \n, \r, \t etc
// Note: assumes a C or C++ back end...
QString escapeStr(const QString& inp)
{
	static QMap<char, QString> replacements {
		{
			'\n', "\\n"
		}, {
			'\t', "\\t"
		}, {
			'\v', "\\v"
		}, {
			'\b', "\\b"
		}, {
			'\r', "\\r"
		}, {
			'\f', "\\f"
		}, {
			'\a', "\\a"
		},
		{
			'"', "\\\""
		}
	};

	QString res;

	for (char c : inp.toLocal8Bit()) {
		if (isprint(c) && (c != '\"')) {
			res += QChar(c);
			continue;
		}

		if (replacements.contains(c)) {
			res += replacements[c];
		}
		else {
			res += "\\" + QString::number(c, 16);
		}
	}

	return res;
}

