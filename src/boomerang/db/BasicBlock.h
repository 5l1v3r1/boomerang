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


#include "boomerang/db/Managed.h"
#include "boomerang/util/Address.h"

#include <QtCore/QString>

class Location;
class ICodeGenerator;
class BasicBlock;
class RTL;
class Function;
class UserProc;
struct SwitchInfo;


/// Depth-first traversal constants.
enum class TravType
{
    Untraversed, ///< Initial value
    DFS_Tag,     ///< Remove redundant nodes pass
    DFS_LNum,    ///< DFS loop stamping pass
    DFS_RNum,    ///< DFS reverse loop stamping pass
    DFS_Case,    ///< DFS case head tagging traversal
    DFS_PDom,    ///< DFS post dominator ordering
    DFS_Codegen  ///< Code generating pass
};


/// an enumerated type for the class of stucture determined for a node
enum class StructType
{
    Invalid,
    Loop,     // Header of a loop only
    Cond,     // Header of a conditional only (if-then-else or switch)
    LoopCond, // Header of a loop and a conditional
    Seq       // sequential statement (default)
};


/// an type for the class of unstructured conditional jumps
enum class UnstructType
{
    Invalid,
    Structured,
    JumpInOutLoop,
    JumpIntoCase
};


/// an enumerated type for the type of conditional headers
enum class CondType
{
    Invalid,
    IfThen,     ///< conditional with only a then clause
    IfThenElse, ///< conditional with a then and an else clause
    IfElse,     ///< conditional with only an else clause
    Case        ///< nway conditional header (case statement)
};


/// an enumerated type for the type of loop headers
enum class LoopType
{
    Invalid,
    PreTested,  ///< Header of a while loop
    PostTested, ///< Header of a do..while loop
    Endless     ///< Header of an endless loop
};


/// Kinds of basic block nodes
/// reordering these will break the save files - trent
enum class BBType
{
    Invalid  = -1, ///< invalid instruction
    Fall     = 0,  ///< fall-through node
    Oneway   = 1,  ///< unconditional branch (jmp)
    Twoway   = 2,  ///< conditional branch   (jXX)
    Nway     = 3,  ///< case branch          (jmp [off + 4*eax])
    Call     = 4,  ///< procedure call       (call)
    Ret      = 5,  ///< return               (ret)
    CompJump = 6,  ///< computed jump
    CompCall = 7,  ///< computed call        (call [eax + 0x14])
};


enum class SBBType
{
    None,          ///< not structured
    PreTestLoop,   ///< header of a loop
    PostTestLoop,
    EndlessLoop,
    JumpInOutLoop, ///< an unstructured jump in or out of a loop
    JumpIntoCase,  ///< an unstructured jump into a case statement
    IfGoto,        ///< unstructured conditional
    IfThen,        ///< conditional with then clause
    IfThenElse,    ///< conditional with then and else clauses
    IfElse,        ///< conditional with else clause only
    Case           ///< case statement (switch)
};

typedef std::list<BasicBlock *>::iterator         BBIterator;
typedef std::list<BasicBlock *>::const_iterator   BBCIterator;

using RTLList   = std::list<RTL *>;

/**
 * BasicBlock class.
 */
class BasicBlock
{
    /// Objects of class Cfg can access the internals of a BasicBlock object.
    friend class Cfg;

public:
    typedef std::vector<BasicBlock *>::iterator   iEdgeIterator;
    typedef RTLList::iterator                     rtlit;
    typedef RTLList::reverse_iterator              rtlrit;
    typedef std::list<SharedExp>::iterator        elit;

public:
    /// \param parent The function this BB belongs to.
    BasicBlock(Function *parent);
    BasicBlock(const BasicBlock& bb);
    BasicBlock(BasicBlock&& bb) = default;
    ~BasicBlock();

    BasicBlock& operator=(const BasicBlock& bb);
    BasicBlock& operator=(BasicBlock&& bb) = default;

