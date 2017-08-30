#pragma once

#include <QString>
#include <QTextStream>

#include "boomerang/util/Types.h"

class Printable
{
public:
    virtual ~Printable() {}
    virtual QString toString() const = 0;
};


namespace Util
{

/**
 * Escape strings properly for code generation.
 * Turns things like newline, return, tab into \n, \r, \t etc
 * \note Assumes a C or C++ back end
 */
QString escapeStr(const QString& str);

QTextStream& alignStream(QTextStream& str, int align);

/// return a bit mask with exactly \p bitCount of the lowest bits set to 1.
/// (example: 16 -> 0xFFFF)
inline QWord getLowerBitMask(DWord bitCount)
{
    return (1ULL << (bitCount % (8*sizeof(QWord)))) - 1ULL;
}

/// Check if \p value is in [\p rangeStart, \p rangeEnd)
template<class T, class U>
bool inRange(const T& value, const U& rangeStart, const U& rangeEnd)
{
    return (value >= rangeStart) && (value < rangeEnd);
}

/// Check if a value is in a container
template<typename Cont, typename T>
bool isIn(const Cont& cont, const T& value)
{
    return std::find(cont.begin(), cont.end(), value) != cont.end();
}

inline SWord swapEndian(SWord value)
{
    value = ((value << 8) & 0xFF00) | ((value >> 8) & 0x00FF);
    return value;
}

inline DWord swapEndian(DWord value)
{
    value = ((value << 16) & 0xFFFF0000) | ((value >> 16) & 0x0000FFFF);
    value = ((value <<  8) & 0xFF00FF00) | ((value >>  8) & 0x00FF00FF);
    return value;
}

inline QWord swapEndian(QWord value)
{
    value = ((value << 32) & 0xFFFFFFFF00000000ULL) | ((value >> 32) & 0x00000000FFFFFFFFULL);
    value = ((value << 16) & 0xFFFF0000FFFF0000ULL) | ((value >> 16) & 0x0000FFFF0000FFFFULL);
    value = ((value <<  8) & 0xFF00FF00FF00FF00ULL) | ((value >>  8) & 0x00FF00FF00FF00FFULL);
    return value;
}

/**
 * Normalize endianness of a value.
 * Swaps bytes of \p value if the endianness of the source,
 * indicated by \p srcBigEndian, is different from the endianness
 * of the host.
 */
SWord normEndian(SWord value, bool srcBigEndian);
DWord normEndian(DWord value, bool srcBigEndian);
QWord normEndian(QWord value, bool srcBigEndian);

/// Read values, respecting endianness
/// \sa normEndian
Byte readByte(const void* src);
SWord readWord(const void* src, bool srcBigEndian);
DWord readDWord(const void* src, bool srcBigEndian);
QWord readQWord(const void* src, bool srcBigEndian);


/// Write values to \p dst, respecting endianness
void writeByte(void* dst, Byte value);
void writeWord(void* dst, SWord value, bool dstBigEndian);
void writeDWord(void* dst, DWord value, bool dstBigEndian);
void writeQWord(void* dst, DWord value, bool dstBigEndian);

/**
 * Sign-extend \p src into \a TgtType.
 * Example:
 *   signExtend<int>((unsigned char)0xFF) == -1
 *
 * \param src number to sign-extend
 * \param numSrcBits Number of Bits in the source type
 *        (Mainly to counter int-promption in (blabla & 0xFF))
 */
template<typename TgtType = int, typename SrcType>
TgtType signExtend(const SrcType& src, size_t numSrcBits = 8*sizeof(SrcType))
{
    static_assert(std::is_integral<SrcType>::value, "Source type must be an integer!");
    static_assert(std::is_integral<TgtType>::value && std::is_signed<TgtType>::value, "Target type must be a signed integer!");

    // size difference, in bits
    const int sizeDifference = 8*sizeof(TgtType) - numSrcBits;
    return ((TgtType)((TgtType)src << sizeDifference)) >> sizeDifference;
}

}

#define DEBUG_BUFSIZE    5000 // Size of the debug print buffer
extern char debug_buffer[DEBUG_BUFSIZE];


/// Given a pointer p, returns the 16 bits (halfword) in the two bytes
/// starting at p.
#define LH(p)    ((int)((Byte *)(p))[0] + ((int)((Byte *)(p))[1] << 8))
