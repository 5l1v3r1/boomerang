/*
 * Copyright (C) 1998-2001, The University of Queensland
 * Copyright (C) 2000-2001, Sun Microsystems, Inc
 * Copyright (C) 2002-2006, Trent Waddington and Mike Van Emmerik
 *
 * See the file "LICENSE.TERMS" for information on usage and
 * redistribution of this file, and for a DISCLAIMER OF ALL
 * WARRANTIES.
 *
 */

/***************************************************************************//**
 * \file       proc.h
 * OVERVIEW:   Interface for the procedure classes, which are used to store information about variables in the
 *                procedure such as parameters and locals.
 *============================================================================*/

/*
 *    $Revision$    // 1.115.2.27
 */

#ifndef _PROC_H_
#define _PROC_H_

#include <list>
#include <vector>
#include <map>
#include <set>
#include <string>
#include <assert.h>
#include "exp.h"                // For lessExpStar
#include "cfg.h"                // For cfg->simplify()
//#include "hllcode.h"
#include "memo.h"
#include "dataflow.h"            // For class UseCollector
#include "statement.h"            // For embedded ReturnStatement pointer, etc

class Prog;
class UserProc;
class Cfg;
class BasicBlock;
class Exp;
class TypedExp;
class lessTI;
class Type;
class RTL;
class HLLCode;
class SyntaxNode;
class Parameter;
class Argument;
class Signature;
class Cluster;
class XMLProgParser;

/***************************************************************************//**
 * Procedure class.
 *============================================================================*/
/// Interface for the procedure classes, which are used to store information about variables in the
/// procedure such as parameters and locals.
class Proc {
protected:
    friend class XMLProgParser;
public:
                        Proc(Prog *prog, ADDRESS uNative, Signature *sig);
virtual                 ~Proc();

        const char*     getName() const;
        void            setName(const std::string &nam);
        ADDRESS         getNativeAddress() const;
        void            setNativeAddress(ADDRESS a);
        Prog *          getProg() { return prog; } //!< Get the program this procedure belongs to.
        void            setProg(Prog *p) { prog = p; }
        Proc *          getFirstCaller();
                        //! Set the first procedure that calls this procedure (or null for main/start).
        void            setFirstCaller(Proc *p) { if (m_firstCaller == nullptr) m_firstCaller = p; }
        Signature *     getSignature() { return signature; } //!< Returns a pointer to the Signature
        void            setSignature(Signature *sig) { signature = sig; }

virtual void            renameParam(const char *oldName, const char *newName);

//virtual std::ostream& put(std::ostream& os) = 0;

        void            matchParams(std::list<Exp*>& actuals, UserProc& caller);

        std::list<Type>* getParamTypeList(const std::list<Exp*>& actuals);
virtual bool            isLib() {return false;} //!< Return true if this is a library proc
virtual bool            isNoReturn() = 0; //!< Return true if this procedure doesn't return

        /**
         * OutPut operator for a Proc object.
         */
        friend std::ostream& operator<<(std::ostream& os, const Proc& proc);

virtual Exp *           getProven(Exp *left) = 0;       //!< Get the RHS, if any, that is proven for left
virtual Exp *           getPremised(Exp *left) = 0;     //!< Get the RHS, if any, that is premised for left
virtual bool            isPreserved(Exp* e) = 0;        //!< Return whether e is preserved by this proc
        void            setProvenTrue(Exp* fact);

        /**
         * Get the callers
         * Note: the callers will be in a random order (determined by memory allocation)
         */
        std::set<CallStatement*>& getCallers() { return callerSet; }

        //! Add to the set of callers
        void            addCaller(CallStatement* caller) { callerSet.insert(caller); }
        void            addCallers(std::set<UserProc*>& callers);

        void            removeParameter(Exp *e);
virtual void            removeReturn(Exp *e);
//virtual void        addReturn(Exp *e);
//        void        sortParameters();

virtual void            printCallGraphXML(std::ostream &os, int depth, bool recurse = true);
        void            printDetailsXML();
        void            clearVisited() { visited = false; }
        bool            isVisited() { return visited; }

