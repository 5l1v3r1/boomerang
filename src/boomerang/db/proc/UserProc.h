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


#include "Proc.h"


#include "boomerang/db/CFG.h" // For cfg->simplify()

#include "boomerang/db/DataFlow.h"       // For class UseCollector
#include "boomerang/db/statements/ReturnStatement.h"
#include "boomerang/db/exp/Binary.h"

#include <list>
#include <vector>
#include <map>
#include <set>
#include <string>
#include <cassert>


class Prog;
class UserProc;
class Cfg;
class BasicBlock;
class Exp;
class TypedExp;
struct lessTI;

class Type;
class RTL;
class ICodeGenerator;
class SyntaxNode;
class Parameter;
class Argument;
class Signature;
class Module;
class QTextStream;
class Log;


enum ProcStatus
{
    PROC_UNDECODED,     ///< Has not even been decoded
    PROC_DECODED,       ///< Decoded, no attempt at decompiling
    PROC_SORTED,        ///< Decoded, and CFG has been sorted by address
    PROC_VISITED,       ///< Has been visited on the way down in decompile()
    PROC_INCYCLE,       ///< Is involved in cycles, has not completed early decompilation as yet
    PROC_PRESERVEDS,    ///< Has had preservation analysis done
    PROC_EARLYDONE,     ///< Has completed everything except the global analyses
    PROC_FINAL,         ///< Has had final decompilation
    // , PROC_RETURNS   ///< Has had returns intersected with all caller's defines
    PROC_CODE_GENERATED ///< Has had code generated
};


typedef std::set<UserProc *>  ProcSet;
typedef std::list<UserProc *> ProcList;

/***************************************************************************/ /**
 * UserProc class.
 ******************************************************************************/
class UserProc : public Function
{
public:
    /**
     * A map between machine dependent locations and their corresponding symbolic, machine independent
     * representations.  Example: m[r28{0} - 8] -> local5; this means that *after* transforming out of SSA
     * form, any locations not specifically mapped otherwise (e.g. m[r28{0} - 8]{55} -> local6) will get this
     * name.
     * It is a *multi*map because one location can have several default names differentiated by type.
     * E.g. r24 -> eax for int, r24 -> eax_1 for float
     */
    typedef std::multimap<SharedConstExp, SharedExp, lessExpStar> SymbolMap;

    typedef std::map<Statement *, int> RefCounter;

public:
    /***************************************************************************/ /**
     * \brief        Constructor with name, native address.
     * \param address - Native address of entry point of procedure
     * \param name - Name of procedure
     * \param mod - Module that contains this Function
     ******************************************************************************/
    UserProc(Address address, const QString& name, Module *mod);
    virtual ~UserProc();

    QString toString() const override;

    /// Returns a pointer to the CFG object.
    Cfg *getCFG()       { return m_cfg; }
    const Cfg *getCFG() const { return m_cfg; }

    /// Returns a pointer to the DataFlow object.
    DataFlow *getDataFlow()       { return &m_df; }
    const DataFlow *getDataFlow() const { return &m_df; }

    /**
     * Returns an abstract syntax tree for the procedure in the internal representation.
     * This function actually _calculates_ * this value
     * and is expected to do so expensively.
     */
    SyntaxNode *getAST() const;


    /// Print AST to a file.
    /// if \p node is null, print the AST of the whole procedure.
    void printAST(SyntaxNode *node = nullptr) const;

    /// \copydoc Function::isNoReturn
    virtual bool isNoReturn() const override;

    DataIntervalMap& localsMap() { return m_localTable; }

    /// Returns whether or not this procedure can be decoded (i.e. has it already been decoded).
    bool isDecoded() const { return m_status >= PROC_DECODED; }
    bool isDecompiled() const { return m_status >= PROC_FINAL; }
    bool isEarlyRecursive() const { return m_cycleGroup != nullptr && m_status <= PROC_INCYCLE; }
    bool doesRecurseTo(UserProc *p) const { return m_cycleGroup && m_cycleGroup->find(p) != m_cycleGroup->end(); }

