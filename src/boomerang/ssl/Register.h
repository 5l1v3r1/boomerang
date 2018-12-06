#pragma region License
/*
 * This file is part of the Boomerang Decompiler.
 *
 * See the file "LICENSE.TERMS" for information on usage and
 * redistribution of this file, and for a DISCLAIMER OF ALL
 * WARRANTIES.
 */
#pragma endregion License
#pragma once


#include "boomerang/core/BoomerangAPI.h"

#include <QString>

#include <cstdint>
#include <memory>
#include <string>


class Type;

typedef std::shared_ptr<Type> SharedType;

typedef int RegID;
static constexpr const RegID RegIDSpecial = -1;

enum class RegType
{
    Invalid = 0,
    Int     = 1,
    Float   = 2,
    Flags   = 3
};


/**
 * Summarises one line of the \@REGISTERS section of an SSL
 * file. This class is used extensively in sslparser.y.
 */
class BOOMERANG_API Register
{
public:
    Register(RegType type, const QString &name, uint16_t sizeInBits);
    Register(const Register &);
    Register(Register &&) = default;

    ~Register() = default;

    Register &operator=(const Register &other);
    Register &operator=(Register &&other) = default;

public:
    bool operator==(const Register &other) const;
    bool operator<(const Register &other) const;

    const QString &getName() const;
    uint16_t getSize() const;

    /// \returns the type of the content of this register
    SharedType getType() const;

    /// \returns the type of the register(int, float, flags)
    RegType getRegType() const { return m_regType; }

    /// Get the mapped offset (see above)
    int getMappedOffset() const { return m_mappedOffset; }

    /// Get the mapped index (see above)
    RegID getMappedIndex() const { return m_mappedIndex; }

    /**
     * Set the mapped offset. This is the bit number where this register starts,
     * e.g. for register %ah, this is 8. For COVERS regisers, this is 0
     */
    void setMappedOffset(int i) { m_mappedOffset = i; }

    /**
     * Set the mapped index. For COVERS registers, this is the lower register
     * of the set that this register covers. For example, if the current register
     * is f28to31, i would be the index for register f28
     * For SHARES registers, this is the "parent" register, e.g. if the current
     * register is %al, the parent is %ax (note: not %eax)
     */
    void setMappedIndex(RegID i) { m_mappedIndex = i; }

private:
    QString m_name;
    uint16_t m_size;
    RegType m_regType;
    RegID m_mappedIndex;
    int m_mappedOffset;
};