        Cluster *       getCluster() { return cluster; }
        void            setCluster(Cluster *c) { cluster = c; }

protected:

typedef std::map<Exp*, Exp*, lessExpStar> mExpExp;
        bool            visited;
        Prog *          prog;
        Signature *     signature;
        ///////////////////////////////////////////////////
        // Persistent state
        ///////////////////////////////////////////////////
        ADDRESS         address;
        Proc *          m_firstCaller;
        ADDRESS         m_firstCallerAddr;
        // FIXME: shouldn't provenTrue be in UserProc, with logic associated with the signature doing the equivalent thing for LibProcs?
        mExpExp         provenTrue;
        // Cache of queries proven false (to save time)
        // mExpExp provenFalse;
        mExpExp         recurPremises;
        std::set<CallStatement*> callerSet;
        Cluster *       cluster;

                        Proc() : visited(false), prog(nullptr), signature(nullptr), address(ADDRESS::g(0L)),
                            m_firstCaller(nullptr), m_firstCallerAddr(ADDRESS::g(0L)),
                            cluster(nullptr)
                        { }
};    // class Proc

/***************************************************************************//**
 * LibProc class.
 *============================================================================*/
class LibProc : public Proc {
protected:
friend  class XMLProgParser;
public:

                    LibProc(Prog *prog, std::string& name, ADDRESS address);
virtual                ~LibProc();
        bool        isLib() {return true;} //!< Return true, since is a library proc
virtual bool        isNoReturn();
virtual Exp*        getProven(Exp* left);
virtual Exp*        getPremised(Exp* /*left*/) {return nullptr;}   //!< Get the RHS that is premised for left
virtual bool        isPreserved(Exp* e);                    //!< Return whether e is preserved by this proc
        //std::ostream& put(std::ostream& os); //!< Prints this procedure to an output stream.
        void        getInternalStatements(StatementList &internal);
protected:
                    LibProc() : Proc() { }
};

enum ProcStatus {
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

typedef std::set <UserProc*> ProcSet;
typedef std::list<UserProc*> ProcList;

/***************************************************************************//**
 * UserProc class.
 *============================================================================*/

class UserProc : public Proc {
protected:
friend  class XMLProgParser;
        Cfg*        cfg; //!< The control flow graph.

        /**
         * The status of this user procedure.
         * Status: undecoded .. final decompiled
         */
        ProcStatus    status;

        /*
         * Somewhat DEPRECATED now. Eventually use the localTable.
         * This map records the names and types for local variables. It should be a subset of the symbolMap, which also
         * stores parameters.
         * It is a convenient place to store the types of locals after
         * conversion from SSA form, since it is then difficult to access the definitions of locations.
         * This map could be combined with symbolMap below, but beware of parameters (in symbols but not locals)
         */
        std::map<std::string, Type*> locals;

        int            nextLocal;        //!< Number of the next local. Can't use locals.size() because some get deleted
        int            nextParam;        //!< Number for param1, param2, etc

public:
        /**
         * A map between machine dependent locations and their corresponding symbolic, machine independent
         * representations.  Example: m[r28{0} - 8] -> local5; this means that *after* transforming out of SSA
         * form, any locations not specifically mapped otherwise (e.g. m[r28{0} - 8]{55} -> local6) will get this
         * name.
         * It is a *multi*map because one location can have several default names differentiated by type.
         * E.g. r24 -> eax for int, r24 -> eax_1 for float
         */
        typedef std::multimap<const Exp*,Exp*,lessExpStar> SymbolMap;
private:
        SymbolMap    symbolMap;
        /**
         * The local "symbol table", which is aware of overlaps
         */
        DataIntervalMap     localTable;

        /**
         * Set of callees (Procedures that this procedure calls). Used for call graph, among other things
         */
        std::list<Proc*>    calleeList;
        UseCollector        col;
        StatementList       parameters;