    bool isSorted() const { return m_status >= PROC_SORTED; }
    void setSorted() { setStatus(PROC_SORTED); }

    ProcStatus getStatus() const { return m_status; }
    void setStatus(ProcStatus s);

    /// print this proc, mainly for debugging
    void print(QTextStream& out, bool html = false) const;
    void printParams(QTextStream& out, bool html = false) const;
    char *prints() const;
    void dump() const;
    void printDFG() const;

    /// Print just the symbol map
    void printSymbolMap(QTextStream& out, bool html = false) const;

    /// For debugging
    void dumpSymbolMap() const;

    /// For debugging
    void dumpSymbolMapx() const;

    /// For debugging
    void testSymbolMap() const;
    void dumpLocals(QTextStream& os, bool html = false) const;
    void dumpLocals() const;

    /***************************************************************************/ /**
     * \brief Records that this procedure has been decoded.
     ******************************************************************************/
    void setDecoded();

    /***************************************************************************/ /**
     * \brief Removes the decoded bit and throws away all the current information
     * about this procedure.
     ******************************************************************************/
    void unDecode();

    /***************************************************************************/ /**
     * \brief        Deletes the whole Cfg for this proc object. Also clears the
     * cfg pointer, to prevent strange errors after this is called
     ******************************************************************************/
    void deleteCFG() override;

    /// simplify the statements in this proc
    void simplify() { m_cfg->simplify(); }

    /***************************************************************************/ /**
     * \brief Begin the decompile process at this procedure
     * \param  path - is a list of pointers to procedures, representing the path from
     * the current entry point to the current procedure in the call graph. Pass an
     * empty set at the top level.
     * \param indent is the indentation level; pass 0 at the top level
     ******************************************************************************/
    std::shared_ptr<ProcSet> decompile(ProcList *path, int& indent);

    /***************************************************************************/ /**
     * \brief Initialise decompile: sort CFG, number statements, dominator tree, etc.
     ******************************************************************************/
    void initialiseDecompile();

    /***************************************************************************/ /**
     * \brief Early decompile: Place phi functions, number statements, first rename,
     * propagation: ready for preserveds.
     ******************************************************************************/
    void earlyDecompile();

    /***************************************************************************/ /**
     * \brief Middle decompile: All the decompilation from preservation up to
     * but not including removing unused statements.
     * \returns the cycle set from the recursive call to decompile()
     ******************************************************************************/
    std::shared_ptr<ProcSet> middleDecompile(ProcList *path, int indent);

    /// Analyse the whole group of procedures for conditional preserveds, and update till no change.
    /// Also finalise the whole group.
    void recursionGroupAnalysis(ProcList *path, int indent);

    /***************************************************************************/ /**
     * \brief Global type analysis (for this procedure).
     ******************************************************************************/
    void typeAnalysis();

    /***************************************************************************/ /**
     * \brief The inductive preservation analysis.
     ******************************************************************************/
    bool inductivePreservation(UserProc *);

    /***************************************************************************/ /**
     * \brief Mark calls involved in the recursion cycle as non childless
     * (each child has had middleDecompile called on it now).
     * \todo Not sure that this is needed...
     ******************************************************************************/
    void markAsNonChildless(const std::shared_ptr<ProcSet>& cs);

    /***************************************************************************/ /**
     * \brief Update the defines and arguments in calls.
     ******************************************************************************/
    void updateCalls();

    /***************************************************************************/ /**
     * \brief Look for short circuit branching
     ******************************************************************************/
    bool branchAnalysis();

    /***************************************************************************/ /**
     * \brief Fix any ugly branch statements (from propagating too much)
     ******************************************************************************/
    void fixUglyBranches();

    void placePhiFunctions() { m_df.placePhiFunctions(this); }

