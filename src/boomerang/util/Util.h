#pragma once

/***************************************************************************/ /**
 * \file       util.h
 * OVERVIEW:   Provides the definition for the miscellaneous bits and pieces
 *                 implemented in the util.so library
 ******************************************************************************/

#include <QString>

#include "boomerang/util/types.h"

class Printable
{
public:
	virtual ~Printable() {}
	virtual QString toString() const = 0;
};


namespace Util
{

QString escapeStr(const QString& str);

/// return a bit mask with exactly @p bitCount of the lowest bits set to 1.
/// (example: 16 -> 0xFFFF)
inline QWord getLowerBitMask(DWord bitCount)
{
	return (1ULL << (QWord)(bitCount % (8*sizeof(void*)))) - 1ULL;
}

template<class T, class U>
bool inRange(const T& val, const U& range_start, const U& range_end)
{
	return((val >= range_start) && (val < range_end));
}

}