        /**
         * The set of address-escaped locals and parameters. If in this list, they should not be propagated
         */
        LocationSet         addressEscapedVars;

        // The modifieds for the procedure are now stored in the return statement

        /**
         * DataFlow object. Holds information relevant to transforming to and from SSA form.
         */
        DataFlow            df;
        int                 stmtNumber;
        ProcSet *           cycleGrp;
public:
                            UserProc(Prog *prog, std::string& name, ADDRESS address);
virtual                     ~UserProc();
        void                setDecoded();
        void                unDecode();
                            //! Returns a pointer to the CFG object.
        Cfg *               getCFG() { return cfg; }
                            //! Returns a pointer to the DataFlow object.
        DataFlow *          getDataFlow() {return &df;}
        void                deleteCFG();
virtual bool                isNoReturn();

        SyntaxNode *        getAST();
        void                printAST(SyntaxNode *a = nullptr);

                            //! Returns whether or not this procedure can be decoded (i.e. has it already been decoded).
        bool                isDecoded() { return status >= PROC_DECODED; }
        bool                isDecompiled() { return status >= PROC_FINAL; }
        bool                isEarlyRecursive() const {return cycleGrp != nullptr && status <= PROC_INCYCLE;}
        bool                doesRecurseTo(UserProc* p) {return cycleGrp && cycleGrp->find(p) != cycleGrp->end();}

        bool                isSorted() { return status >= PROC_SORTED; }
        void                setSorted() { setStatus(PROC_SORTED); }

        ProcStatus          getStatus() { return status; }
        void                setStatus(ProcStatus s);
        void                generateCode(HLLCode *hll);

        void                print(std::ostream &out, bool html = false) const;
        void                printParams(std::ostream &out, bool html = false) const;
        char *              prints();
        void                dump();

        void                printDFG() const;
        void                printSymbolMap(std::ostream& out, bool html = false) const;
        void                dumpSymbolMap();
        void                dumpSymbolMapx();
        void                testSymbolMap();
        void                dumpLocals(std::ostream& os, bool html = false) const;
        void                dumpLocals();
                            //! simplify the statements in this proc
        void                simplify() { cfg->simplify(); }
        ProcSet*            decompile(ProcList* path, int& indent);
        void                initialiseDecompile();
        void                earlyDecompile();
        ProcSet*            middleDecompile(ProcList* path, int indent);
        void                recursionGroupAnalysis(ProcList* path, int indent);

        void                typeAnalysis();
        void                rangeAnalysis();
        void                logSuspectMemoryDefs();
        // Split the set of cycle-associated procs into individual subcycles.
        //void        findSubCycles(CycleList& path, CycleSet& cs, CycleSetSet& sset);

        bool                inductivePreservation(UserProc* topOfCycle);
        void                markAsNonChildless(ProcSet* cs);

        void                updateCalls();
        void                branchAnalysis();
        void                fixUglyBranches();
        void                placePhiFunctions() {df.placePhiFunctions(this);}
        bool                doRenameBlockVars(int pass, bool clearStacks = false);
        bool                canRename(Exp* e) {return df.canRename(e, this);}

        Statement *         getStmtAtLex(unsigned int begin, unsigned int end);


        void                initStatements();
        void                numberStatements();
        bool                nameStackLocations();
        void                removeRedundantPhis();
        void                findPreserveds();
        void                findSpPreservation();
        void                removeSpAssignsIfPossible();
        void                removeMatchingAssignsIfPossible(Exp *e);
        void                updateReturnTypes();
        void                fixCallAndPhiRefs();
        void                initialParameters();
        void                mapLocalsAndParams();
        void                findFinalParameters();
        int                 nextParamNum() {return ++nextParam;}
        void                addParameter(Exp *e, Type* ty);
        void                insertParameter(Exp* e, Type* ty);
//        void        addNewReturns(int depth);
        void                updateArguments();
        void                updateCallDefines();
        void                replaceSimpleGlobalConstants();
        void                reverseStrengthReduction();