    /***************************************************************************/ /**
     * \brief Rename block variables, with log if verbose.
     * \returns true if a change
     ******************************************************************************/
    bool doRenameBlockVars(int pass, bool clearStacks = false);

    bool canRename(SharedExp e) { return m_df.canRename(e, this); }

    Statement *getStmtAtLex(unsigned int begin, unsigned int end);

    /***************************************************************************/ /**
     * \brief Initialise the statements, e.g. proc, bb pointers
     ******************************************************************************/
    void initStatements();
    void numberStatements();
    bool nameStackLocations();
    void removeRedundantPhis();

    /***************************************************************************/ /**
     * \brief  Was trimReturns()
     ******************************************************************************/
    void findPreserveds();

    /***************************************************************************/ /**
     * \brief Preservations only for the stack pointer
     ******************************************************************************/
    void findSpPreservation();
    void removeSpAssignsIfPossible();
    void removeMatchingAssignsIfPossible(SharedExp e);
    void updateReturnTypes();

    /***************************************************************************/ /**
     * \brief  Perform call and phi statement bypassing at all depths
     ******************************************************************************/
    void fixCallAndPhiRefs();

    /***************************************************************************/ /**
     * \brief Get the initial parameters, based on this UserProc's use collector
     * Probably unused now
     ******************************************************************************/
    void initialParameters();

    /// Map expressions to locals and initial parameters
    void mapLocalsAndParams();

    /**
     * Search for expressions without explicit definitions (i.e. WILDCARD{0}),
     * which represent parameters (use before definition).
     * These are called final parameters, because they are determined
     * from implicit references, not from the use collector at the start of the proc,
     * which include some caused by recursive calls
     */
    void findFinalParameters();

    int nextParamNum() { return ++m_nextParam; }

    /// Add the parameter to the signature
    void addParameter(SharedExp e, SharedType ty);

    /***************************************************************************/ /**
     * \brief Insert into parameters list correctly sorted
     * Update the parameters, in case the signature and hence ordering and filtering has changed, or the locations in the
     * collector have changed
     ******************************************************************************/
    void insertParameter(SharedExp e, SharedType ty);

    /// Update the arguments in calls
    void updateArguments();

    /// Update the defines in calls
    void updateCallDefines();

    /// Replace simple global constant references
    /// Statement level transform :
    /// PREDICATE: (statement IS_A Assign) AND (statement.rhs IS_A MemOf) AND (statement.rhs.sub(1) IS_A IntConst)
    /// ACTION:
    ///     $tmp_addr = assgn.rhs.sub(1);
    ///     $tmp_val  = prog->readNative($tmp_addr,statement.type.bitwidth/8);
    ///     statement.rhs.replace_with(Const($tmp_val))
    void replaceSimpleGlobalConstants();
    void reverseStrengthReduction();

    void trimParameters(int depth = -1);
    void processFloatConstants();

    /// Not used with DFA Type Analysis; the equivalent thing happens in mapLocalsAndParams() now
    void mapExpressionsToLocals(bool lastPass = false);
    void addParameterSymbols();

    /// True if e represents a stack local variable
    bool isLocal(SharedExp e) const;

    /// True if e represents a stack local or stack param
    bool isLocalOrParam(const SharedExp& e) const;

    /// Is this m[sp{-} +/- K]?
    /// True if e could represent a stack local or stack param
    bool isLocalOrParamPattern(const SharedExp& e);

    /// True if a local exists with name \a name
    bool existsLocal(const QString& name) const;

    bool isAddressEscapedVar(const SharedExp& e) const { return m_addressEscapedVars.exists(e); }

    /// True if e can be propagated
    bool isPropagatable(const SharedExp& e) const;

    /**
     * Find the procs the calls point to.
     * To be called after decoding all procs.
     */
    /// find the procs the calls point to
    void assignProcsToCalls();

    /**
     * Perform final simplifications
     */
    /// perform final simplifications
    void finalSimplify();

