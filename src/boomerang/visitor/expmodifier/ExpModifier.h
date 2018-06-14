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


#include <memory>


class Exp;
class Unary;
class Binary;
class Ternary;
class TypedExp;
class FlagDef;
class RefExp;
class Location;
class Const;
class Terminal;

using SharedExp = std::shared_ptr<class Exp>;


/**
 * The ExpModifier class is used to iterate over all subexpressions in an expression. It contains methods for each kind
 * of subexpression found in an and can be used to eliminate switch statements.
 * It is a little more expensive to use than ExpVisitor, but can make changes to the expression
 */
class ExpModifier
{
public:
    ExpModifier() = default;
    virtual ~ExpModifier() = default;

public:
    bool isMod() const { return m_mod; }
    void clearMod() { m_mod = false; }

    /**
     * Change the expression before visiting children.
     * The default behaviour is to not modify the expression
     * and to recurse to all child expressions.
     *
     * \param[in]  exp           the expression to change
     * \param[out] visitChildren true to continue visiting children.
     * \returns the modified expression
     */
    virtual SharedExp preModify(const std::shared_ptr<Unary>& exp, bool& visitChildren);

    /// \copydoc ExpModifier::preModify
    virtual SharedExp preModify(const std::shared_ptr<Binary>& exp, bool& visitChildren);

    /// \copydoc ExpModifier::preModify
    virtual SharedExp preModify(const std::shared_ptr<Ternary>& exp, bool& visitChildren);

    /// \copydoc ExpModifier::preModify
    virtual SharedExp preModify(const std::shared_ptr<TypedExp>& exp, bool& visitChildren);

    /// \copydoc ExpModifier::preModify
    virtual SharedExp preModify(const std::shared_ptr<FlagDef>& exp, bool& visitChildren);

    /// \copydoc ExpModifier::preModify
    virtual SharedExp preModify(const std::shared_ptr<RefExp>& exp, bool& visitChildren);

    /// \copydoc ExpModifier::preModify
    virtual SharedExp preModify(const std::shared_ptr<Location>& exp, bool& visitChildren);

    /**
     * Modify the expression after modifying children.
     * The default behaviour is to not modify the expression.
     *
     * \param exp the expression to modify.
     * \returns the modified expression.
     */
    virtual SharedExp postModify(const std::shared_ptr<Unary>& exp);

    /// \copydoc ExpModifier::postModify
    virtual SharedExp postModify(const std::shared_ptr<Binary>& exp);

    /// \copydoc ExpModifier::postModify
    virtual SharedExp postModify(const std::shared_ptr<Ternary>& exp);

    /// \copydoc ExpModifier::postModify
    virtual SharedExp postModify(const std::shared_ptr<TypedExp>& exp);

    /// \copydoc ExpModifier::postModify
    virtual SharedExp postModify(const std::shared_ptr<FlagDef>& exp);

    /// \copydoc ExpModifier::postModify
    virtual SharedExp postModify(const std::shared_ptr<RefExp>& exp);

    /// \copydoc ExpModifier::postModify
    virtual SharedExp postModify(const std::shared_ptr<Location>& exp);


    /// \copydoc ExpModifier::preModify
    virtual SharedExp modify(const std::shared_ptr<Const>& exp);

    /// \copydoc ExpModifier::preModify
    virtual SharedExp modify(const std::shared_ptr<Terminal>& exp);

protected:
    bool m_mod = false; ///< Set if there is any change. Don't have to implement
};


