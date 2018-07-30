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


#include "boomerang/ssl/statements/TypingStatement.h"


/**
 * Assignment is the base class of all statements that assign to a
 * left hand side like ordinary assign statements, phi statements or implicit assignments.
 */
class Assignment : public TypingStatement
{
public:
    Assignment(SharedExp lhs);
    Assignment(SharedType ty, SharedExp lhs);
    Assignment(const Assignment& other) = default;
    Assignment(Assignment&& other) = default;

    virtual ~Assignment() override;

    Assignment& operator=(const Assignment& other) = default;
    Assignment& operator=(Assignment&& other) = default;

public:
    /// We also want operator< for assignments. For example, we want ReturnStatement
    /// to contain a set of (pointers to) Assignments, so we can automatically
    /// make sure that existing assignments are not duplicated.
    /// Assume that we won't want sets of assignments differing by anything other than LHSs
    bool operator<(const Assignment& o) { return m_lhs < o.m_lhs; }

    /// \copydoc Assignment::print
    virtual void print(QTextStream& os, bool html = false) const override;

    /// Like print, but print without statement number
    virtual void printCompact(QTextStream& os, bool html = false) const = 0;

    /// \copydoc Statement::getTypeFor
    virtual SharedConstType getTypeFor(SharedConstExp e) const override;

    /// \copydoc Statement::getTypeFor
    virtual SharedType getTypeFor(SharedExp e) override;

    /// \copydoc Statement::setTypeFor
    virtual void setTypeFor(SharedExp e, SharedType ty) override;

    /// \copydoc Statement::usesExp
    /// \internal PhiAssign and ImplicitAssign don't override
    virtual bool usesExp(const Exp& e) const override;

    /// \copydoc Statement::getDefinitions
    virtual void getDefinitions(LocationSet& defs, bool assumeABICompliance) const override;

    /// \copydoc Statement::definesLoc
    virtual bool definesLoc(SharedExp loc) const override;

    /// \returns the expression defining the left hand side of the assignment
    virtual SharedExp getLeft() const;

    /// Update the left hand side of the assignment
    void setLeft(SharedExp e);

    // get how to replace this statement in a use
    /// \returns the expression defining the right hand side of the assignment
    virtual SharedExp getRight() const = 0;

    /// \copydoc Statement::generateCode
    virtual void generateCode(ICodeGenerator *, const BasicBlock *) override {}

    /// \copydoc Statement::simplifyAddr
    virtual void simplifyAddr() override;

protected:
    SharedExp m_lhs; ///< The left hand side of the assignment
};