    /// eliminate duplicate arguments
    void eliminateDuplicateArgs();

private:
    void searchRegularLocals(OPER minusOrPlus, bool lastPass, int sp, StatementList& stmts);

    /// Return a string for a new local suitable for \a e
    QString newLocalName(const SharedExp& e);

public:
    bool removeNullStatements();
    bool removeDeadStatements();

    /// Count references to the things that are under SSA control.
    /// For each SSA subscripting, increment a counter for that definition
    void countRefs(RefCounter& refCounts);

    /// Remove unused statements.
    void remUnusedStmtEtc();
    void remUnusedStmtEtc(RefCounter& refCounts /* , int depth*/);

    /// Note: call the below after translating from SSA form
    /// FIXME: this can be done before transforming out of SSA form now, surely...
    void removeUnusedLocals();
    void mapTempsToLocals();

    const std::map<QString, SharedType>& getLocals() const { return m_locals; }

    /// Remove all liveness info in UseCollectors in calls
    void removeCallLiveness();
    bool propagateAndRemoveStatements();

    /// Propagate statements, but don't remove
    /// Return true if change; set convert if an indirect call is converted to direct (else clear)
    /// Propagate statemtents; return true if change; set convert if an indirect call is converted to direct
    /// (else clear)
    bool propagateStatements(bool& convert, int pass);
    void findLiveAtDomPhi(LocationSet& usedByDomPhi);

#if USE_DOMINANCE_NUMS
    void setDominanceNumbers();
#endif

    /// Propagate into xxx of m[xxx] in the UseCollector (locations live at the entry of this proc)
    void propagateToCollector();

    /// Clear the useCollectors (in this Proc, and all calls).
    void clearUses();

    // int        findMaxDepth();                    ///< Find max memory nesting depth.

    void fromSSAform();

    /// Find the locations united by Phi-functions
    void findPhiUnites(ConnectionGraph& pu);
    void insertAssignAfter(Statement *s, SharedExp left, SharedExp right);
    void removeSubscriptsFromSymbols();
    void removeSubscriptsFromParameters();

    /// Insert statement \a a after statement \a s.
    /// \note this procedure is designed for the front end, where enclosing BBs are not set up yet.
    /// So this is an inefficient linear search!
    void insertStatementAfter(Statement *s, Statement *a);

    /***************************************************************************/ /**
     * \brief Add a mapping for the destinations of phi functions that have one
     * argument that is a parameter
     *
     * The idea here is to give a name to those SSA variables that have one and only one parameter amongst the phi
     * arguments. For example, in test/source/param1, there is 18 *v* m[r28{-} + 8] := phi{- 7} with m[r28{-} + 8]{0}
     * mapped
     * to param1; insert a mapping for m[r28{-} + 8]{18} to param1. This will avoid a copy, and will use the name of the
     * parameter only when it is acually used as a parameter
     ******************************************************************************/
    void nameParameterPhis();
    void mapParameters();

    void conTypeAnalysis();
    void dfaTypeAnalysis();

    /***************************************************************************/ /**
     * \brief Trim parameters to procedure calls with ellipsis (...).
     * Also add types for ellipsis parameters, if any
     * \returns true if any signature types so added.
     ******************************************************************************/
    bool ellipsisProcessing();

    /***************************************************************************/ /**
     * \brief Used for checking for unused parameters
     *
     * Remove the unused parameters. Check for uses for each parameter as param{0}.
     * Some parameters are apparently used when in fact they are only used as parameters to calls to procedures in the
     * recursion set. So don't count components of arguments of calls in the current recursion group that chain through to
     * ultimately use the argument as a parameter to the current procedure.
     * Some parameters are apparently used when in fact they are only used by phi statements which transmit a return from
     * a recursive call ultimately to the current procedure, to the exit of the current procedure, and the return exists
     * only because of a liveness created by a parameter to a recursive call. So when examining phi statements, check if
     * referenced from a return of the current procedure, and has an implicit operand, and all the others satisfy a call
     * to doesReturnChainToCall(param, this proc).
     * but not including removing unused statements.
     *
     * \param param - Exp to check
     * \param p - our caller?
     * \param visited - a set of procs already visited, to prevent infinite recursion
     * \returns true/false :P
     ******************************************************************************/
    bool doesParamChainToCall(SharedExp param, UserProc *p, ProcSet *visited);
    bool isRetNonFakeUsed(CallStatement *c, SharedExp loc, UserProc *p, ProcSet *Visited);

