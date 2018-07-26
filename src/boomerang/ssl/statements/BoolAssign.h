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


#include "boomerang/ssl/statements/Assignment.h"


/**
 * BoolAssign represents "setCC" type instructions, where some destination is set
 * (to 1 or 0) depending on the condition codes.
 * It has a condition Exp, similar to the BranchStatement class.
 */
class BoolAssign : public Assignment
{
public:
    /// \param size the size of the assignment
    BoolAssign(int size);
    BoolAssign(const BoolAssign& other) = default;
    BoolAssign(BoolAssign&& other) = default;

    virtual ~BoolAssign() override;

    BoolAssign& operator=(const BoolAssign& other) = default;
    BoolAssign& operator=(BoolAssign&& other) = default;

public:
    /// \copydoc Statement::clone
    virtual Statement *clone() const override;

    /// \copydoc Statement::accept
    virtual bool accept(StmtVisitor *visitor) const override;

    /// \copydoc Statement::accept
    virtual bool accept(StmtExpVisitor *visitor) override;

    /// \copydoc Statement::accept
    virtual bool accept(StmtModifier *modifier) override;

    /// \copydoc Statement::accept
    virtual bool accept(StmtPartModifier *modifier) override;

    /**
     * Sets the BranchType of this jcond as well as the flag
     * indicating whether or not the floating point condition codes
     * are used.
     * \param cond      the type of branch
     * \param usesFloat - this condional jump checks the floating point condition codes
     */
    void setCondType(BranchType cond, bool usesFloat = false);

    BranchType getCond() const { return m_jumpType; }
    bool isFloat() const { return m_isFloat; }
    void setFloat(bool b) { m_isFloat = b; }

    // Set and return the Exp representing the HL condition

    /// \returns the Exp expression containing the HL condition.
    SharedExp getCondExpr() const;

    /**
     * Set the Exp expression containing the HL condition.
     * \param pss Pointer to semantic string to set
     */
    void setCondExpr(SharedExp pss);

    // As above, no delete (for subscripting)
    void setCondExprND(SharedExp e) { m_cond = e; }
    int getSize() const { return m_size; } // Return the size of the assignment

    /**
     * Change this from an unsigned to a signed branch.
     * \note Not sure if this is ever going to be used
     */
    void makeSigned();

    /// \copydoc Assignment::printCompact
    virtual void printCompact(QTextStream& os, bool html = false) const override;

    /// \copydoc Statement::generateCode
    virtual void generateCode(ICodeGenerator *gen, const BasicBlock *parentBB) override;

    /// \copydoc Statement::simplify
    virtual void simplify() override;

    /// \copydoc Statement::getDefinitions
    virtual void getDefinitions(LocationSet& def, bool assumeABICompliance) const override;

    /// \copydoc Assignment::getRight
    virtual SharedExp getRight() const override { return getCondExpr(); }

    /// \copydoc Assignment::usesExp
    virtual bool usesExp(const Exp& e) const override;

    /// \copydoc Statement::search
    virtual bool search(const Exp& search, SharedExp& result) const override;

    /// \copydoc Statement::searchAll
    virtual bool searchAll(const Exp& search, std::list<SharedExp>& result) const override;

    /// \copydoc Statement::searchAndReplace
    virtual bool searchAndReplace(const Exp& search, SharedExp replace, bool cc = false) override;

    /// a hack for the SETS macro
    /// This is for setting up SETcc instructions; see include/decoder.h macro SETS
    void setLeftFromList(const std::list<Statement *>& stmts);

private:
    BranchType m_jumpType = BranchType::INVALID; ///< the condition for setting true
    SharedExp m_cond;      ///< Exp representation of the high level condition: e.g. r[8] == 5
    bool m_isFloat;        ///< True if condition uses floating point CC
    int m_size;            ///< The size of the dest
};
