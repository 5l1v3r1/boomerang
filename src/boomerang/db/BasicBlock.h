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


#include "boomerang/util/Address.h"
#include "boomerang/util/StatementList.h"
#include "boomerang/util/LocationSet.h"

#include <QString>

#include <memory>


class Location;
class ICodeGenerator;
class BasicBlock;
class RTL;
class Function;
class UserProc;
class ConnectionGraph;
struct SwitchInfo;


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


using RTLList   = std::list<std::unique_ptr<class RTL>>;
using SharedExp = std::shared_ptr<class Exp>;


/**
 * BasicBlock class.
 */
class BasicBlock
{
public:
    typedef RTLList::iterator                     RTLIterator;
    typedef RTLList::reverse_iterator             RTLRIterator;

public:
    /**
     * Creates an incomplete BB.
     * \param function Enclosing function
     */
    BasicBlock(Address lowAddr, Function *function);

    /**
     * Creates a complete BB.
     * \param bbType   type of BasicBlock
     * \param rtls     rtl statements that will be contained in this BasicBlock
     * \param function Function this BasicBlock belongs to.
     */
    BasicBlock(BBType bbType, std::unique_ptr<RTLList> rtls, Function *function);

    BasicBlock(const BasicBlock& other);
    BasicBlock(BasicBlock&& other) = default;
    ~BasicBlock();

    BasicBlock& operator=(const BasicBlock& other);
    BasicBlock& operator=(BasicBlock&& other) = default;

public:
    /// \returns the type of the BasicBlock
    inline BBType getType()         const { return m_bbType; }
    inline bool isType(BBType type) const { return m_bbType == type; }
    inline void setType(BBType bbType)    { m_bbType = bbType; }

    /// \returns enclosing function, nullptr if the BB does not belong to a function.
    inline const Function *getFunction() const { return m_function; }
    inline Function *getFunction()             { return m_function; }

    /**
     * \returns the lowest real address associated with this BB.
     * \note although this is usually the address of the first RTL, it is not
     * always so. For example, if the BB contains just a delayed branch,and the delay
     * instruction for the branch does not affect the branch, so the delay instruction
     * is copied in front of the branch instruction. Its address will be
     * UpdateAddress()'ed to 0, since it is "not really there", so the low address
     * for this BB will be the address of the branch.
     * \sa updateBBAddresses
     */
    Address getLowAddr() const;

    /**
     * Get the highest address associated with this BB.
     * This is always the address associated with the last RTL.
     * \sa updateBBAddresses
     */
    Address getHiAddr() const;

    /// \returns true if the instructions of this BB have not been decoded yet.
    inline bool isIncomplete() const { return getHiAddr() == Address::INVALID; }

    // predecessor / successor functions

    inline int getNumPredecessors() const { return m_predecessors.size(); }
    inline int getNumSuccessors()   const { return m_successors.size(); }

    /// \returns all predecessors of this BB.
    const std::vector<BasicBlock *>& getPredecessors() const;

    /// \returns all successors of this BB.
    const std::vector<BasicBlock *>& getSuccessors() const;

    /// \returns the \p i-th predecessor of this BB.
    /// Returns nullptr if \p i is out of range.
    BasicBlock *getPredecessor(int i);
    const BasicBlock *getPredecessor(int i) const;

    /// \returns the \p i-th successor of this BB.
    /// Returns nullptr if \p i is out of range.
    BasicBlock *getSuccessor(int i);
    const BasicBlock *getSuccessor(int i) const;

    /// Change the \p i-th predecessor of this BB.
    /// \param i index (0-based)
    void setPredecessor(int i, BasicBlock *predecessor);

    /// Change the \p i-th successor of this BB.
    /// \param i index (0-based)
    void setSuccessor(int i, BasicBlock *successor);

    /// Add a predecessor to this BB.
    void addPredecessor(BasicBlock *predecessor);

    /// Add a successor to this BB.
    void addSuccessor(BasicBlock *successor);