    /// Remove redundant parameters. Return true if remove any
    bool removeRedundantParameters();

    /***************************************************************************/ /**
     * \brief Remove any returns that are not used by any callers
     *
     * Remove unused returns for this procedure, based on the equation:
     * returns = modifieds isect union(live at c) for all c calling this procedure.
     * The intersection operation will only remove locations. Removing returns can have three effects for each component
     * y used by that return (e.g. if return r24 := r25{10} + r26{20} is removed, statements 10 and 20 will be affected
     * and y will take the values r25{10} and r26{20}):
     * 1) a statement s defining a return becomes unused if the only use of its definition was y
     * 2) a call statement c defining y will no longer have y live if the return was the only use of y. This could cause a
     *    change to the returns of c's destination, so removeRedundantReturns has to be called for c's destination proc (if
     * it
     *    turns out to be the only definition, and that proc was not already scheduled for return removing).
     * 3) if y is a parameter (i.e. y is of the form loc{0}), then the signature of this procedure changes, and all callers
     *    have to have their arguments trimmed, and a similar process has to be applied to all those caller's removed
     *    arguments as is applied here to the removed returns.
     * The \a removeRetSet is the set of procedures to process with this logic; caller in Prog calls all elements in this
     * set (only add procs to this set, never remove)
     *
     * \returns true if any change
     ******************************************************************************/
    bool removeRedundantReturns(std::set<UserProc *>& removeRetSet);

    // Check for a gainful use of bparam{0} in this proc. Return with true when the first such use is found.
    // Ignore uses in return statements of recursive functions, and phi statements that define them
    // Procs in visited are already visited
    ///         Reurn true if location e is used gainfully in this procedure. visited is a set of UserProcs already
    ///            visited.
    bool checkForGainfulUse(SharedExp e, ProcSet& Visited);

    /***************************************************************************/ /**
     * \brief Update parameters and call livenesses to take into account the changes
     * causes by removing a return from this procedure, or a callee's parameter
     * (which affects this procedure's arguments, which are also uses).
     *
     * Need to save the old parameters and call livenesses, redo the dataflow and
     * removal of unused statements, recalculate the parameters and call livenesses,
     * and if either or both of these are changed, recurse to parents or those calls'
     * children respectively. (When call livenesses change like this, it means that
     * the recently removed return was the only use of that liveness, i.e. there was a
     * return chain.)
     * \sa removeRedundantReturns().
     ******************************************************************************/
    void updateForUseChange(std::set<UserProc *>& removeRetSet);

    /// this function was non-reentrant, but now reentrancy is frequently used
    /// prove any arbitary property of this procedure. If conditional is true, do not save the result, as it may
    /// be conditional on premises stored in other procedures
    bool prove(const std::shared_ptr<Binary>& query, bool conditional = false);

    /// helper function, should be private
    bool prover(SharedExp query, std::set<PhiAssign *>& lastPhis, std::map<PhiAssign *, SharedExp>& cache,
                PhiAssign *lastPhi = nullptr);

    /// promote the signature if possible
    void promoteSignature();

    /// get all statements
    /// Get to a statement list, so they come out in a reasonable and consistent order
    /// get all the statements
    void getStatements(StatementList& stmts) const;
    virtual void removeReturn(SharedExp e) override;
    void removeStatement(Statement *stmt);
    bool searchAll(const Exp& search, std::list<SharedExp>& result);