    /// \returns the type pf the BasicBlock
    inline BBType getType()         const { return m_nodeType; }
    inline bool isType(BBType type) const { return m_nodeType == type; }

    /// \returns enclosing function, null if none
    inline const Function *getFunction() const { return m_function; }
    inline Function *getParent()               { return m_function; }

    /**
     * Get the lowest real address associated with this BB.
     * \note although this is usually the address of the first RTL, it is not
     * always so. For example, if the BB contains just a delayed branch,and the delay
     * instruction for the branch does not affect the branch, so the delay instruction
     * is copied in front of the branch instruction. Its address will be
     * UpdateAddress()'ed to 0, since it is "not really there", so the low address
     * for this BB will be the address of the branch.
     *
     * \returns the lowest real address associated with this BB
     */
    Address getLowAddr() const;

    /// Get the highest address associated with this BB.
    /// This is always the address associated with the last RTL.
    Address getHiAddr() const;

    // predecessor / successor functions

    size_t getNumPredecessors() const { return m_predecessors.size(); }
    size_t getNumSuccessors()   const { return m_successors.size(); }

    /// \returns all predecessors of this BB.
    std::vector<BasicBlock *>& getPredecessors();

    /// \returns all successors of this BB.
    const std::vector<BasicBlock *>& getSuccessors();

    /// Removes all successor BBs.
    /// Called when noreturn call is found
    void removeAllSuccessors() { m_successors.clear(); }

    /// Change the \p i-th predecessor of this BB.
    /// \param i index (0-based)
    void setPredecessor(size_t i, BasicBlock *newIn);

    /// Change the \p i-th successor of this BB.
    /// \param i index (0-based)
    void setSuccessor(size_t i, BasicBlock *newOutEdge);

    /// \returns the \p i-th successor of this BB.
    /// Returns nullptr if \p i is out of range.
    BasicBlock *getSuccessor(size_t i);

    /// Add a predecessor to this BB.
    void addPredecessor(BasicBlock *predecessor);

    /// Add a successor to this BB.
    void addSuccessor(BasicBlock *successor);

    /// Remove a predecessor BB.
    void removePredecessor(BasicBlock *predecessor);

    /// Remove a successor BB
    void removeSuccessor(BasicBlock *successor);

    /// Checks if the \p i-th in edge is a back edge, i.e. an edge from a successor BB to this BB.
    /// Can happen e.g. for loops.
    bool isBackEdge(size_t i) const;

    bool isCaseOption();

    /// \returns true if this BB has been traversed.
    bool isTraversed();

    /// Sets the traversed flag
    void setTraversed(bool bTraversed);

    /**
     * Print the whole BB to the given stream
     * \param os   stream to output to
     * \param html print in html mode
     */
    void print(QTextStream& os, bool html = false);

    /// Output this BB to the log.
    void printToLog();

    /// Print to a static buffer (for debugging)
    const char *prints();

    /// Print this BB to stderr
    void dump();

    /**
     * Update the type and number of out edges.
     * Used for example where a COMPJUMP type is updated to an
     * NWAY when a switch idiom is discovered.
     * \param bbType - the new type
     */
    void updateType(BBType bbType);

    /**
     * Sets the "jump required" bit. This means that this BB is an orphan
     * (not generated from input code), and that the "fall through" out edge
     * (m_successors[BELSE]) needs to be implemented as a jump. The back end
     * needs to take heed of this bit
     */
    void setJumpRequired();


    /// \returns the "jump required" bit. See \ref setJumpRequired for details
    bool isJumpRequired();

    /// \returns all RTLs that are part of this BB.
    RTLList *getRTLs();
    const RTLList *getRTLs() const;

    RTL *getRTLWithStatement(Statement *stmt);

    /// \returns the address of the call, if this is a call BB.
    /// For all other BB types, returns Address::INVALID.
    Address getCallDest();

    /// \returns the destination procedure of the call if this is a call BB.
    /// Returns nullptr for all other BB types.
    Function *getCallDestProc();

    /// Get the destination proc
    Function *getDestProc();