        void                trimParameters(int depth = -1);
        void                processFloatConstants();
        //void        mapExpressionsToParameters();   ///< must be in SSA form
        void                mapExpressionsToLocals(bool lastPass = false);
        void                addParameterSymbols();
        bool                isLocal(Exp* e);
        bool                isLocalOrParam(Exp* e);
        bool                isLocalOrParamPattern(Exp* e);
        bool                existsLocal(const char *name);
        bool                isAddressEscapedVar(Exp* e) {return addressEscapedVars.exists(e);}
        bool                isPropagatable(Exp* e);
        void                assignProcsToCalls();
        void                finalSimplify();
        void                eliminateDuplicateArgs();

private:
        void                searchRegularLocals(OPER minusOrPlus, bool lastPass, int sp, StatementList& stmts);
        const char *        newLocalName(Exp* e);
public:
        bool                removeNullStatements();
        bool                removeDeadStatements();
typedef std::map<Statement*, int> RefCounter;
        void                countRefs(RefCounter& refCounts);

        void                remUnusedStmtEtc();
        void                remUnusedStmtEtc(RefCounter& refCounts /* , int depth*/);
        void                removeUnusedLocals();
        void                mapTempsToLocals();
        void                removeCallLiveness();
        bool                propagateAndRemoveStatements();
        bool                propagateStatements(bool& convert, int pass);
        void                findLiveAtDomPhi(LocationSet& usedByDomPhi);
#if        USE_DOMINANCE_NUMS
        void                setDominanceNumbers();
#endif
        void                propagateToCollector();
        void                clearUses();
        void                clearRanges();
        //int        findMaxDepth();                    ///< Find max memory nesting depth.

        void                fromSSAform();
        void                findPhiUnites(ConnectionGraph& pu);
        void                insertAssignAfter(Statement* s, Exp* left, Exp* right);
        void                removeSubscriptsFromSymbols();
        void                removeSubscriptsFromParameters();

        void                insertStatementAfter(Statement* s, Statement* a);
        void                nameParameterPhis();
        void                mapParameters();

        void                conTypeAnalysis();
        void                dfaTypeAnalysis();
        void                dfa_analyze_scaled_array_ref( Statement* s, Prog* prog );

        void                dfa_analyze_implict_assigns( Statement* s, Prog* prog );
        bool                ellipsisProcessing();

        // For the final pass of removing returns that are never used
//typedef    std::map<UserProc*, std::set<Exp*, lessExpStar> > ReturnCounter;
        bool                doesParamChainToCall(Exp* param, UserProc* p, ProcSet* visited);
        bool                isRetNonFakeUsed(CallStatement* c, Exp* loc, UserProc* p, ProcSet* visited);

        bool                removeRedundantParameters();
        bool                removeRedundantReturns(std::set<UserProc*>& removeRetSet);
        bool                checkForGainfulUse(Exp* e, ProcSet& visited);
        void                updateForUseChange(std::set<UserProc*>& removeRetSet);
        bool                prove(Exp *query, bool conditional = false);

        bool                prover(Exp *query, std::set<PhiAssign*> &lastPhis, std::map<PhiAssign*, Exp*> &cache,
                                   Exp* original, PhiAssign *lastPhi = nullptr);
        void                promoteSignature();
        void                getStatements(StatementList &stmts) const;
virtual void                removeReturn(Exp *e);
        void                removeStatement(Statement *stmt);
        bool                searchAll(Exp* search, std::list<Exp*> &result);