    // Get the set of locations defined by this proc.
    // In other words, the define set, currently called returns
    void getDefinitions(LocationSet& defs);

    /***************************************************************************/ /**
     * Before Type Analysis, refs like r28{0} have a nullptr Statement pointer. After this, they will point to an
     * implicit assignment for the location. Thus, during and after type analysis, you can find the type of any
     * location by following the reference to the definition
     * Note: you need something recursive to make sure that child subexpressions are processed before parents
     * Example: m[r28{0} - 12]{0} could end up adding an implicit assignment for r28{-} with a null reference, when other
     * pieces of code add r28{0}
     ******************************************************************************/
    void addImplicitAssigns();
    void makeSymbolsImplicit();
    void makeParamsImplicit();

    StatementList& getParameters() { return m_parameters; }
    StatementList& getModifieds() { return theReturnStatement->getModifieds(); }


    /// Return an expression that is equivilent to e in terms of symbols.
    /// Creates new symbols as needed.

    /**
     * Return an expression that is equivilent to e in terms of local variables.
     * Creates new locals as needed.
     */
    SharedExp getSymbolExp(SharedExp le, SharedType ty = nullptr, bool lastPass = false);

    /**
     * Return the next available local variable; make it the given type. Note: was returning TypedExp*.
     * If nam is non null, use that name
     */
    SharedExp newLocal(SharedType ty, const SharedExp& e, char *nam = nullptr);

    /**
     * Add a new local supplying all needed information.
     */
    void addLocal(SharedType ty, const QString& nam, SharedExp e);

    /// return a local's type
    SharedType getLocalType(const QString& nam);
    void setLocalType(const QString& nam, SharedType ty);
    SharedType getParamType(const QString& nam);

    /// return a symbol's exp (note: the original exp, like r24, not local1)
    /// \note linear search!!
    SharedConstExp expFromSymbol(const QString& nam) const;

    void mapSymbolTo(const SharedConstExp& from, SharedExp to);

    /// As above but with replacement
    void mapSymbolToRepl(const SharedConstExp& from, SharedExp oldTo, SharedExp newTo);

    /// Remove this mapping
    void removeSymbolMapping(const SharedConstExp& from, SharedExp to);

    // FIXME: is this the same as lookupSym() now?
    /// \brief Lookup the symbol map considering type
    /// Lookup the expression in the symbol map. Return nullptr or a C string with the symbol. Use the Type* ty to
    /// select from several names in the multimap; the name corresponding to the first compatible type is returned
    SharedExp getSymbolFor(const SharedConstExp& e, SharedType ty);

    QString lookupSym(const SharedConstExp& e, SharedType ty);

    /// Lookup a specific symbol for the given ref
    QString lookupSymFromRef(const std::shared_ptr<RefExp>& r);

    /// Lookup a specific symbol if any, else the general one if any
    QString lookupSymFromRefAny(const std::shared_ptr<RefExp>& r);

    /// e is a parameter location, e.g. r8 or m[r28{0}+8]. Lookup a symbol for it
    /// Find the implicit definition for \a e and lookup a symbol
    QString lookupParam(SharedExp e);

    /// Check if \a r is already mapped to a local, else add one
    void checkLocalFor(const std::shared_ptr<RefExp>& r);

    /// Find the type of the local or parameter \a e
    SharedType getTypeForLocation(const SharedConstExp& e);
    const SharedType getTypeForLocation(const SharedConstExp& e) const;

    /// Determine whether e is a local, either as a true opLocal (e.g. generated by fromSSA), or if it is in the
    /// symbol map and the name is in the locals map. If it is a local, return its name, else nullptr
    QString findLocal(const SharedExp& e, SharedType ty);
    QString findLocalFromRef(const std::shared_ptr<RefExp>& r);
    QString findFirstSymbol(const SharedExp& e);