    /**
     * Traverse this node and recurse on its children in a depth first manner.
     * Records the times at which this node was first visited and last visited
     *
     * \param first the number of nodes that have been visited
     * \param last  the number of nodes that have been visited for the last time during this traversal
     * \returns the number of nodes (including this one) that were traversed from this node
     */
    unsigned getDFTOrder(int& first, int& last);

    /**
     * Traverse this node and recurse on its parents in a reverse depth first manner.
     * Records the times at which this node was first visited and last visited
     *
     * \param first the number of nodes that have been visited
     * \param last  the number of nodes that have been visited for the last time during this traversal
     * \returns the number of nodes (including this one) that were traversed from this node
     */
    unsigned getRevDFTOrder(int& first, int& last);

    /**
     * Static comparison function that returns true if the first BB has an
     * address less than the second BB.
     *
     * \param bb1 first BB
     * \param bb2 last BB
     * \returns bb1.address < bb2.address
     */
    static bool lessAddress(BasicBlock *bb1, BasicBlock *bb2);

    /**
     * Static comparison function that returns true if the first BB has DFT
     * first order less than the second BB.
     * \returns bb1.first_DFS < bb2.first_DFS
     */
    static bool lessFirstDFT(BasicBlock *bb1, BasicBlock *bb2);

    /**
     * Static comparison function that returns true if the first BB has DFT
     * first order less than the second BB.
     * \returns bb1.last_DFS < bb2.last_DFS
     */
    static bool lessLastDFT(BasicBlock *bb1, BasicBlock *bb2);

    bool isOverlappedRegProcessingDone() const { return m_overlappedRegProcessingDone; }
    void setOverlappedRegProcessingDone() { m_overlappedRegProcessingDone = true; }


    class LastStatementNotABranchError : public std::exception
    {
    public:
        Statement *stmt;
        LastStatementNotABranchError(Statement *_stmt)
            : stmt(_stmt) {}
    };

    /*
     * Structuring and code generation.
     *
     * This code is whole heartly based on AST by Doug Simon.
     * Portions may be copyright to him and are available under a BSD style license.
     *
     * Adapted for Boomerang by Trent Waddington, 20 June 2002.
     */
    /// Get the condition
    SharedExp getCond() noexcept (false);

    /// set the condition
    void setCond(SharedExp e) noexcept (false);

    /// Get the destination, if any
    SharedExp getDest();

    /// Get the loop body
    BasicBlock *getLoopBody();

    /// Simplify all expressions in this BB
    void simplify();

    /**
     * Get first/next statement this BB
     * Somewhat intricate because of the post call semantics; these funcs save a lot of duplicated, easily-bugged
     * code
     */
    Statement *getFirstStmt(rtlit& rit, StatementList::iterator& sit);
    Statement *getNextStmt(rtlit& rit, StatementList::iterator& sit);
    Statement *getLastStmt(rtlrit& rit, StatementList::reverse_iterator& sit);
    Statement *getFirstStmt();
    Statement *getLastStmt();
    Statement *getPrevStmt(rtlrit& rit, StatementList::reverse_iterator& sit);

    RTL *getLastRTL() { return m_listOfRTLs->back(); }
    void getStatements(StatementList& stmts) const;

public:
    /// Prepend an assignment (usually a PhiAssign or ImplicitAssign)
    /// \a proc is the enclosing Proc
    void prependStmt(Statement *s, UserProc *proc);

    // Liveness
    bool calcLiveness(ConnectionGraph& ig, UserProc *proc);

    /// Locations that are live at the end of this BB are the union of the locations that are live at the start of its
    /// successors
    /// liveout gets all the livenesses, and phiLocs gets a subset of these, which are due to phi statements at the top of
    /// successors
    void getLiveOut(LocationSet& live, LocationSet& phiLocs);

    /// Find indirect jumps and calls
    /// Find any BBs of type COMPJUMP or COMPCALL. If found, analyse, and if possible decode extra code and return true
    bool decodeIndirectJmp(UserProc *proc);

