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


#include "boomerang/ssl/exp/Unary.h"


/**
 * Holds one subexpression and the type of this subexpression.
 */
class BOOMERANG_API TypedExp : public Unary
{
public:
    TypedExp(SharedExp e1);

    /// Constructor, type, and subexpression.
    /// A rare const parameter allows the common case of providing a temporary,
    /// e.g. foo = new TypedExp(Type(INTEGER), ...);
    TypedExp(SharedType ty, SharedExp e1);
    TypedExp(const TypedExp &other);
    TypedExp(TypedExp &&other) = default;

    virtual ~TypedExp() override = default;

    TypedExp &operator=(const TypedExp &other) = default;
    TypedExp &operator=(TypedExp &&other) = default;

public:
    static std::shared_ptr<TypedExp> get(SharedExp exp);
    static std::shared_ptr<TypedExp> get(SharedType ty, SharedExp exp);

    /// \copydoc Unary::clone
    virtual SharedExp clone() const override;

    /// \copydoc Unary::operator==
    bool operator==(const Exp &o) const override;

    /// \copydoc Unary::operator<
    bool operator<(const Exp &o) const override;

    /// \copydoc Unary::operator*=
    bool operator*=(const Exp &o) const override;

    /// Get and set the type
    SharedType getType() { return m_type; }
    SharedConstType getType() const { return m_type; }

    void setType(SharedType ty) { m_type = ty; }

    /// \copydoc Unary::ascendType
    virtual SharedType ascendType() override;

    /// \copydoc Unary::descendType
    virtual bool descendType(SharedType newType) override;

public:
    /// \copydoc Unary::acceptVisitor
    virtual bool acceptVisitor(ExpVisitor *v) override;

protected:
    /// \copydoc Unary::acceptPreModifier
    virtual SharedExp acceptPreModifier(ExpModifier *mod, bool &visitChildren) override;

    /// \copydoc Unary::acceptPostModifier
    virtual SharedExp acceptPostModifier(ExpModifier *mod) override;

private:
    SharedType m_type;
};
