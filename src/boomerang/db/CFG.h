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


#include "boomerang/db/exp/ExpHelp.h"
#include "boomerang/util/Address.h"

#include <list>
#include <vector>
#include <set>
#include <map>


class Function;
class Prog;
class UserProc;
class UseSet;
class LocationSet;
class SSACounts;
class BasicBlock;
class ICodeGenerator;
class CallStatement;
class BranchStatement;
class RTL;
class Global;
class Parameter;
class ConnectionGraph;
class Statement;

enum class BBType;

using RTLList   = std::list<RTL *>;

#define BTHEN    0
#define BELSE    1


/**
 * Control Flow Graph class. Contains all the BasicBlock objects for a procedure.
 * These BBs contain all the RTLs for the procedure, so by traversing the Cfg,
 * one traverses the whole procedure.
 */
class Cfg
{
    typedef std::map<SharedExp, Statement *, lessExpStar>          ExpStatementMap;

    // A type for the Address to BB map
    typedef std::map<Address, BasicBlock *, std::less<Address> >   BBStartMap;

public:
    typedef std::list<BasicBlock *>::iterator                      iterator;
    typedef std::list<BasicBlock *>::const_iterator                const_iterator;
    typedef std::list<BasicBlock *>::reverse_iterator              reverse_iterator;
    typedef std::list<BasicBlock *>::const_reverse_iterator        const_reverse_iterator;

public:
    /// Creates an empty CFG for the function \p proc
    Cfg(UserProc *proc);
    Cfg(const Cfg& other) = delete;
    Cfg(Cfg&& other) = default;

    ~Cfg();

    Cfg& operator=(const Cfg& other) = delete;
    Cfg& operator=(Cfg&& other) = default;

public:
    // The iterators get invalidated when the list of BBs is sorted or
    // when BBs are added or removed.
    iterator               begin()        { return m_listBB.begin(); }
    const_iterator         begin()  const { return m_listBB.begin(); }
    reverse_iterator       rbegin()       { return m_listBB.rbegin(); }
    const_reverse_iterator rbegin() const { return m_listBB.rbegin(); }

    iterator                end()         { return m_listBB.end(); }
    const_iterator          end()   const { return m_listBB.end(); }
    reverse_iterator        rend()        { return m_listBB.rend(); }
    const_reverse_iterator  rend()  const { return m_listBB.rend(); }

public:
    /// Remove all basic blocks from the CFG
    void clear();

    /// \returns the number of (complete and incomplete) BBs in this CFG.
    int getNumBBs() const { return m_listBB.size(); }

    /// Checks if the BB is part of this CFG
    bool hasBB(const BasicBlock *bb) const { return std::find(m_listBB.begin(), m_listBB.end(), bb) != m_listBB.end(); }

    /**
     * Create a new Basic Block for this CFG.
     * If the BB is blocked by a larger complete BB, the existing BB will be split at the first address of
     * \p bbRTLs; in this case this function returns nullptr (since no BB was created).
     * The case of the new BB being blocked by a smaller complete BB is not handled by this method;
     * use \ref Cfg::label instead.
     *
     * The new BB might also be blocked by exising incomplete BBs.
     * If this is the case, the new BB will be split at all blocking incomplete BBs,
     * and fallthrough edges will be added between parts of the split BB.
     * In this case, the incomplete BBs will be removed (since we just completed them).
     *
     * \param bbType Type of the new Basic Block
     * \param bbRTLs RTL list with semantics of all instructions contained in this BB.
     *
     * \returns the newly created BB, or the exisitng BB if the new BB is the same as
     * another exising complete BB.
     */
    BasicBlock *createBB(BBType bbType, std::unique_ptr<RTLList> bbRTLs);

    /**
     * Creates a new incomplete BB at address \p startAddr.
     * Creating an incomplete BB will cause the Cfg to not be well-fomed until all
     * incomplete BBs are completed by calling \ref createBB.
     */
    BasicBlock *createIncompleteBB(Address startAddr);

    /// Completely removes a single BB from this CFG.
    /// Also removes all in edges and out edges from \p bb (if found)
    void removeBB(BasicBlock *bb);

    /**
     * Get a BasicBlock starting at the given address.
     * If there is no such block, return nullptr.
     */
    inline BasicBlock *getBBStartingAt(Address addr)
    {
        BBStartMap::iterator it = m_bbStartMap.find(addr);
        return (it != m_bbStartMap.end()) ? (*it).second : nullptr;
    }

