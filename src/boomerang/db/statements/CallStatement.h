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


#include "boomerang/db/statements/GotoStatement.h"
#include "boomerang/db/statements/Assignment.h"
#include "boomerang/db/Managed.h"


class ImplicitAssign;


/**
 * Represents a high level call.
 * Information about parameters and the like are stored here.
 */
class CallStatement : public GotoStatement
{
public:
    CallStatement();
    virtual ~CallStatement() override;

    /// \copydoc GotoStatement::clone
    virtual Statement *clone() const override;

    /// \copydoc GotoStatement::accept
    virtual bool accept(StmtVisitor *visitor) override;

    /// \copydoc GotoStatement::accept
    virtual bool accept(StmtExpVisitor *visitor) override;

    /// \copydoc GotoStatement::accept
    virtual bool accept(StmtModifier *modifier) override;

    /// \copydoc GotoStatement::accept
    virtual bool accept(StmtPartModifier *modifier) override;

    /// \copydoc Statement::setNumber
    virtual void setNumber(int num) override;

    /// Set the arguments of this call.
    /// \param args The list of locations to set the arguments to (for testing)
    void setArguments(const StatementList& args);

    /// Set the arguments of this call based on signature info
    /// \note Should only be called for calls to library functions
    void setSigArguments();

    /// Return call's arguments
    StatementList& getArguments() { return m_arguments; }

    /// Update the arguments based on a callee change
    void updateArguments();

    /// Temporarily needed for ad-hoc type analysis
    int findDefine(SharedExp e);        // Still needed temporarily for ad hoc type analysis
    void removeDefine(SharedExp e);
    void addDefine(ImplicitAssign *as); // For testing

    /// Set the defines to the set of locations modified by the callee,
    /// or if no callee, to all variables live at this call
    void updateDefines();         // Update the defines based on a callee change

    // Calculate results(this) = defines(this) intersect live(this)
    // Note: could use a LocationList for this, but then there is nowhere to store the types (for DFA based TA)
    // So the RHS is just ignored
    std::unique_ptr<StatementList> calcResults(); // Calculate defines(this) isect live(this)

    ReturnStatement *getCalleeReturn() { return m_calleeReturn; }
    void setCalleeReturn(ReturnStatement *ret) { m_calleeReturn = ret; }
    bool isChildless() const;
    SharedExp getProven(SharedExp e);

    std::shared_ptr<Signature> getSignature() { return m_signature; }
    void setSignature(std::shared_ptr<Signature> sig) { m_signature = sig; } ///< Only used by range analysis

    /// Localise the various components of expression e with reaching definitions to this call
    /// Note: can change e so usually need to clone the argument
    /// Was called substituteParams
    ///
    /// Substitute the various components of expression e with the appropriate reaching definitions.
    /// Used in e.g. fixCallBypass (via the CallBypasser). Locations defined in this call are replaced with their proven
    /// values, which are in terms of the initial values at the start of the call (reaching definitions at the call)
    SharedExp localiseExp(SharedExp e);

    /// Localise only components of e, i.e. xxx if e is m[xxx]
    void localiseComp(SharedExp e); // Localise only xxx of m[xxx]

    // Do the call bypass logic e.g. r28{20} -> r28{17} + 4 (where 20 is this CallStatement)
    // Set ch if changed (bypassed)
    SharedExp bypassRef(const std::shared_ptr<RefExp>& r, bool& ch);

    void clearUseCollector() { m_useCol.clear(); }
    void addArgument(SharedExp e, UserProc *proc);

    /// Find the reaching definition for expression e.
    /// Find the definition for the given expression, using the embedded Collector object
    /// Was called findArgument(), and used implicit arguments and signature parameters
    /// \note must only operator on unsubscripted locations, otherwise it is invalid
    SharedExp findDefFor(SharedExp e) const;
    SharedExp getArgumentExp(int i) const;
    void setArgumentExp(int i, SharedExp e);
    void setNumArguments(int i);
    int getNumArguments() const;
    void removeArgument(int i);
    SharedType getArgumentType(int i) const;
    void setArgumentType(int i, SharedType ty);
    void truncateArguments();
    void clearLiveEntry();
    void eliminateDuplicateArgs();

    /// \copydoc GotoStatement::print
    virtual void print(QTextStream& os, bool html = false) const override;

    /// \copydoc GotoStatement::search
    virtual bool search(const Exp& search, SharedExp& result) const override;

    /// \copydoc GotoStatement::searchAndReplace
    virtual bool searchAndReplace(const Exp& search, SharedExp replace, bool cc = false) override;

    /// \copydoc GotoStatement::search
    virtual bool searchAll(const Exp& search, std::list<SharedExp>& result) const override;

    /**
     * \brief Sets a bit that says that this call is effectively followed by a return.
     * This happens e.g. on Sparc when there is a restore in the delay slot of the call
     * \param b true if this is to be set; false to clear the bit
     */
    void setReturnAfterCall(bool b);