    /// Remove a predecessor BB.
    void removePredecessor(BasicBlock *predecessor);

    /// Remove a successor BB
    void removeSuccessor(BasicBlock *successor);

    /// Removes all successor BBs.
    /// Called when noreturn call is found
    void removeAllSuccessors() { m_successors.clear(); }

    /// removes all predecessor BBs.
    void removeAllPredecessors() { m_predecessors.clear(); }

    /// \returns true if this BB is a (direct) predecessor of \p bb,
    /// i.e. there is an edge from this BB to \p bb
    bool isPredecessorOf(const BasicBlock *bb) const;

    /// \returns true if this BB is a (direct) successor of \p bb,
    /// i.e. there is an edge from \p bb to this BB.
    bool isSuccessorOf(const BasicBlock *bb) const;

    // RTL and statement related
public:
    /// \returns all RTLs that are part of this BB.
    RTLList *getRTLs();
    const RTLList *getRTLs() const;

    inline RTL *getLastRTL() { return m_listOfRTLs ? m_listOfRTLs->back().get() : nullptr; }
    inline const RTL *getLastRTL() const { return m_listOfRTLs ? m_listOfRTLs->back().get() : nullptr; }

    void removeRTL(RTL *rtl);

    /**
     * Update the RTL list of this basic block. Takes ownership of the pointer.
     * \param rtls a list of RTLs
     */
    void setRTLs(std::unique_ptr<RTLList> rtls);

    /**
     * Get first/next statement this BB
     * Somewhat intricate because of the post call semantics; these funcs save a lot of duplicated, easily-bugged
     * code
     */
    Statement *getFirstStmt(RTLIterator& rit, StatementList::iterator& sit);
    Statement *getNextStmt(RTLIterator& rit, StatementList::iterator& sit);
    Statement *getLastStmt(RTLRIterator& rit, StatementList::reverse_iterator& sit);
    Statement *getFirstStmt();
    Statement *getLastStmt();
    Statement *getPrevStmt(RTLRIterator& rit, StatementList::reverse_iterator& sit);

    /// Appends all statements in this BB to \p stmts.
    void appendStatementsTo(StatementList& stmts) const;

    /// Prepend an assignment (usually a PhiAssign or ImplicitAssign)
    /// \a proc is the enclosing Proc
    void prependStmt(Statement *s, UserProc *proc);

    bool hasStatement(const Statement *stmt) const;

public:
    /// \returns the destination procedure of the call if this is a call BB.
    /// Returns nullptr for all other BB types.
    Function *getCallDestProc() const;

    /*
     * Structuring and code generation.
     *
     * This code is whole heartly based on AST by Doug Simon.
     * Portions may be copyright to him and are available under a BSD style license.
     *
     * Adapted for Boomerang by Trent Waddington, 20 June 2002.
     */

    /**
     * Get the condition of a conditional branch.
     * If the BB does not have a conditional branch statement,
     * this function returns nullptr.
     */
    SharedExp getCond() const;

    /**
     * Set the condition of a conditional branch BB.
     * If the BB is not a branch, nothing happens.
     */
    void setCond(SharedExp e);

    /// Get the destination, if any
    SharedExp getDest() const;

    /// Simplify all expressions in this BB
    void simplify();

    /// Update the high and low address of this BB if the RTL list has changed.
    void updateBBAddresses();

public:
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

protected:
    /// The function this BB is part of, or nullptr if this BB is not part of a function.
    Function *m_function = nullptr;
    std::unique_ptr<RTLList>  m_listOfRTLs; ///< Ptr to list of RTLs

    Address m_lowAddr = Address::ZERO;
    Address m_highAddr = Address::INVALID;

    BBType m_bbType = BBType::Invalid;      ///< type of basic block

    /* in-edges and out-edges */
    std::vector<BasicBlock *> m_predecessors;  ///< Vector of in-edges
    std::vector<BasicBlock *> m_successors;    ///< Vector of out-edges
};