        void                getDefinitions(LocationSet &defs);
        void                addImplicitAssigns();
        void                makeSymbolsImplicit();
        void                makeParamsImplicit();
        StatementList&      getParameters() { return parameters; }
        StatementList&      getModifieds() { return theReturnStatement->getModifieds(); }
public:
        Exp *               getSymbolExp(Exp *le, Type *ty = nullptr, bool lastPass = false);
        Exp *               newLocal(Type* ty, Exp* e, char* nam = nullptr);
        void                addLocal(Type *ty, const char *nam, Exp *e);
        Type *              getLocalType(const char *nam);
        void                setLocalType(const char *nam, Type *ty);
        Type *              getParamType(const char *nam);
        const Exp *         expFromSymbol(const char *nam) const;
        void                mapSymbolTo(const Exp *from, Exp* to);
        void                mapSymbolToRepl(const Exp* from, Exp* oldTo, Exp* newTo);
        void                removeSymbolMapping(const Exp *from, Exp* to);
        Exp *               getSymbolFor(const Exp *e, Type* ty);
        const char *        lookupSym(const Exp *e, Type* ty);
        const char *        lookupSymFromRef(RefExp* r);
        const char *        lookupSymFromRefAny(RefExp* r);
        const char *        lookupParam(Exp* e);
        void                checkLocalFor(RefExp* r);
        Type *              getTypeForLocation(const Exp *e);
        const Type *        getTypeForLocation(const Exp *e) const;
        const char *        findLocal(Exp* e, Type* ty);
        const char *        findLocalFromRef(RefExp* r);
        const char *        findFirstSymbol(Exp* e);
        int                 getNumLocals() { return (int)locals.size(); }
        const char *        getLocalName(int n);
        const char *        getSymbolName(Exp* e);
        void                renameLocal(const char *oldName, const char *newName);
virtual void                renameParam(const char *oldName, const char *newName);

        const char *        getRegName(Exp* r);
        void                setParamType(const char* nam, Type* ty);
        void                setParamType(int idx, Type* ty);

        BasicBlock *        getEntryBB();
        void                setEntryBB();
                            //! Get the callees.
        std::list<Proc*>&   getCallees() { return calleeList; }
        void                addCallee(Proc* callee);
        void                addCallees(std::list<UserProc*>& callees);
        bool                containsAddr(ADDRESS uAddr);
                            //! Change BB containing this statement from a COMPCALL to a CALL.
        void                undoComputedBB(Statement* stmt) {
                                cfg->undoComputedBB(stmt);
                            }
virtual Exp *               getProven(Exp* left);
virtual Exp *               getPremised(Exp* left);
                            //! Set a location as a new premise, i.e. assume e=e
        void                setPremise(Exp* e) {e = e->clone(); recurPremises[e] = e;}
        void                killPremise(Exp* e) {recurPremises.erase(e);}
virtual bool                isPreserved(Exp* e);

virtual void                printCallGraphXML(std::ostream &os, int depth, bool recurse = true);
        void                printDecodedXML();
        void                printAnalysedXML();
        void                printSSAXML();
        void                printXML();
        void                printUseGraph();

        bool                searchAndReplace(Exp *search, Exp *replace);
        void                castConst(int num, Type* ty);
                            /// Add a location to the UseCollector; this means this location is used before defined,
                            /// and hence is an *initial* parameter.
                            /// \note final parameters don't use this information; it's only for handling recursion.
        void                useBeforeDefine(Exp* loc) {col.insert(loc);}
        void                processDecodedICTs();

private:
        ReturnStatement *   theReturnStatement;
        mutable int         DFGcount; //!< used in dotty output
public:
        ADDRESS             getTheReturnAddr() {
                                return theReturnStatement == nullptr ? NO_ADDRESS : theReturnStatement->getRetAddr();
                            }
        void                setTheReturnAddr(ReturnStatement* s, ADDRESS r) {
                                assert(theReturnStatement == nullptr);
                                theReturnStatement = s;
                                theReturnStatement->setRetAddr(r);
                            }
        ReturnStatement*    getTheReturnStatement() {return theReturnStatement;}
        bool                filterReturns(Exp* e);
        bool                filterParams(Exp* e);
        void                setImplicitRef(Statement* s, Exp* a, Type* ty);

        void verifyPHIs();
protected:
                            UserProc();
        void                setCFG(Cfg *c) { cfg = c; }
};        // class UserProc
Log& operator<< ( Log& out, const UserProc& c );

#endif
