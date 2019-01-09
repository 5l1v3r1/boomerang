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


#include "boomerang/ssl/type/Type.h"


class BOOMERANG_API NamedType : public Type
{
public:
    explicit NamedType(const QString &name);

    NamedType(const NamedType &other) = default;
    NamedType(NamedType &&other)      = default;

    virtual ~NamedType() override;

    NamedType &operator=(const NamedType &other) = default;
    NamedType &operator=(NamedType &&other) = default;

public:
    static std::shared_ptr<NamedType> get(const QString &_name)
    {
        return std::make_shared<NamedType>(_name);
    }

    /// \copydoc Type::clone
    virtual SharedType clone() const override;

public:
    /// \copydoc Type::operator==
    virtual bool operator==(const Type &other) const override;

    /// \copydoc Type::operator<
    virtual bool operator<(const Type &other) const override;

public:
    /// \copydoc Type::getSize
    virtual Size getSize() const override;

    /// \copydoc Type::getCtype
    virtual QString getCtype(bool final = false) const override;

public:
    QString getName() const { return m_name; }

    SharedType resolvesTo() const;

    /// \copydoc Type::meetWith
    virtual SharedType meetWith(SharedType other, bool &changed, bool useHighestPtr) const override;

protected:
    /// \copydoc Type::isCompatible
    virtual bool isCompatible(const Type &other, bool all) const override;

private:
    QString m_name;
};


template<>
inline std::shared_ptr<NamedType> Type::as<NamedType>()
{
    assert(std::dynamic_pointer_cast<NamedType>(shared_from_this()) != nullptr);
    return std::static_pointer_cast<NamedType>(shared_from_this());
}


template<>
inline std::shared_ptr<const NamedType> Type::as<NamedType>() const
{
    assert(std::dynamic_pointer_cast<const NamedType>(shared_from_this()) != nullptr);
    return std::static_pointer_cast<const NamedType>(shared_from_this());
}