    /**
     * \brief Tests a bit that says that this call is effectively followed by a return.
     * This happens e.g. on Sparc when there is a restore in the delay slot of the call
     * \returns True if this call is effectively followed by a return
     */
    bool isReturnAfterCall() const;

    /// Set and return the list of Exps that occur *after* the call (the
    /// list of exps in the RTL occur before the call). Useful for odd patterns.
    void setPostCallExpList(std::list<SharedExp> *le);

    std::list<SharedExp> *getPostCallExpList();

    /// Set the function that is called by this call statement.
    void setDestProc(Function *dest);

    /// \returns the function that is called by this call statement.
    Function *getDestProc();

    /// \copydoc Statement::genConstraints
    virtual void genConstraints(LocationSet& cons) override;

    /// \copydoc GotoStatement::generateCode
    virtual void generateCode(ICodeGenerator *gen, const BasicBlock *parentBB) override;

    /// \copydoc GotoStatement::usesExp
    virtual bool usesExp(const Exp& exp) const override;

    /// \copydoc GotoStatement::isDefinition
    virtual bool isDefinition() const override;

    /// \copydoc Statement::getDefinitions
    virtual void getDefinitions(LocationSet& defs) const override;

    /// \copydoc Statement::definesLoc
    virtual bool definesLoc(SharedExp loc) const override; // True if this Statement defines loc

    /// \copydoc GotoStatement::simplify
    virtual void simplify() override;

    /// \copydoc Statement::getTypeFor
    virtual SharedType getTypeFor(SharedExp e) const override;

    /// \copydoc Statement::setTypeFor
    virtual void setTypeFor(SharedExp e, SharedType ty) override;  // Set the type for this location, defined in this statement

    /// \returns pointer to the def collector object
    DefCollector *getDefCollector() { return &m_defCol; }

    /// \returns pointer to the use collector object
    UseCollector *getUseCollector() { return &m_useCol; }

    /// Add x to the UseCollector for this call
    void useBeforeDefine(SharedExp x) { m_useCol.insert(x); }

    /// Remove e from the UseCollector
    void removeLiveness(SharedExp e) { m_useCol.remove(e); }

    /// Remove all livenesses
    void removeAllLive() { m_useCol.clear(); }

    /// Get list of locations defined by this call
    StatementList& getDefines() { return m_defines; }

    /// Process this call for ellipsis parameters. If found, in a printf/scanf call, truncate the number of
    /// parameters if needed, and return true if any signature parameters added
    /// This function has two jobs. One is to truncate the list of arguments based on the format string.
    /// The second is to add parameter types to the signature.
    /// If -Td is used, type analysis will be rerun with these changes.
    bool ellipsisProcessing(Prog *prog);

    /// Attempt to convert this call, if indirect, to a direct call.
    /// NOTE: at present, we igore the possibility that some other statement
    /// will modify the global. This is a serious limitation!!
    /// \returns true if converted
    bool convertToDirect();

    /// direct call
    void useColfromSSAForm(Statement *s) { m_useCol.fromSSAForm(m_proc, s); }

    bool isCallToMemOffset() const;

private:
    /// Private helper functions for the above
    /// Helper function for makeArgAssign(?)
    void addSigParam(SharedType ty, bool isScanf);

    /// Make an assign suitable for use as an argument from a callee context expression
    Assign *makeArgAssign(SharedType ty, SharedExp e);

    bool objcSpecificProcessing(const QString& formatStr);

protected:
    void updateDefineWithType(int n);

private:
    bool m_returnAfterCall; // True if call is effectively followed by a return.

    /// The list of arguments passed by this call, actually a list of Assign statements (location := expr)
    StatementList m_arguments;

    /// The list of defines for this call, a list of ImplicitAssigns (used to be called returns).
    /// Essentially a localised copy of the modifies of the callee, so the callee could be deleted. Stores types and
    /// locations.  Note that not necessarily all of the defines end up being declared as results.
    StatementList m_defines;

    /// Destination of call. In the case of an analysed indirect call, this will be ONE target's return statement.
    /// For an unanalysed indirect call, or a call whose callee is not yet sufficiently decompiled due to recursion,
    /// this will be nullptr
    Function *m_procDest;

    /// The signature for this call. NOTE: this used to be stored in the Proc, but this does not make sense when
    /// the proc happens to have varargs
    std::shared_ptr<Signature> m_signature;

    /// A UseCollector object to collect the live variables at this call.
    /// Used as part of the calculation of results
    UseCollector m_useCol;

    /// A DefCollector object to collect the reaching definitions;
    /// used for bypassAndPropagate/localiseExp etc; also
    /// the basis for arguments if this is an unanlysed indirect call
    DefCollector m_defCol;

    /// Pointer to the callee ReturnStatement. If the callee is unanlysed,
    /// this will be a special ReturnStatement with ImplicitAssigns.
    /// Callee could be unanalysed because of an unanalysed indirect call,
    /// or a "recursion break".
    ReturnStatement *m_calleeReturn;
};