    inline const BasicBlock *getBBStartingAt(Address addr) const
    {
        BBStartMap::const_iterator it = m_bbStartMap.find(addr);
        return (it != m_bbStartMap.end()) ? (*it).second : nullptr;
    }

    /// \returns the entry BB of the procedure of this CFG
    BasicBlock *getEntryBB() { return m_entryBB; }
    const BasicBlock *getEntryBB() const { return m_entryBB; }
    BasicBlock *getExitBB() { return m_exitBB; }
    const BasicBlock *getExitBB() const { return m_exitBB; }

    /// Check if \p addr is the start of a basic block, complete or not
    bool isStartOfBB(Address addr) const;

    /// Check if the given address is the start of an incomplete basic block.
    bool isStartOfIncompleteBB(Address addr) const;

    /// Check if the BasicBlock is in this graph
    bool existsBB(const BasicBlock *bb) const { return std::find(m_listBB.begin(), m_listBB.end(), bb) != m_listBB.end(); }

    /**
     * Add an out edge from \p sourceBB to address \p destAddr.
     * If \p destAddr is not the start of a BB,
     * an incomplete BB will be created.
     * If \p destAddr is in the middle of a complete BB,
     * the destination BB will be split.
     *
     * \param sourceBB the source of the edge
     * \param destAddr the destination of a CTI (jump or call)
     */
    void addEdge(BasicBlock *sourceBB, Address destAddr);

    /**
     * Add an edge from \p sourceBB to \p destBB.
     * \param sourceBB the start of the edge.
     * \param destBB the destination of the edge.
     */
    void addEdge(BasicBlock *sourceBB, BasicBlock *destBB);

    /* Checks whether the given native address is a label (explicit or non explicit) or not.  Explicit labels are
     * addresses that have already been tagged as being labels due to transfers of control to that address.
     * Non explicit labels are those that belong to basic blocks that have already been constructed (i.e. have
     * previously been parsed) and now need to be made explicit labels.     In the case of non explicit labels, the
     * basic block is split into two and types and edges are adjusted accordingly. pNewBB is set to the lower part
     * of the split BB.
     * Returns true if the native address is that of an explicit or non explicit label, false otherwise. */

    /**
     * Checks whether the given native address is a label (explicit or non explicit) or not.
     * Returns false for incomplete BBs.
     * So it returns true iff the address has already been decoded in some BB. If it was not
     * already a label (i.e. the first instruction of some BB), the BB is split so that it becomes a label.
     * Explicit labels are addresses that have already been tagged as being labels due to transfers of control
     * to that address, and are therefore the start of some BB.
     * Non explicit labels are those that belong to basic blocks that have already been constructed
     * (i.e. have previously been parsed) and now need to be made explicit labels.
     * In the case of non explicit labels, the basic block is split into two and types and edges
     * are adjusted accordingly.
     * If \p pNewBB is the BB that gets split, it is changed to point to the
     * address of the new (lower) part of the split BB.
     * If there is an incomplete entry in the table for this address which overlaps with a completed address,
     * the completed BB is split and the BB for this address is completed.
     *
     * \param         addr   native (source) address to check
     * \param         pNewBB See above
     * \returns       True if \p addr is a label, i.e. (now) the start of a BB
     *                Note: \p pNewBB may be modified (as above)
     */
    bool label(Address addr, BasicBlock *& pNewBB);

    /**
     * Sorts the BBs in the CFG according to the low address of each BB.
     * Useful because it makes printouts easier, if they used iterators
     * to traverse the list of BBs.
     */
    void sortByAddress();

    /**
     * Checks that all BBs are complete, and all out edges are valid.
     * Also checks that the Cfg does not contain interprocedural edges.
     * By definition, the empty CFG is well-formed.
     */
    bool isWellFormed() const;

    /// Simplify all the expressions in the CFG
    void simplify();

    /// Change the BB enclosing stmt to be CALL, not COMPCALL
    void undoComputedBB(Statement *stmt);

    BasicBlock *findRetNode();

    /// Set the entry bb to \p entryBB and mark all return BBs as exit BBs.
    void setEntryAndExitBB(BasicBlock *entryBB);