    /**
     * Called when a switch has been identified. Visits the destinations of the switch,
     * adds out edges to the BB, etc.
     * \note    Used to be called as soon as a switch statement is discovered, but this causes decoded but unanalysed
     *          BBs (statements not numbered, locations not SSA renamed etc) to appear in the CFG. This caused problems
     *          when there were nested switch statements. Now only called when re-decoding a switch statement
     * \param   proc - Pointer to the UserProc object for this code
     */
    void processSwitch(UserProc *proc);

    /**
     * Find the number of cases for this switch statement. Assumes that there is a compare and branch around the indirect
     * branch. Note: fails test/sparc/switchAnd_cc because of the and instruction, and the compare that is outside is not
     * the compare for the upper bound. Note that you CAN have an and and still a test for an upper bound. So this needs
     * tightening.
     * TMN: It also needs to check for and handle the double indirect case; where there is one array (of e.g. ubyte)
     * that is indexed by the actual switch value, then the value from that array is used as an index into the array of
     * code pointers.
     */
    int findNumCases();

    /// Change the BB enclosing stmt to be CALL, not COMPCALL
    bool undoComputedBB(Statement *stmt);

    /**
     * Searches for all instances of "search" and adds them to "result"
     * in reverse nesting order. The search is optionally type sensitive.
     * \note out of date doc, unless type senistivity is a part of \a search_for ?
     *
     * \param search_for a location to search for
     * \param results    a list which will have any matching exprs appended to it
     * \returns true if there were any matches
     */
    bool searchAll(const Exp& search_for, std::list<SharedExp>& results);

    /**
     * Replace all instances of search with replace.
     * Can be type sensitive if required.
     * \param search - ptr to an expression to search for
     * \param replace the expression with which to replace it
     * \returns true if replacement took place
     */
    bool searchAndReplace(const Exp& search, SharedExp replace);

    bool isLatchNode() { return m_loopHead && m_loopHead->m_latchNode == this; }
    BasicBlock *getLatchNode()  const { return m_latchNode; }
    BasicBlock *getLoopHead()   const { return m_loopHead; }
    BasicBlock *getLoopFollow() const { return m_loopFollow; }
    BasicBlock *getCondFollow() const { return m_condFollow; }
    BasicBlock *getCaseHead()   const { return m_caseHead; }

    TravType getTravType() const { return m_traversed; }
    StructType getStructType() const { return m_structuringType; }
    CondType getCondType() const;
    UnstructType getUnstructType() const;
    LoopType getLoopType() const;

    void setTravType(TravType type) { m_traversed = type; }
    void setStructType(StructType s);

    int getOrdering() const { return m_ord; }

    /// Return true if every parent (i.e. forward in edge source) of this node has
    /// had its code generated
    bool allParentsGenerated();

protected:
    void setLoopStamps(int& time, std::vector<BasicBlock *>& order);
    void setRevLoopStamps(int& time);
    void setRevOrder(std::vector<BasicBlock *>& order);

    void setLoopHead(BasicBlock *head) { m_loopHead = head; }
    void setLatchNode(BasicBlock *latch) { m_latchNode = latch; }
    void setCaseHead(BasicBlock *head, BasicBlock *follow);

    void setUnstructType(UnstructType us);
    void setLoopType(LoopType l);
    void setCondType(CondType l);

    void setLoopFollow(BasicBlock *other) { m_loopFollow = other; }
    void setCondFollow(BasicBlock *other) { m_condFollow = other; }

    /// establish if this bb has a back edge to the given destination
    bool hasBackEdgeTo(BasicBlock *dest);

    /// establish if this bb has any back edges leading FROM it
    bool hasBackEdge()
    {
        for (auto bb : m_successors) {
            if (hasBackEdgeTo(bb)) {
                return true;
            }
        }

        return false;
    }

    /// establish if this bb is an ancestor of another BB
    bool isAncestorOf(BasicBlock *other);
    bool inLoop(BasicBlock *header, BasicBlock *latch);

    char *indent(int indLevel, int extra = 0);