    size_t getNumLocals() { return m_locals.size(); }
    QString getLocalName(int n);

    /// As getLocalName, but look for expression \a e
    QString getSymbolName(SharedExp e);
    void renameLocal(const char *oldName, const char *newName);
    virtual void renameParam(const char *oldName, const char *newName) override;

    /// WARN: write tests for getRegName in all combinations of r[1] r[tmp+1] etc.
    /// Get a name like eax or o2 from r24 or r8
    QString getRegName(SharedExp r);

    void setParamType(const char *nam, SharedType ty);
    void setParamType(int idx, SharedType ty);

    /***************************************************************************/ /**
     * \brief    Get the BB with the entry point address for this procedure
     * \note (not always the first BB)
     * \returns   Pointer to the entry point BB, or nullptr if not found
     ******************************************************************************/
    BasicBlock *getEntryBB();

    /***************************************************************************/ /**
     * \brief        Set the entry BB for this procedure (constructor has the entry address)
     ******************************************************************************/
    void setEntryBB();

    /// Get the callees.
    std::list<Function *>& getCallees() { return m_calleeList; }

    /***************************************************************************/ /**
     * \brief Add this callee to the set of callees for this proc
     * \param  callee - A pointer to the Proc object for the callee
     ******************************************************************************/
    void addCallee(Function *callee);

    /***************************************************************************/ /**
     * \brief Return true if this procedure contains the given address
     * \param uAddr address to search for
     * \returns          true if it does
     ******************************************************************************/
    bool containsAddr(Address uAddr) const;

    /// Change BB containing this statement from a COMPCALL to a CALL.
    void undoComputedBB(Statement *stmt) const { m_cfg->undoComputedBB(stmt); }

    /// \copydoc Function::getProven
    virtual SharedExp getProven(SharedExp left) override;

    /// \copydoc Function::getPremised
    virtual SharedExp getPremised(SharedExp left) override;

    /// Set a location as a new premise, i.e. assume e=e
    void setPremise(SharedExp e)
    {
        e = e->clone();
        m_recurPremises[e] = e;
    }

    void killPremise(const SharedExp& e) { m_recurPremises.erase(e); }

    /// \copydoc Function::isPreserved
    virtual bool isPreserved(SharedExp e) override;

    /// \copydoc Function::printCallGraphXML
    virtual void printCallGraphXML(QTextStream& os, int depth, bool recurse = true) override;

    void printDecodedXML();
    void printAnalysedXML();
    void printSSAXML();
    void printXML();
    void printUseGraph();

    bool searchAndReplace(const Exp& search, SharedExp replace);

    /// Cast the constant whose conscript is num to be type ty
    void castConst(int num, SharedType ty);

    /// Add a location to the UseCollector; this means this location is used before defined,
    /// and hence is an *initial* parameter.
    /// \note final parameters don't use this information; it's only for handling recursion.
    void useBeforeDefine(const SharedExp& loc) { m_procUseCollector.insert(loc); }

    /***************************************************************************/ /**
     * \brief Copy the decoded indirect control transfer instructions' RTLs to
     * the front end's map, and decode any new targets for this CFG
     *
     * Copy the RTLs for the already decoded Indirect Control Transfer instructions, and decode any new targets in this CFG
     * Note that we have to delay the new target decoding till now, because otherwise we will attempt to decode nested
     * switch statements without having any SSA renaming, propagation, etc
     ******************************************************************************/
    void processDecodedICTs();

public:
    Address getTheReturnAddr() { return theReturnStatement == nullptr ? Address::INVALID : theReturnStatement->getRetAddr(); }
    void setTheReturnAddr(ReturnStatement *s, Address r)
    {
        assert(theReturnStatement == nullptr);
        theReturnStatement = s;
        theReturnStatement->setRetAddr(r);
    }

    ReturnStatement *getTheReturnStatement() { return theReturnStatement; }