    // Implicit assignments

    /// Find the existing implicit assign for x (if any)
    Statement *findTheImplicitAssign(const SharedExp& x);

    /// Find exiting implicit assign for parameter p
    Statement *findImplicitParamAssign(Parameter *p);

    /// Remove an existing implicit assignment for x
    void removeImplicitAssign(SharedExp x);

    /// Find or create an implicit assign for x
    Statement *findImplicitAssign(SharedExp x);

    bool implicitsDone() const { return m_implicitsDone; }    ///<  True if implicits have been created
    void setImplicitsDone() { m_implicitsDone = true; } ///< Call when implicits have been created

    void setBBStart(BasicBlock *bb, Address startAddr) { m_bbStartMap[startAddr] = bb; }

private:
    /**
     * Given two basic blocks that belong to a well-formed graph,
     * merges the second block onto the first one and returns the new block.
     * The in and out edges links are updated accordingly.
     * Note that two basic blocks can only be merged if each
     * has a unique out-edge and in-edge respectively,
     * and these edges correspond to each other.
     *
     * \returns true if the blocks were merged.
     */
    bool mergeBBs(BasicBlock *bb1, BasicBlock *bb2);

    /**
     * Amalgamate the RTLs for \p bb1 and  \p bb2, and place the result into \p bb2
     *
     * This is called where a two-way branch is deleted, thereby joining a two-way BB with it's successor.
     * This happens for example when transforming Intel floating point branches, and a branch on parity is deleted.
     * The joined BB becomes the type of the successor.
     *
     * \note Assumes that fallthrough of *pb1 is *pb2
     *
     * \param   bb1,bb2 pointers to the BBs to join
     * \returns True if successful
     */
    bool joinBB(BasicBlock *bb1, BasicBlock *bb2);

    /**
     * Split the given basic block at the RTL associated with \p splitAddr. The first node's type becomes
     * fall-through and ends at the RTL prior to that associated with \p splitAddr.
     * The second node's type becomes the type of the original basic block (\p bb),
     * and its out-edges are those of the original basic block.
     * In edges of the new BB's descendants are changed.
     *
     * \pre assumes \p splitAddr is an address within the boundaries of the given BB.
     *
     * \param   bb         pointer to the BB to be split
     * \param   splitAddr  address of RTL to become the start of the new BB
     * \param   newBB      if non zero, it remains as the "bottom" part of the BB, and splitBB only modifies the top part
     *                     to not overlap.
     * \param   deleteRTLs if true, deletes the RTLs removed from the existing BB after the split point. Only used if
     *                     there is an overlap with existing instructions
     * \returns A pointer to the "bottom" (new) part of the split BB.
     */
    BasicBlock *splitBB(BasicBlock *bb, Address splitAddr, BasicBlock *newBB = nullptr, bool deleteRTLs = false);

    /**
     * Complete the merge of two BBs by adjusting in and out edges.
     * No checks are made that the merge is valid (hence this is a private function).
     *
     * \param bb1,bb2 pointers to the two BBs to merge
     * \param deleteBB if true, \p bb1 is deleted as well
     */
    void completeMerge(BasicBlock *bb1, BasicBlock *bb2, bool deleteBB = false);

    void appendBBs(std::list<BasicBlock *>& worklist, std::set<BasicBlock *>& workset);

public:
    /// print this cfg, mainly for debugging
    void print(QTextStream& out, bool html = false);
    void printToLog();
    void dump();            ///< Dump to LOG_STREAM()
    void dumpImplicitMap(); ///< Dump the implicit map to LOG_STREAM()

private:
    UserProc *m_myProc;                      ///< Pointer to the UserProc object that contains this CFG object
    mutable bool m_wellFormed;
    bool m_implicitsDone;                    ///< True when the implicits are done; they can cause problems (e.g. with ad-hoc global assignment)

    std::list<BasicBlock *> m_listBB;        ///< BasicBlocks contained in this CFG

    BBStartMap m_bbStartMap;                 ///< The Address to BB map
    BasicBlock *m_entryBB;                   ///< The CFG entry BasicBlock.
    BasicBlock *m_exitBB;                    ///< The CFG exit BasicBlock.
    ExpStatementMap m_implicitMap;           ///< Map from expression to implicit assignment. The purpose is to prevent multiple implicit assignments for the same location.
};