    void addRTL(RTL *rtl)
    {
        if (m_listOfRTLs == nullptr) {
            m_listOfRTLs.reset(new RTLList);
        }

        m_listOfRTLs->push_back(rtl);
    }

    void addLiveIn(SharedExp e) { m_liveIn.insert(e); }

private:
    /**
     * \param function Function this BasicBlock belongs to.
     * \param rtls     rtl statements that will be contained in this BasicBlock
     * \param bbType type of BasicBlock
     */
    BasicBlock(Function *function, std::unique_ptr<RTLList> rtls, BBType bbType);

    /**
     * Update the RTL list of this basic block. Takes ownership of the pointer.
     * \param rtls a list of RTLs
     */
    void setRTLs(std::unique_ptr<RTLList> rtls);

protected:
    /// The function this BB is part of, or nullptr if this BB is not part of a function.
    Function *m_function;

    BBType m_nodeType = BBType::Invalid;      ///< type of basic block
    std::unique_ptr<RTLList>  m_listOfRTLs = nullptr; ///< Ptr to list of RTLs

    /* general basic block information */
    int m_labelNum      = 0;                  ///< Nonzero if start of BB needs label
    bool m_labelNeeded  = false;
    bool m_incomplete   = true;               ///< True if not yet complete
    bool m_jumpRequired = false;              ///< True if jump required for "fall through"

    /* in-edges and out-edges */
    std::vector<BasicBlock *> m_predecessors;  ///< Vector of in-edges
    std::vector<BasicBlock *> m_successors;    ///< Vector of out-edges

    /* for traversal */
    bool m_traversedMarker = false; ///< traversal marker

    /* Liveness */
    LocationSet m_liveIn;                  ///< Set of locations live at BB start

    /*
     * Depth first traversal of all bbs, numbering as we go and as we come back, forward and reverse passes.
     * Use Cfg::establishDFTOrder() and CFG::establishRevDFTOrder to create these values.
     */
    int m_DFTfirst = 0; ///< depth-first traversal first visit
    int m_DFTlast  = 0; ///< depth-first traversal last visit
    int m_DFTrevfirst;  ///< reverse depth-first traversal first visit
    int m_DFTrevlast;   ///< reverse depth-first traversal last visit

    /// Control flow analysis stuff, lifted from Doug Simon's honours thesis.
    int m_ord;                               ///< node's position within the ordering structure
    int m_revOrd;                            ///< position within ordering structure for the reverse graph
    int m_inEdgesVisited;                    ///< counts the number of in edges visited during a DFS
    int m_numForwardInEdges;                 ///< inedges to this node that aren't back edges
    int m_loopStamps[2], m_revLoopStamps[2]; ///< used for structuring analysis
    TravType m_traversed;                    ///< traversal flag for the numerous DFS's
    QString m_labelStr;                      ///< the high level label for this node (if needed)

    /* high level structuring */
    SBBType m_loopCondType = SBBType::None; ///< type of conditional to treat this loop header as (if any)
    SBBType m_structType   = SBBType::None; ///< structured type of this node

    // analysis information
    BasicBlock *m_immPDom;         ///< immediate post dominator
    BasicBlock *m_loopHead;        ///< head of the most nested enclosing loop
    BasicBlock *m_caseHead;        ///< head of the most nested enclosing case
    BasicBlock *m_condFollow;      ///< follow of a conditional header
    BasicBlock *m_loopFollow;      ///< follow of a loop header
    BasicBlock *m_latchNode;       ///< latching node of a loop header

    // Structured type of the node
    StructType m_structuringType;    ///< the structuring class (Loop, Cond, etc)
    UnstructType m_unstructuredType; ///< the restructured type of a conditional header
    LoopType m_loopHeaderType;       ///< the loop type of a loop header
    CondType m_conditionHeaderType;  ///< the conditional type of a conditional header

    /// true if processing for overlapped registers on statements in this BB
    /// has been completed.
    bool m_overlappedRegProcessingDone = false;
};