    /***************************************************************************/ /**
     * \brief Decide whether to filter out \a e (return true) or keep it
     * Filter out locations not possible as return locations. Return true to *remove* (filter *out*)
     * \returns true if \a e  should be filtered out
     ******************************************************************************/
    bool filterReturns(SharedExp e);

    /***************************************************************************/ /**
     * \brief Decide whether to filter out \a e (return true) or keep it
     * Filter out locations not possible as parameters or arguments. Return true to remove
     * \returns true if \a e  should be filtered out
     * \sa UserProc::filterReturns
     ******************************************************************************/
    bool filterParams(SharedExp e);

    /// Find or insert a new implicit reference just before statement s, for address expression a with type t.
    /// Meet types if necessary
    /// Find and if necessary insert an implicit reference before s whose address expression is a and type is t.
    void setImplicitRef(Statement *s, SharedExp a, SharedType ty);

    void verifyPHIs();
    void debugPrintAll(const char *c);

protected:
    UserProc();
    void setCFG(Cfg *c) { m_cfg = c; }

private:
    SymbolMap m_symbolMap;

    /// The local "symbol table", which is aware of overlaps
    DataIntervalMap m_localTable;

    /// Set of callees (Procedures that this procedure calls). Used for call graph, among other things
    std::list<Function *> m_calleeList;

    /**
     * A collector for initial parameters (locations used before being defined).
     * Note that final parameters don't use this;
     * it's only of use during group decompilation analysis (sorting out recursion)
     */
    UseCollector m_procUseCollector;

    /**
     * The list of parameters, ordered and filtered.
     * Note that a LocationList could be used, but then there would be nowhere to store the types (for DFA based TA)
     * The RHS is just ignored; the list is of ImplicitAssigns.
     * DESIGN ISSUE: it would be nice for the parameters' implicit assignments to be the sole definitions, i.e. not
     * need other implicit assignments for these. But the targets of RefExp's are not expected to change address,
     * so they are not suitable at present (since the addresses regularly get changed as the parameters get
     * recreated).
     */
    StatementList m_parameters;

    /// The set of address-escaped locals and parameters. If in this list, they should not be propagated
    LocationSet m_addressEscapedVars;

    // The modifieds for the procedure are now stored in the return statement

    /// DataFlow object. Holds information relevant to transforming to and from SSA form.
    DataFlow m_df;
    int m_stmtNumber; ///< Current statement number. Makes it easier to split decompile() into smaller pieces.

    /**
     * Pointer to a set of procedures involved in a recursion group.
     * \note Each procedure in the cycle points to the same set! However, there can be several separate cycles.
     * E.g. in test/source/recursion.c, there is a cycle with f and g, while another is being built up (it only
     * has c, d, and e at the point where the f-g cycle is found).
     */
    std::shared_ptr<ProcSet> m_cycleGroup;

private:
    /**
     * We ensure that there is only one return statement now.
     * See code in frontend/frontend.cpp handling case STMT_RET.
     * If no return statement, this will be nullptr.
     */
    ReturnStatement *theReturnStatement;
    mutable int DFGcount; ///< used in dotty output

private:
    Cfg *m_cfg; ///< The control flow graph.

    /**
     * The status of this user procedure.
     * Status: undecoded .. final decompiled
     */
    ProcStatus m_status;

    /*
     * Somewhat DEPRECATED now. Eventually use the localTable.
     * This map records the names and types for local variables. It should be a subset of the symbolMap, which also
     * stores parameters.
     * It is a convenient place to store the types of locals after conversion from SSA form,
     * since it is then difficult to access the definitions of locations.
     * This map could be combined with symbolMap below, but beware of parameters
     * (in symbols but not locals)
     */
    std::map<QString, SharedType> m_locals;

    int m_nextLocal = 0; ///< Number of the next local. Can't use locals.size() because some get deleted
    int m_nextParam = 0; ///< Number for param1, param2, etc
};
