/*
 * Copyright (C) 1997-2000, The University of Queensland
 * Copyright (C) 2000-2001, Sun Microsystems, Inc
 * Copyright (C) 2002, Trent Waddington
 *
 * See the file "LICENSE.TERMS" for information on usage and
 * redistribution of this file, and for a DISCLAIMER OF ALL
 * WARRANTIES.
 *
 */

/***************************************************************************//**
 * \file    cfg.cpp
 * \brief   Implementation of the CFG class.
 ******************************************************************************/

/*
 * $Revision$    // 1.95.2.5
 * 18 Apr 02 - Mike: Mods for boomerang
 * 19 Jul 04 - Mike: Changed initialisation of BBs to not rely on out edges
 * 20 Mar 11 - Mike: Added missing braces in Cfg::findLoopFollow()
 */

/***************************************************************************//**
 * \class Cfg
 * Control Flow Graph class. Contains all the BasicBlock objects for a procedure.
 * These BBs contain all the RTLs for the procedure, so by traversing the Cfg,
 * one traverses the whole procedure.
 * \var Cfg::myProc
 * Pointer to the UserProc object that contains this CFG object
 * \var Cfg::m_listBB
 * BasicBlock s contained in this CFG
 * \var Cfg::Ordering
 * Ordering of BBs for control flow structuring
 * \var Cfg::revOrdering
 * Ordering of BBs for control flow structuring
 * \var Cfg::m_mapBB
 * The ADDRESS to PBB map.
 * \var Cfg::entryBB
 * The CFG entry BasicBlock.
 * \var Cfg::exitBB
 * The CFG exit BasicBlock.
 * \var Cfg::m_bWellFormed
 * \var Cfg::structured
 * \var Cfg::callSites
 * Set of the call instructions in this procedure.
 * \var Cfg::lastLabel
 * Last label (positive integer) used by any BB this Cfg
 * \var Cfg::implicitMap
 * Map from expression to implicit assignment. The purpose is to prevent multiple implicit assignments
 * for the same location.
 * \var Cfg::bImplicitsDone
 * True when the implicits are done; they can cause problems (e.g. with ad-hoc global assignment)
 * \var Cfg::m_vectorBB
 * faster access
 ******************************************************************************/

/***************************************************************************//**
 * Dependencies.
 ******************************************************************************/

#include <cassert>
#if defined(_MSC_VER) && _MSC_VER <= 1200
#pragma warning(disable:4786)
#endif

#include <algorithm>        // For find()
#include <fstream>
#include <sstream>
#include <cstring>
#include "types.h"
#include "statement.h"
#include "signature.h"
#include "exp.h"
#include "cfg.h"
#include "register.h"
#include "rtl.h"
#include "proc.h"            // For Proc::setTailCaller()
#include "prog.h"            // For findProc()
#include "util.h"
#include "hllcode.h"
#include "boomerang.h"
#include "log.h"

void delete_lrtls(std::list<RTL *> &pLrtl);
void erase_lrtls(std::list<RTL *> &pLrtl, std::list<RTL*>::iterator begin,
                 std::list<RTL*>::iterator end);

/**********************************
 * Cfg methods.
 **********************************/

Cfg::Cfg()
    : m_bWellFormed(false), structured(false), bImplicitsDone(false), lastLabel(0), entryBB(NULL), exitBB(NULL)
{}

/***************************************************************************//**
 *
 * \brief        Destructor. Note: destructs the component BBs as well
 *
 ******************************************************************************/
Cfg::~Cfg() {
    // Delete the BBs
    for (BasicBlock * it : m_listBB)
        delete it;
}
/***************************************************************************//**
 *
 * \brief   Set the pointer to the owning UserProc object
 * \param   proc - pointer to the owning UserProc object
 *
 ******************************************************************************/
void Cfg::setProc(UserProc* proc) {
    myProc = proc;
}

/***************************************************************************//**
 *
 * \brief        Clear the CFG of all basic blocks, ready for decode
 *
 ******************************************************************************/
void Cfg::clear() {
    // Don't delete the BBs; this will delete any CaseStatements we want to save for the re-decode. Just let the garbage
    // collection take care of it.
    // for (std::list<PBB>::iterator it = m_listBB.begin(); it != m_listBB.end(); it++)
    //    delete *it;
    m_listBB.clear();
    m_mapBB.clear();
    implicitMap.clear();
    entryBB = NULL;
    exitBB = NULL;
    m_bWellFormed = false;
    callSites.clear();
    lastLabel = 0;
}

/***************************************************************************//**
 *
 * \brief assignment operator for Cfg's, the BB's are shallow copied
 * \param other - rhs
 * \returns            <nothing>
 ******************************************************************************/
const Cfg& Cfg::operator=(const Cfg& other) {
    m_listBB = other.m_listBB;
    m_mapBB  = other.m_mapBB;
    m_bWellFormed = other.m_bWellFormed;
    return *this;
}

/***************************************************************************//**
 *
 * \brief        Set the entry and calculate exit BB pointers
 * \note        Each cfg should have only one exit node now
 * \param        bb: pointer to the entry BB
 ******************************************************************************/
void Cfg::setEntryBB(BasicBlock *bb) {
    entryBB = bb;
    for (BasicBlock * it : m_listBB) {
        if (it->getType() == RET) {
            exitBB = it;
            return;
        }
    }
    // It is possible that there is no exit BB
}

void Cfg::setExitBB(PBB bb) {
    exitBB = bb;
}

/***************************************************************************//**
 *
 *
 * \brief        Check the entry BB pointer; if zero, emit error message
 *                      and return true
 * \returns            true if was null
 ******************************************************************************/
bool Cfg::checkEntryBB() {
    if (entryBB != nullptr)
        return false;
    std::cerr << "No entry BB for ";
    if (myProc)
        std::cerr << myProc->getName() << std::endl;
    else
        std::cerr << "unknown proc\n";
    return true;
}

/***************************************************************************//**
 *
 * \brief        Add a new basic block to this cfg
 *
 * Checks to see if the address associated with pRtls is already in the map as an incomplete BB; if so, it is
 * completed now and a pointer to that BB is returned. Otherwise, allocates memory for a new basic block node,
 * initializes its list of RTLs with pRtls, its type to the given type, and allocates enough space to hold
 * pointers to the out-edges (based on given numOutEdges).
 * The native address associated with the start of the BB is taken from pRtls, and added to the map (unless 0).
 * \note You cannot assume that the returned BB will have the RTL associated with pStart as its first RTL, since
 * the BB could be split. You can however assume that the returned BB is suitable for adding out edges (i.e. if
 * the BB is split, you get the "bottom" part of the BB, not the "top" (with lower addresses at the "top").
 * Returns NULL if not successful, or if there already exists a completed BB at this address (this can happen
 * with certain kinds of forward branches).
 *
 * \param   pRtls list of pointers to RTLs to initialise the BB with bbType: the type of the BB (e.g. TWOWAY)
 * \param   iNumOutEdges number of out edges this BB will eventually have
 * \returns Pointer to the newly created BB, or 0 if there is already an incomplete BB with the same address
 ******************************************************************************/
PBB Cfg::newBB(std::list<RTL*>* pRtls, BBTYPE bbType, int iNumOutEdges) throw(BBAlreadyExistsError) {
    MAPBB::iterator mi;
    BasicBlock * pBB;

    // First find the native address of the first RTL
    // Can't use BasicBlock::GetLowAddr(), since we don't yet have a BB!
    ADDRESS addr = pRtls->front()->getAddress();

    // If this is zero, try the next RTL (only). This may be necessary if e.g. there is a BB with a delayed branch only,
    // with its delay instruction moved in front of it (with 0 address).
    // Note: it is possible to see two RTLs with zero address with Sparc: jmpl %o0, %o1. There will be one for the delay
    // instr (if not a NOP), and one for the side effect of copying %o7 to %o1.
    // Note that orphaned BBs (for which we must compute addr here to to be 0) must not be added to the map, but they
    // have no RTLs with a non zero address.
    if ( addr.isZero() && (pRtls->size() > 1)) {
        std::list<RTL*>::iterator next = pRtls->begin();
        addr = (*++next)->getAddress();
    }

    // If this addr is non zero, check the map to see if we have a (possibly incomplete) BB here already
    // If it is zero, this is a special BB for handling delayed branches or the like
    bool bDone = false;
    if ( !addr.isZero() ) {
        mi = m_mapBB.find(addr);
        if (mi != m_mapBB.end() && (*mi).second) {
            pBB = (*mi).second;
            // It should be incomplete, or the pBB there should be zero (we have called Label but not yet created the BB
            // for it).  Else we have duplicated BBs. Note: this can happen with forward jumps into the middle of a
            // loop, so not error
            if (!pBB->m_bIncomplete) {
                // This list of RTLs is not needed now
                delete_lrtls(*pRtls);
                if (VERBOSE)
                    LOG << "throwing BBAlreadyExistsError\n";
                throw BBAlreadyExistsError(pBB);
            }
            else {
                // Fill in the details, and return it
                pBB->setRTLs(pRtls);
                pBB->m_nodeType = bbType;
                pBB->m_iNumOutEdges = iNumOutEdges;
                pBB->m_bIncomplete = false;
            }
            bDone = true;
        }
    }
    if (!bDone) {
        // Else add a new BB to the back of the current list.
        pBB = new BasicBlock(pRtls, bbType, iNumOutEdges);
        m_listBB.push_back(pBB);

        // Also add the address to the map from native (source) address to
        // pointer to BB, unless it's zero
        if ( !addr.isZero() ) {
            m_mapBB[addr] = pBB;            // Insert the mapping
            mi = m_mapBB.find(addr);
        }
    }

    if ( !addr.isZero() && (mi != m_mapBB.end())) {
        // Existing New            +---+ Top of new
        //            +---+        +---+
        //    +---+   |   |        +---+ Fall through
        //    |   |   |   | =>     |   |
        //    |   |   |   |        |   | Existing; rest of new discarded
        //    +---+   +---+        +---+
        //
        // Check for overlap of the just added BB with the next BB (address wise).  If there is an overlap, truncate the
        // std::list<Exp*> for the new BB to not overlap, and make this a fall through BB.
        // We still want to do this even if the new BB overlaps with an incomplete BB, though in this case,
        // splitBB needs to fill in the details for the "bottom" BB of the split.
        // Also, in this case, we return a pointer to the newly completed BB, so it will get out edges added
        // (if required). In the other case (i.e. we overlap with an existing, completed BB), we want to return 0, since
        // the out edges are already created.
        if (++mi != m_mapBB.end()) {
            PBB pNextBB = (*mi).second;
            ADDRESS uNext = (*mi).first;
            bool bIncomplete = pNextBB->m_bIncomplete;
            if (uNext <= pRtls->back()->getAddress()) {
                // Need to truncate the current BB. We use splitBB(), but pass it pNextBB so it doesn't create a new BB
                // for the "bottom" BB of the split pair
                splitBB(pBB, uNext, pNextBB);
                // If the overlapped BB was incomplete, return the "bottom" part of the BB, so adding out edges will
                // work properly.
                if (bIncomplete) {
                    return pNextBB;
                }
                // However, if the overlapping BB was already complete, return 0, so out edges won't be added twice
                throw BBAlreadyExistsError(pNextBB);
            }
        }

        // Existing New            +---+ Top of existing
        //    +---+                +---+
        //    |    |    +---+        +---+ Fall through
        //    |    |    |    | =>    |    |
        //    |    |    |    |        |    | New; rest of existing discarded
        //    +---+    +---+        +---+
        // Note: no need to check the other way around, because in this case, we will have called Cfg::Label(), and it
        // will have split the existing BB already.
    }
    assert(pBB);
    return pBB;
}

// Use this function when there are outedges to BBs that are not created yet. Usually used via addOutEdge()
/***************************************************************************//**
 *
 * \brief Allocates space for a new, incomplete BB, and the given address is
 * added to the map. This BB will have to be completed before calling WellFormCfg.
 * This function will commonly be called via AddOutEdge()
 * \returns           pointer to allocated BasicBlock
 ******************************************************************************/
BasicBlock * Cfg::newIncompleteBB(ADDRESS addr) {
    // Create a new (basically empty) BB
    PBB pBB = new BasicBlock();
    // Add it to the list
    m_listBB.push_back(pBB);
    m_mapBB[addr] = pBB;                // Insert the mapping
    return pBB;
}

/***************************************************************************//**
 *
 * \brief Add an out edge to this BB (and the in-edge to the dest BB)
 *                  May also set a label
 *
 * Adds an out-edge to the basic block pBB by filling in the first slot that is empty.
 * \note  a pointer to a BB is given here.
 *
 * \note    Overloaded with address as 2nd argument (calls this proc in the end)
 * \note    Does not increment m_iNumOutEdges; this is supposed to be constant for a BB.
 *                      (But see BasicBlock::addNewOutEdge())
 * \param   pBB source BB (to have the out edge added to)
 * \param   pDestBB destination BB (to have the out edge point to)
 * \returns            <nothing>
 ******************************************************************************/
void Cfg::addOutEdge(BasicBlock * pBB, BasicBlock * pDestBB, bool bSetLabel /* = false */) {
    // Add the given BB pointer to the list of out edges
    pBB->m_OutEdges.push_back(pDestBB);
    // Note that the number of out edges is set at constructor time, not incremented here.
    // Add the in edge to the destination BB
    pDestBB->m_InEdges.push_back(pBB);
    pDestBB->m_iNumInEdges++;            // Inc the count
    if (bSetLabel)
        setLabel(pDestBB);    // Indicate "label required"
}

/***************************************************************************//**
 *
 * \brief        Add an out edge to this BB (and the in-edge to the dest BB)
 * May also set a label
 *
 * Adds an out-edge to the basic block pBB by filling in the first slot that is empty.    Note: an address is
 * given here; the out edge will be filled in as a pointer to a BB. An incomplete BB will be created if
 * required. If bSetLabel is true, the destination BB will have its "label required" bit set.
 *
 * \note            Calls the above
 * \param pBB: source BB (to have the out edge added to)
 * \param addr: source address of destination
 * (the out edge is to point to the BB whose lowest address is addr)
 * \param bSetLabel: if true, set a label at the destination address.  Set true on "true" branches of labels
 *
 ******************************************************************************/
void Cfg::addOutEdge(PBB pBB, ADDRESS addr, bool bSetLabel /* = false */) {
    // Check to see if the address is in the map, i.e. we already have a BB for this address
    MAPBB::iterator it = m_mapBB.find(addr);
    PBB pDestBB;
    if (it != m_mapBB.end() && (*it).second) {
        // Just add this PBB to the list of out edges
        pDestBB = (*it).second;
    }
    else {
        // Else, create a new incomplete BB, add that to the map, and add the new BB as the out edge
        pDestBB = newIncompleteBB(addr);
    }
    addOutEdge(pBB, pDestBB, bSetLabel);
}

/***************************************************************************//**
 *
 * \brief Return true if the given address is the start of a basic block, complete or not
 *
 * Just checks to see if there exists a BB starting with this native address. If not, the address is NOT added
 * to the map of labels to BBs.
 * \note must ignore entries with a null pBB, since these are caused by
 * calls to Label that failed, i.e. the instruction is not decoded yet.
 *
 * \param        uNativeAddr: native address to look up
 * \returns      True if uNativeAddr starts a BB
 ******************************************************************************/
bool Cfg::existsBB (ADDRESS uNativeAddr) {
    MAPBB::iterator mi;
    mi = m_mapBB.find (uNativeAddr);
    return (mi != m_mapBB.end() && (*mi).second);
}
/***************************************************************************//**
 *
 * Split the given basic block at the RTL associated with uNativeAddr. The first node's type becomes
 * fall-through and ends at the RTL prior to that associated with uNativeAddr.  The second node's type becomes
 * the type of the original basic block (pBB), and its out-edges are those of the original basic block.
 * In edges of the new BB's descendants are changed.
 * PRECONDITION: assumes uNativeAddr is an address within the boundaries of the given basic block.
 * \param   pBB -  pointer to the BB to be split
 * \param   uNativeAddr - address of RTL to become the start of the new BB
 * \param   pNewBB -  if non zero, it remains as the "bottom" part of the BB, and splitBB only modifies the top part
 * to not overlap.
 * \param   bDelRtls - if true, deletes the RTLs removed from the existing BB after the split point. Only used if
 *                there is an overlap with existing instructions
 * \returns Returns a pointer to the "bottom" (new) part of the split BB.
 ******************************************************************************/
PBB Cfg::splitBB (PBB pBB, ADDRESS uNativeAddr, PBB pNewBB /* = 0 */, bool bDelRtls /* = false */) {
    std::list<RTL*>::iterator ri;

    // First find which RTL has the split address; note that this could fail (e.g. label in the middle of an
    // instruction, or some weird delay slot effects)
    for (ri = pBB->m_pRtls->begin(); ri != pBB->m_pRtls->end(); ri++) {
        if ((*ri)->getAddress() == uNativeAddr)
            break;
    }
    if (ri == pBB->m_pRtls->end()) {
        std::cerr << "could not split BB at " << std::hex << pBB->getLowAddr() << " at split address " << uNativeAddr
                  << std::endl;
        return pBB;
    }

    // If necessary, set up a new basic block with information from the original bb
    if (pNewBB == NULL) {
        pNewBB = new BasicBlock(*pBB);
        // But we don't want the top BB's in edges; our only in-edge should be the out edge from the top BB
        pNewBB->m_iNumInEdges = 0;
        pNewBB->m_InEdges.erase(pNewBB->m_InEdges.begin(), pNewBB->m_InEdges.end());
                                            // The "bottom" BB now starts at the implicit label, so we create a new list
                                            // that starts at ri. We need a new list, since it is different from the
                                            // original BB's list. We don't have to "deep copy" the RTLs themselves,
                                            // since they will never overlap
        pNewBB->setRTLs(new std::list<RTL*>(ri, pBB->m_pRtls->end()));
        m_listBB.push_back(pNewBB);         // Put it in the graph
        m_mapBB[uNativeAddr] = pNewBB;      // Put the implicit label into the map. Need to do this before the addOutEdge() below
        pNewBB->m_iLabelNum = ++lastLabel;  // There must be a label here; else would not be splitting. Give it a new label
    }
    else if (pNewBB->m_bIncomplete) {
                                            // We have an existing BB and a map entry, but no details except for
                                            // in-edges and m_bHasLabel.
                                            // First save the in-edges and m_iLabelNum
        std::vector<PBB> ins(pNewBB->m_InEdges);
        int label = pNewBB->m_iLabelNum;
                                            // Copy over the details now, completing the bottom BB
        *pNewBB = *pBB;                     // Assign the BB, copying fields. This will set m_bIncomplete false
                                            // Replace the in edges (likely only one)
        pNewBB->m_InEdges = ins;
        pNewBB->m_iNumInEdges = ins.size();
        pNewBB->m_iLabelNum = label;        // Replace the label (must be one, since we are splitting this BB!)
                                            // The "bottom" BB now starts at the implicit label
                                            // We need to create a new list of RTLs, as per above
        pNewBB->setRTLs(new std::list<RTL*>(ri, pBB->m_pRtls->end()));
    }
                                            // else pNewBB exists and is complete. We don't want to change the complete
                                            // BB in any way, except to later add one in-edge
    pBB->m_nodeType = FALL;                 // Update original ("top") basic block's info and make it a fall-through
                                            // Fix the in-edges of pBB's descendants. They are now pNewBB
                                            // Note: you can't believe m_iNumOutEdges at the time that this function may
                                            // get called
    for ( PBB pDescendant : pBB->m_OutEdges ) {
                                            // Search through the in edges for pBB (old ancestor)
        unsigned k;
        for (k=0; k < pDescendant->m_InEdges.size(); k++) {
            if (pDescendant->m_InEdges[k] == pBB) {
                // Replace with a pointer to the new ancestor
                pDescendant->m_InEdges[k] = pNewBB;
                break;
            }
        }
        // That pointer should have been found!
        assert (k < pDescendant->m_InEdges.size());
    }
                                            // The old BB needs to have part of its list of RTLs erased, since the
                                            // instructions overlap
    if (bDelRtls) {
                                            // Delete the list of pointers, and also the RTLs they point to
        erase_lrtls(*pBB->m_pRtls, ri, pBB->m_pRtls->end());
    }
    else {
                                            // Delete the list of pointers, but not the RTLs they point to
        pBB->m_pRtls->erase(ri, pBB->m_pRtls->end());
    }
                                            // Erase any existing out edges
    pBB->m_OutEdges.erase(pBB->m_OutEdges.begin(), pBB->m_OutEdges.end());
    pBB->m_iNumOutEdges = 1;
    addOutEdge (pBB, uNativeAddr);
    return pNewBB;
}

/***************************************************************************//**
 *
 * \brief Get the first BB of this cfg
 *
 * Gets a pointer to the first BB this cfg. Also initialises `it' so that calling GetNextBB will return the
 * second BB, etc.  Also, *it is the first BB.  Returns 0 if there are no BBs this CFG.
 *
 * \param       it set to an value that must be passed to getNextBB
 * \returns     Pointer to the first BB this cfg, or NULL if none
 ******************************************************************************/
PBB Cfg::getFirstBB(BB_IT& it) {
    it = m_listBB.begin();
    if (it == m_listBB.end())
        return 0;
    return *it;
}

/***************************************************************************//**
 *
 * \brief Get the next BB this cfg. Basically increments the given iterator and returns it
 *
 * Gets a pointer to the next BB this cfg. `it' must be from a call to GetFirstBB(), or from a subsequent call
 * to GetNextBB().  Also, *it is the current BB.  Returns 0 if there are no more BBs this CFG.
 *
 * \param   it - iterator from a call to getFirstBB or getNextBB
 * \returns pointer to the BB, or NULL if no more
 ******************************************************************************/
BasicBlock * Cfg::getNextBB(BB_IT& it) {
    if (++it == m_listBB.end())
        return 0;
    return *it;
}
/*
 * Checks whether the given native address is a label (explicit or non explicit) or not.  Explicit labels are
 * addresses that have already been tagged as being labels due to transfers of control to that address.
 * Non explicit labels are those that belong to basic blocks that have already been constructed (i.e. have
 * previously been parsed) and now need to be made explicit labels.     In the case of non explicit labels, the
 * basic block is split into two and types and edges are adjusted accordingly. pNewBB is set to the lower part
 * of the split BB.
 * Returns true if the native address is that of an explicit or non explicit label, false otherwise.
 */
/***************************************************************************//**
 *
 * \brief    Checks whether the given native address is a label (explicit or non explicit) or not. Returns false for
 *                incomplete BBs.
 *
 *  So it returns true iff the address has already been decoded in some BB. If it was not
 *  already a label (i.e. the first instruction of some BB), the BB is split so that it becomes a label.
 *  Explicit labels are addresses that have already been tagged as being labels due to transfers of control
 *  to that address, and are therefore the start of some BB.     Non explicit labels are those that belong
 *  to basic blocks that have already been constructed (i.e. have previously been parsed) and now need to
 *  be made explicit labels. In the case of non explicit labels, the basic block is split into two and types
 *  and edges are adjusted accordingly. If \a pCurBB is the BB that gets split, it is changed to point to the
 *  address of the new (lower) part of the split BB.
 *  If there is an incomplete entry in the table for this address which overlaps with a completed address,
 *  the completed BB is split and the BB for this address is completed.
 * \param         uNativeAddress - native (source) address to check
 * \param         pCurBB - See above
 * \returns       True if uNativeAddr is a label, i.e. (now) the start of a BB
 *                Note: pCurBB may be modified (as above)
 ******************************************************************************/
bool Cfg::label ( ADDRESS uNativeAddr, PBB& pCurBB ) {
    MAPBB::iterator mi, newi;


    mi = m_mapBB.find (uNativeAddr);            // check if the native address is in the map already (explicit label)
    if (mi == m_mapBB.end()) {                  // not in the map
                                                // If not an explicit label, temporarily add the address to the map
        m_mapBB[uNativeAddr] = nullptr;         // no PBB yet
                                                // get an iterator to the new native address and check if the previous
                                                // element in the (sorted) map overlaps this new native address; if so,
                                                // it's a non-explicit label which needs to be made explicit by
                                                // splitting the previous BB.
        mi = m_mapBB.find (uNativeAddr);
        newi = mi;
        bool bSplit = false;
        PBB pPrevBB = nullptr;
        if (newi != m_mapBB.begin()) {
            pPrevBB = (*--mi).second;
            if (!pPrevBB->m_bIncomplete &&
                    (pPrevBB->getLowAddr() < uNativeAddr) &&
                    (pPrevBB->getHiAddr () >= uNativeAddr)) {
                bSplit = true;
            }
        }
        if (bSplit) {
                                                // Non-explicit label. Split the previous BB
            PBB pNewBB = splitBB (pPrevBB, uNativeAddr);
            if (pCurBB == pPrevBB) {
                                                // This means that the BB that we are expecting to use, usually to add
                                                // out edges, has changed. We must change this pointer so that the right
                                                // BB gets the out edges. However, if the new BB is not the BB of
                                                // interest, we mustn't change pCurBB
                pCurBB = pNewBB;
            }
            return true;                        // wasn't a label, but already parsed
        }
        else {                                  // not a non-explicit label
                                                // We don't have to erase this map entry. Having a null BasicBlock
                                                // pointer is coped with in newBB() and addOutEdge(); when eventually
                                                // the BB is created, it will replace this entry.  We should be
                                                // currently processing this BB. The map will be corrected when newBB is
                                                // called with this address.
            return false;                       // was not already parsed
        }
    }
    else {                                      // We already have uNativeAddr in the map
        if ((*mi).second && !(*mi).second->m_bIncomplete) {
            return true;                        // There is a complete BB here. Return true.
        }

                                                // We are finalising an incomplete BB. Still need to check previous map
                                                // entry to see if there is a complete BB overlapping
        bool bSplit = false;
        PBB pPrevBB, pBB = (*mi).second;
        if (mi != m_mapBB.begin())  {
            pPrevBB = (*--mi).second;
            if (!pPrevBB->m_bIncomplete &&
                    (pPrevBB->getLowAddr() < uNativeAddr) &&
                    (pPrevBB->getHiAddr () >= uNativeAddr))
                bSplit = true;
        }
        if (bSplit) {
                                                // Pass the third parameter to splitBB, because we already have an
                                                // (incomplete) BB for the "bottom" BB of the split
            splitBB (pPrevBB, uNativeAddr, pBB);// non-explicit label
            return true;                        // wasn't a label, but already parsed
        }
                                                // A non overlapping, incomplete entry is in the map.
        return false;
    }
}

// Return true if there is an incomplete BB already at this address
/***************************************************************************//**
 *
 * \brief        Return true if given address is the start of an incomplete basic block
 *
 * Checks whether the given native address is in the map. If not, returns false. If so, returns true if it is
 * incomplete. Otherwise, returns false.
 *
 * \param       uAddr Address to look up
 * \returns     True if uAddr starts an incomplete BB
 ******************************************************************************/
bool Cfg::isIncomplete(ADDRESS uAddr) {
    MAPBB::iterator mi = m_mapBB.find(uAddr);
    if (mi == m_mapBB.end())
        // No entry at all
        return false;
    // Else, there is a BB there. If it's incomplete, return true
    BasicBlock * pBB = (*mi).second;
    return pBB->m_bIncomplete;
}

/***************************************************************************//**
 *
 * \brief   Sorts the BBs in a cfg by first address. Just makes it more convenient to read when BBs are
 * iterated.
 *
 * Sorts the BBs in the CFG according to the low address of each BB.  Useful because it makes printouts easier,
 * if they used iterators to traverse the list of BBs.
 *
 ******************************************************************************/
void Cfg::sortByAddress() {
    m_listBB.sort(BasicBlock::lessAddress);
}

/***************************************************************************//**
 *
 * \brief        Sorts the BBs in a cfg by their first DFT numbers.
 ******************************************************************************/
void Cfg::sortByFirstDFT() {
#ifndef _WIN32
    m_listBB.sort(BasicBlock::lessFirstDFT);
#else
    updateVectorBB();
    for (std::list<PBB>::iterator it = m_listBB.begin(); it != m_listBB.end(); it++)
        m_vectorBB[(*it)->m_DFTfirst-1] = *it;
    m_listBB.clear();
    for (size_t i = 0; i < m_vectorBB.size(); i++)
        m_listBB.push_back(m_vectorBB[i]);
#endif
}

/***************************************************************************//**
 * \brief        Sorts the BBs in a cfg by their last DFT numbers.
 ******************************************************************************/
void Cfg::sortByLastDFT() {
#ifndef _WIN32
    m_listBB.sort(BasicBlock::lessLastDFT);
#else
    updateVectorBB();
    for (std::list<PBB>::iterator it = m_listBB.begin(); it != m_listBB.end(); it++)
        m_vectorBB[(*it)->m_DFTlast-1] = *it;
    m_listBB.clear();
    for (size_t i = 0; i < m_vectorBB.size(); i++)
        m_listBB.push_back(m_vectorBB[i]);
#endif
}

/***************************************************************************//**
 * \brief        Updates m_vectorBB to m_listBB
 ******************************************************************************/
void Cfg::updateVectorBB() {
    m_vectorBB.clear();
    for (std::list<PBB>::iterator it = m_listBB.begin(); it != m_listBB.end(); it++)
        m_vectorBB.push_back(*it);
}


/***************************************************************************//**
 *
 * \brief Checks that all BBs are complete, and all out edges are valid. However, ADDRESSes that are
 * interprocedural out edges are not checked or changed.
 *
 * Transforms the input machine-dependent cfg, which has ADDRESS labels for each out-edge, into a machine-
 * independent cfg graph (i.e. a well-formed graph) which has references to basic blocks for each out-edge.
 *
 * \returns True if transformation was successful
 ******************************************************************************/
bool Cfg::wellFormCfg() {
    m_bWellFormed = true;
    for (BB_IT it = m_listBB.begin(); it != m_listBB.end(); it++) {
        // it iterates through all BBs in the list
        // Check that it's complete
        BasicBlock *current = *it;
        if (current->m_bIncomplete) {
            m_bWellFormed = false;
            MAPBB::iterator itm;
            for (itm = m_mapBB.begin(); itm != m_mapBB.end(); itm++)
                if ((*itm).second == *it) break;
            if (itm == m_mapBB.end())
                std::cerr << "WellFormCfg: incomplete BB not even in map!\n";
            else {
                std::cerr << "WellFormCfg: BB with native address ";
                std::cerr << std::hex << (*itm).first << " is incomplete\n";
            }
        } else {
            // Complete. Test the out edges
            assert((int)current->m_OutEdges.size() == current->m_iNumOutEdges);
            for (int i=0; i < current->m_iNumOutEdges; i++) {
                // check if address is interprocedural
                //                if ((*it)->m_OutEdgeInterProc[i] == false)
                {
                    // i iterates through the outedges in the BB *it
                    PBB pBB = current->m_OutEdges[i];

                    // Check that the out edge has been written (i.e. nonzero)
                    if (pBB == NULL) {
                        m_bWellFormed = false;    // At least one problem
                        ADDRESS addr = current->getLowAddr();
                        std::cerr << "WellFormCfg: BB with native address " << std::hex << addr <<
                                     " is missing outedge " << i << std::endl;
                    }
                    else {
                        // Check that there is a corresponding in edge from the
                        // child to here
                        std::vector<PBB>::iterator ii;
                        for (ii=pBB->m_InEdges.begin(); ii != pBB->m_InEdges.end(); ii++)
                            if (*ii == *it)
                                break;
                        if (ii == pBB->m_InEdges.end()) {
                            std::cerr << "WellFormCfg: No in edge to BB at " << std::hex << (*it)->getLowAddr() <<
                                         " from successor BB at " << pBB->getLowAddr() << std::endl;
                            m_bWellFormed = false;    // At least one problem
                        }
                    }
                }
            }
            // Also check that each in edge has a corresponding out edge to here (could have an extra in-edge, for
            // example)
            assert((int)(*it)->m_InEdges.size() == (*it)->m_iNumInEdges);
            std::vector<PBB>::iterator ii;
            for (ii = (*it)->m_InEdges.begin(); ii != (*it)->m_InEdges.end(); ii++) {
                std::vector<PBB>::iterator oo;
                for (oo=(*ii)->m_OutEdges.begin(); oo != (*ii)->m_OutEdges.end(); oo++)
                    if (*oo == *it) break;
                if (oo == (*ii)->m_OutEdges.end()) {
                    std::cerr << "WellFormCfg: No out edge to BB at " << std::hex << (*it)->getLowAddr() <<
                                 " from predecessor BB at " << (*ii)->getLowAddr() << std::endl;
                    m_bWellFormed = false;    // At least one problem
                }
            }
        }
    }
    return m_bWellFormed;
}

/***************************************************************************//**
 *
 * Given two basic blocks that belong to a well-formed graph, merges the second block onto the first one and
 * returns the new block.  The in and out edges links are updated accordingly.
 * Note that two basic blocks can only be merged if each has a unique out-edge and in-edge respectively, and
 * these edges correspond to each other.
 * \returns            true if the blocks are merged.
 ******************************************************************************/
bool Cfg::mergeBBs( BasicBlock *pb1, BasicBlock *pb2) {
    // Can only merge if pb1 has only one outedge to pb2, and pb2 has only one in-edge, from pb1. This can only be done
    // after the in-edges are done, which can only be done on a well formed CFG.
    if (!m_bWellFormed) return false;
    if (pb1->m_iNumOutEdges != 1) return false;
    if (pb2->m_iNumInEdges != 1) return false;
    if (pb1->m_OutEdges[0] != pb2) return false;
    if (pb2->m_InEdges[0] != pb1) return false;

    // Merge them! We remove pb1 rather than pb2, since this is also what is needed for many optimisations, e.g. jump to
    // jump.
    completeMerge(pb1, pb2, true);
    return true;
}

/***************************************************************************//**
 *
 * \brief Complete the merge of two BBs by adjusting in and out edges.  If bDelete is true, delete pb1
 *
 * Completes the merge of pb1 and pb2 by adjusting out edges. No checks are made that the merge is valid
 * (hence this is a private function) Deletes pb1 if bDelete is true
 *
 * \param pb1 pointers to the two BBs to merge
 * \param pb2 pointers to the two BBs to merge
 * \param bDelete: if true, pb1 is deleted as well
 *
 ******************************************************************************/
void Cfg::completeMerge(PBB pb1, PBB pb2, bool bDelete) {
    // First we replace all of pb1's predecessors' out edges that used to point to pb1 (usually only one of these) with
    // pb2
    for (int i=0; i < pb1->m_iNumInEdges; i++)     {
        PBB pPred = pb1->m_InEdges[i];
        for (int j=0; j < pPred->m_iNumOutEdges; j++) {
            if (pPred->m_OutEdges[j] == pb1)
                pPred->m_OutEdges[j] = pb2;
        }
    }

    // Now we replace pb2's in edges by pb1's inedges
    pb2->m_InEdges = pb1->m_InEdges;
    pb2->m_iNumInEdges = pb1->m_iNumInEdges;

    if (bDelete) {
        // Finally, we delete pb1 from the BB list. Note: remove(pb1) should also work, but it would involve member
        // comparison (not implemented), and also would attempt to remove ALL elements of the list with this value (so
        // it has to search the whole list, instead of an average of half the list as we have here).
        for (BB_IT it = m_listBB.begin(); it != m_listBB.end(); it++) {
            if (*it == pb1) {
                m_listBB.erase(it);
                break;
            }
        }
    }
}

/***************************************************************************//**
 *
 * \brief Amalgamate the RTLs for pb1 and pb2, and place the result into pb2
 *
 * This is called where a two-way branch is deleted, thereby joining a two-way BB with it's successor.
 * This happens for example when transforming Intel floating point branches, and a branch on parity is deleted.
 * The joined BB becomes the type of the successor.
 *
 * \note Assumes that fallthrough of *pb1 is *pb2
 *
 * \param   pb1 pointers to the BBs to join
 * \param   pb2 pointers to the BBs to join
 * \returns True if successful
 ******************************************************************************/
bool Cfg::joinBB(PBB pb1, PBB pb2) {
    // Ensure that the fallthrough case for pb1 is pb2
    std::vector<PBB>& v = pb1->getOutEdges();
    if (v.size() != 2 || v[1] != pb2)
        return false;
    // Prepend the RTLs for pb1 to those of pb2. Since they will be pushed to the front of pb2, push them in reverse
    // order
    std::list<RTL*>::reverse_iterator it;
    for (it = pb1->m_pRtls->rbegin(); it != pb1->m_pRtls->rend(); it++) {
        pb2->m_pRtls->push_front(*it);
    }
    completeMerge(pb1, pb2);                // Mash them together
    // pb1 no longer needed. Remove it from the list of BBs.  This will also delete *pb1. It will be a shallow delete,
    // but that's good because we only did shallow copies to *pb2
    BB_IT bbit = std::find(m_listBB.begin(), m_listBB.end(), pb1);
    m_listBB.erase(bbit);
    return true;
}
/***************************************************************************//**
 *
 * \brief Completely remove a BB from the CFG.
 *
 ******************************************************************************/
void Cfg::removeBB( PBB bb) {
    BB_IT bbit = std::find(m_listBB.begin(), m_listBB.end(), bb);
    m_listBB.erase(bbit);
}

/***************************************************************************//**
 *
 * \brief   Compress the CFG. For now, it only removes BBs that are just branches
 *
 * Given a well-formed cfg graph, optimizations are performed on the graph to reduce the number of basic blocks
 * and edges.
 * Optimizations performed are: removal of branch chains (i.e. jumps to jumps), removal of redundant jumps (i.e.
 *  jump to the next instruction), merge basic blocks where possible, and remove redundant basic blocks created
 *  by the previous optimizations.
 * \returns            Returns false if not successful.
 ******************************************************************************/
bool Cfg::compressCfg() {
    // must be well formed
    if (!m_bWellFormed) return false;

    // FIXME: The below was working while we still had reaching definitions.  It seems to me that it would be easy to
    // search the BB for definitions between the two branches (so we don't need reaching defs, just the SSA property of
    //  unique definition).
    //
    // Look in CVS for old code.

    // Find A -> J -> B     where J is a BB that is only a jump
    // Then A -> B
    for (BB_IT it = m_listBB.begin(); it != m_listBB.end(); it++) {
        for (auto it1 = (*it)->m_OutEdges.begin(); it1 != (*it)->m_OutEdges.end(); it1++) {
            PBB pSucc = (*it1);            // Pointer to J
            PBB bb = (*it);                // Pointer to A
            if (pSucc->m_InEdges.size()==1 && pSucc->m_OutEdges.size()==1 &&
                    pSucc->m_pRtls->size()==1 &&
                    pSucc->m_pRtls->front()->getNumStmt() == 1 &&
                    pSucc->m_pRtls->front()->elementAt(0)->isGoto()) {
                // Found an out-edge to an only-jump BB
                /* std::cout << "outedge to jump detected at " << std::hex << bb->getLowAddr() << " to ";
                                        std::cout << pSucc->getLowAddr() << " to " << pSucc->m_OutEdges.front()->getLowAddr() << std::dec <<
                                        std::endl; */
                // Point this outedge of A to the dest of the jump (B)
                *it1=pSucc->m_OutEdges.front();
                // Now pSucc still points to J; *it1 points to B.  Almost certainly, we will need a jump in the low
                // level C that may be generated. Also force a label for B
                bb->m_bJumpReqd = true;
                setLabel(*it1);
                // Find the in-edge from B to J; replace this with an in-edge to A
                std::vector<PBB>::iterator it2;
                for (it2 = (*it1)->m_InEdges.begin();
                     it2 != (*it1)->m_InEdges.end(); it2++) {
                    if (*it2==pSucc)
                        *it2 = bb;            // Point to A
                }
                // Remove the in-edge from J to A. First find the in-edge
                for (it2 = pSucc->m_InEdges.begin();
                     it2 != pSucc->m_InEdges.end(); it2++) {
                    if (*it2 == bb)
                        break;
                }
                assert(it2 != pSucc->m_InEdges.end());
                pSucc->deleteInEdge(it2);
                // If nothing else uses this BB (J), remove it from the CFG
                if (pSucc->m_iNumInEdges == 0) {
                    for (BB_IT it3 = m_listBB.begin(); it3 != m_listBB.end();
                         it3++) {
                        if (*it3==pSucc) {
                            m_listBB.erase(it3);
                            // And delete the BB
                            delete pSucc;
                            break;
                        }
                    }
                }
            }
        }
    }
    return true;
}

/***************************************************************************//**
 *
 * \breif   Reset all the traversed flags.
 *
 * Reset all the traversed flags.
 * To make this a useful public function, we need access to the traversed flag with other public functions.
 *
 ******************************************************************************/
void Cfg::unTraverse() {
    for ( BasicBlock * it : m_listBB ) {
        it->m_iTraversed = false;
        it->traversed = UNTRAVERSED;
    }
}

/***************************************************************************//**
 *
 * \brief        Given a well-formed cfg graph, a partial ordering is established between the nodes. The ordering is
 *                    based on the final visit to each node during a depth first traversal such that if node n1 was
 *                    visited for the last time before node n2 was visited for the last time, n1 will be less than n2.
 *                    The return value indicates if all nodes where ordered. This will not be the case for incomplete CFGs
 *                    (e.g. switch table not completely recognised) or where there are nodes unreachable from the entry
 *                    node.
 * \returns            all nodes where ordered
 ******************************************************************************/
bool Cfg::establishDFTOrder() {
    // Must be well formed.
    if (!m_bWellFormed)
        return false;

    // Reset all the traversed flags
    unTraverse();

    int first = 0;
    int last = 0;
    unsigned numTraversed;

    if (checkEntryBB())
        return false;

    numTraversed = entryBB->DFTOrder(first,last);

    return numTraversed == m_listBB.size();
}

PBB Cfg::findRetNode() {
    PBB retNode = nullptr;
    for ( BasicBlock * bb : m_listBB ) {
        if (bb->getType() == RET) {
            return bb;
        } else if (bb->getType() == CALL) {
            Proc *p = bb->getCallDestProc();
            if (p && !strcmp(p->getName(), "exit")) // TODO: move this into check Proc::noReturn();
                retNode = bb;
        }
    }
    return retNode;
}

/***************************************************************************//**
 *
 * \brief        Performs establishDFTOrder on the reverse (flip) of the graph, assumes: establishDFTOrder has
 *                    already been called
 *
 * \returns            all nodes where ordered
 ******************************************************************************/
bool Cfg::establishRevDFTOrder() {
    // Must be well formed.
    if (!m_bWellFormed)
        return false;

    // WAS: sort by last dfs and grab the exit node
    // Why?     This does not seem like a the best way. What we need is the ret node, so let's find it.
    // If the CFG has more than one ret node then it needs to be fixed.
    //sortByLastDFT();

    PBB retNode = findRetNode();

    if (retNode == NULL)
        return false;

    // Reset all the traversed flags
    unTraverse();

    int first = 0;
    int last = 0;
    unsigned numTraversed = retNode->RevDFTOrder(first,last);

    return numTraversed == m_listBB.size();
}

/***************************************************************************//**
 *
 * \brief Query the wellformed'ness status
 * \returns m_bWellFormed
 ******************************************************************************/
bool Cfg::isWellFormed() {
    return m_bWellFormed;
}

/***************************************************************************//**
 *
 * \brief Return true if there is a BB at the address given whose first RTL is an orphan,
 * i.e. GetAddress() returns 0.
 * \returns            <nothing>
 ******************************************************************************/
bool Cfg::isOrphan(ADDRESS uAddr) {
    MAPBB::iterator mi = m_mapBB.find(uAddr);
    if (mi == m_mapBB.end())
        return false; // No entry at all
    // Return true if the first RTL at this address has an address set to 0
    PBB pBB = (*mi).second;
    // If it's incomplete, it can't be an orphan
    if (pBB->m_bIncomplete)
        return false;
    return pBB->m_pRtls->front()->getAddress().isZero();
}

/***************************************************************************//**
 *
 * \brief   Return an index for the given PBB
 *
 * Given a pointer to a basic block, return an index (e.g. 0 for the first basic block, 1 for the next, ... n-1
 * for the last BB.
 *
 * \note Linear search: O(N) complexity
 * \param pBB - BasicBlock to find
 * \returns     Index, or -1 for unknown PBB
 ******************************************************************************/
int Cfg::pbbToIndex (PBB pBB) {
    BB_IT it = m_listBB.begin();
    int i = 0;
    while (it != m_listBB.end()) {
        if (*it++ == pBB) return i;
        i++;
    }
    return -1;
}

/***************************************************************************//**
 *
 * \brief Add a call to the set of calls within this procedure.
 * \param call - a call instruction
 *
 ******************************************************************************/
void Cfg::addCall(CallStatement* call) {
    callSites.insert(call);
}

/***************************************************************************//**
 *
 * \brief        Get the set of calls within this procedure.
 *
 * \returns            the set of calls within this procedure
 ******************************************************************************/
Cfg::sCallStatement & Cfg::getCalls() {
    return callSites;
}
/***************************************************************************//**
 * \brief Replace all instances of search with replace. Can be type sensitive if
 * reqd
 * \param search a location to search for
 * \param replace the expression with which to replace it
 ******************************************************************************/
void Cfg::searchAndReplace(Exp* search, Exp* replace) {
    for (BasicBlock *bb : m_listBB) {
        for (RTL * rtl_it : *bb->getRTLs()) {
            RTL& rtl(*rtl_it);
            rtl.searchAndReplace(search,replace);
        }
    }
}

bool Cfg::searchAll(Exp *search, std::list<Exp*> &result) {
    bool ch = false;
    for (BasicBlock *bb : m_listBB) {
        std::list<RTL*>& rtls(*bb->getRTLs());
        for (RTL * rtl_it : rtls) {
            RTL& rtl(*rtl_it);
            ch |= rtl.searchAll(search, result);
        }
    }
    return ch;
}

/***************************************************************************//**
 *
 * \brief    "deep" delete for a list of pointers to RTLs
 * \param pLrtl - the list
 * \returns        <none>
 ******************************************************************************/
void delete_lrtls(std::list<RTL*>& pLrtl) {
    for (RTL *it : pLrtl)
        delete it;
}

/***************************************************************************//**
 *
 * \brief   "deep" erase for a list of pointers to RTLs
 * \param   pLrtls - the list
 * \param   begin - iterator to first (inclusive) item to delete
 * \param   end - iterator to last (exclusive) item to delete
 *
 ******************************************************************************/
void erase_lrtls(std::list<RTL*>& pLrtl, std::list<RTL*>::iterator begin,
                 std::list<RTL*>::iterator end) {
    for (auto it = begin; it != end; it++) {
        delete (*it);
    }
    pLrtl.erase(begin, end);
}

/***************************************************************************//**
 *
 * \brief Adds a label for the given basicblock. The label number will be a non-zero integer
 *
 *        Sets a flag indicating that this BB has a label, in the sense that a label is required in the
 * translated source code
 * \note         The label is only set if it was not set previously
 * \param        pBB Pointer to the BB whose label will be set
 ******************************************************************************/
void Cfg::setLabel(BasicBlock * pBB) {
    if (pBB->m_iLabelNum == 0)
        pBB->m_iLabelNum = ++lastLabel;
}

/***************************************************************************//**
 *
 * \brief Set an additional new out edge to a given value
 *
 * Append a new out-edge from the given BB to the other given BB
 * Needed for example when converting a one-way BB to a two-way BB
 *
 * \note        Use BasicBlock::setOutEdge() for the common case where an existing out edge is merely changed
 * \note        Use Cfg::addOutEdge for ordinary BB creation; this is for unusual cfg manipulation
 * \note        side effect : Increments m_iNumOutEdges
 *
 * \param pFromBB pointer to the BB getting the new out edge
 * \param pNewOutEdge pointer to BB that will be the new successor
 * \returns            <nothing>
 ******************************************************************************/
void Cfg::addNewOutEdge(PBB pFromBB, PBB pNewOutEdge) {
    pFromBB->m_OutEdges.push_back(pNewOutEdge);
    pFromBB->m_iNumOutEdges++;
    // Since this is a new out-edge, set the "jump required" flag
    pFromBB->m_bJumpReqd = true;
    // Make sure that there is a label there
    setLabel(pNewOutEdge);
}

/***************************************************************************//**
 *
 * \brief Simplify all the expressions in the CFG
 *
 ******************************************************************************/
void Cfg::simplify() {
    if (VERBOSE)
        LOG << "simplifying...\n";
    for (BasicBlock * it : m_listBB)
        it->simplify();
}

// print this cfg, mainly for debugging
void Cfg::print(std::ostream &out, bool html) {
    for (BasicBlock * it : m_listBB)
        it->print(out, html);
    out << std::endl;
}

void Cfg::dump() {
    print(std::cerr);
}

void Cfg::dumpImplicitMap() {
    for (auto it : implicitMap) {
        std::cerr << it.first << " -> " << it.second << "\n";
    }
}

void Cfg::printToLog() {
    std::ostringstream ost;
    print(ost);
    LOG << ost.str().c_str();
}

void Cfg::setTimeStamps() {
    // set DFS tag
    for (BasicBlock * it : m_listBB)
        it->traversed = DFS_TAG;

    // set the parenthesis for the nodes as well as setting the post-order ordering between the nodes
    int time = 1;
    Ordering.clear();
    entryBB->setLoopStamps(time, Ordering);

    // set the reverse parenthesis for the nodes
    time = 1;
    entryBB->setRevLoopStamps(time);

    PBB retNode = findRetNode();
    assert(retNode);
    revOrdering.clear();
    retNode->setRevOrder(revOrdering);
}

// Finds the common post dominator of the current immediate post dominator and its successor's immediate post dominator
BasicBlock *Cfg::commonPDom(BasicBlock *  curImmPDom, BasicBlock *  succImmPDom) {
    if (!curImmPDom)
        return succImmPDom;
    if (!succImmPDom)
        return curImmPDom;
    if (curImmPDom->revOrd == succImmPDom->revOrd)
        return curImmPDom;  // ordering hasn't been done

    PBB oldCurImmPDom = curImmPDom;
    PBB oldSuccImmPDom = succImmPDom;

    int giveup = 0;
#define GIVEUP 10000
    while (giveup < GIVEUP && curImmPDom && succImmPDom && (curImmPDom != succImmPDom)) {
        if (curImmPDom->revOrd > succImmPDom->revOrd)
            succImmPDom = succImmPDom->immPDom;
        else
            curImmPDom = curImmPDom->immPDom;
        giveup++;
    }

    if (giveup >= GIVEUP) {
        if (VERBOSE)
            LOG << "failed to find commonPDom for " << oldCurImmPDom->getLowAddr() << " and " <<
                   oldSuccImmPDom->getLowAddr() << "\n";
        return oldCurImmPDom;  // no change
    }

    return curImmPDom;
}

/** Finds the immediate post dominator of each node in the graph PROC->cfg.  Adapted version of the dominators algorithm
 * by Hecht and Ullman; finds immediate post dominators only.  \note graph should be reducible
 */
void Cfg::findImmedPDom() {
    PBB curNode, succNode;    // the current Node and its successor

    // traverse the nodes in order (i.e from the bottom up)
    int i;
    for (i = revOrdering.size() - 1; i >= 0; i--) {
        curNode = revOrdering[i];
        std::vector<PBB> &oEdges = curNode->getOutEdges();
        for (unsigned int j = 0; j < oEdges.size(); j++) {
            succNode = oEdges[j];
            if (succNode->revOrd > curNode->revOrd)
                curNode->immPDom = commonPDom(curNode->immPDom, succNode);
        }
    }

    // make a second pass but consider the original CFG ordering this time
    unsigned u;
    for (u = 0; u < Ordering.size(); u++) {
        curNode = Ordering[u];
        std::vector<PBB> &oEdges = curNode->getOutEdges();
        if (oEdges.size() > 1)
            for (unsigned int j = 0; j < oEdges.size(); j++) {
                succNode = oEdges[j];
                curNode->immPDom = commonPDom(curNode->immPDom, succNode);
            }
    }

    // one final pass to fix up nodes involved in a loop
    for (u = 0; u < Ordering.size(); u++) {
        curNode = Ordering[u];
        std::vector<PBB> &oEdges = curNode->getOutEdges();
        if (oEdges.size() > 1)
            for (unsigned int j = 0; j < oEdges.size(); j++) {
                succNode = oEdges[j];
                if (curNode->hasBackEdgeTo(succNode) && curNode->getOutEdges().size() > 1 &&
                        succNode->immPDom &&
                        succNode->immPDom->ord < curNode->immPDom->ord)
                    curNode->immPDom = commonPDom(succNode->immPDom, curNode->immPDom);
                else
                    curNode->immPDom = commonPDom(curNode->immPDom, succNode);
            }
    }
}

// Structures all conditional headers (i.e. nodes with more than one outedge)
void Cfg::structConds() {
    // Process the nodes in order
    for ( BasicBlock *curNode : Ordering) {

        // does the current node have more than one out edge?
        if (curNode->getOutEdges().size() > 1) {
            // if the current conditional header is a two way node and has a back edge, then it won't have a follow
            if (curNode->hasBackEdge() && curNode->getType() == TWOWAY) {
                curNode->setStructType(Cond);
                continue;
            }

            // set the follow of a node to be its immediate post dominator
            curNode->setCondFollow(curNode->immPDom);

            // set the structured type of this node
            curNode->setStructType(Cond);

            // if this is an nway header, then we have to tag each of the nodes within the body of the nway subgraph
            if (curNode->getCondType() == Case)
                curNode->setCaseHead(curNode,curNode->getCondFollow());
        }
    }
}

// Pre: The loop induced by (head,latch) has already had all its member nodes tagged
// Post: The type of loop has been deduced
void Cfg::determineLoopType(PBB header, bool* &loopNodes) {
    assert(header->getLatchNode());

    // if the latch node is a two way node then this must be a post tested loop
    if (header->getLatchNode()->getType() == TWOWAY) {
        header->setLoopType(PostTested);

        // if the head of the loop is a two way node and the loop spans more than one block  then it must also be a
        // conditional header
        if (header->getType() == TWOWAY && header != header->getLatchNode())
            header->setStructType(LoopCond);
    }

    // otherwise it is either a pretested or endless loop
    else if (header->getType() == TWOWAY) {
        // if the header is a two way node then it must have a conditional follow (since it can't have any backedges
        // leading from it). If this follow is within the loop then this must be an endless loop
        if (header->getCondFollow() && loopNodes[header->getCondFollow()->ord]) {
            header->setLoopType(Endless);

            // retain the fact that this is also a conditional header
            header->setStructType(LoopCond);
        } else
            header->setLoopType(PreTested);
    }
    // both the header and latch node are one way nodes so this must be an endless loop
    else
        header->setLoopType(Endless);
}

// Pre: The loop headed by header has been induced and all it's member nodes have been tagged
// Post: The follow of the loop has been determined.
void Cfg::findLoopFollow(PBB header, bool* &loopNodes) {
    assert(header->getStructType() == Loop || header->getStructType() == LoopCond);
    loopType lType = header->getLoopType();
    PBB latch = header->getLatchNode();

    if (lType == PreTested) {
        // if the 'while' loop's true child is within the loop, then its false child is the loop follow
        if (loopNodes[header->getOutEdges()[0]->ord])
            header->setLoopFollow(header->getOutEdges()[1]);
        else
            header->setLoopFollow(header->getOutEdges()[0]);
    } else if (lType == PostTested) {
        // the follow of a post tested ('repeat') loop is the node on the end of the non-back edge from the latch node
        if (latch->getOutEdges()[0] == header)
            header->setLoopFollow(latch->getOutEdges()[1]);
        else
            header->setLoopFollow(latch->getOutEdges()[0]);
    } else { // endless loop
        PBB follow = NULL;

        // traverse the ordering array between the header and latch nodes.
        PBB latch = header->getLatchNode();
        for (int i = header->ord - 1; i > latch->ord; i--) {
            PBB &desc = Ordering[i];
            // the follow for an endless loop will have the following
            // properties:
            //     i) it will have a parent that is a conditional header inside the loop whose follow is outside the
            //        loop
            //    ii) it will be outside the loop according to its loop stamp pair
            // iii) have the highest ordering of all suitable follows (i.e. highest in the graph)

            if (desc->getStructType() == Cond && desc->getCondFollow() &&
                    desc->getLoopHead() == header) {
                if (loopNodes[desc->getCondFollow()->ord]) {
                    // if the conditional's follow is in the same loop AND is lower in the loop, jump to this follow
                    if (desc->ord > desc->getCondFollow()->ord)
                        i = desc->getCondFollow()->ord;
                    // otherwise there is a backward jump somewhere to a node earlier in this loop. We don't need to any
                    //  nodes below this one as they will all have a conditional within the loop.
                    else break;
                } else {
                    // otherwise find the child (if any) of the conditional header that isn't inside the same loop
                    PBB succ = desc->getOutEdges()[0];
                    if (loopNodes[succ->ord]) {
                        if (!loopNodes[desc->getOutEdges()[1]->ord])
                            succ = desc->getOutEdges()[1];
                        else
                            succ = NULL;
                    }
                    // if a potential follow was found, compare its ordering with the currently found follow
                    if (succ && (!follow || succ->ord > follow->ord))
                        follow = succ;
                }
            }
        }
        // if a follow was found, assign it to be the follow of the loop under
        // investigation
        if (follow)
            header->setLoopFollow(follow);
    }
}

// Pre: header has been detected as a loop header and has the details of the
//        latching node
// Post: the nodes within the loop have been tagged
void Cfg::tagNodesInLoop(PBB header, bool* &loopNodes) {
    assert(header->getLatchNode());

    // traverse the ordering structure from the header to the latch node tagging the nodes determined to be within the
    // loop. These are nodes that satisfy the following:
    //    i) header.loopStamps encloses curNode.loopStamps and curNode.loopStamps encloses latch.loopStamps
    //    OR
    //    ii) latch.revLoopStamps encloses curNode.revLoopStamps and curNode.revLoopStamps encloses header.revLoopStamps
    //    OR
    //    iii) curNode is the latch node

    PBB latch = header->getLatchNode();
    for (int i = header->ord - 1; i >= latch->ord; i--)
        if (Ordering[i]->inLoop(header, latch)) {
            // update the membership map to reflect that this node is within the loop
            loopNodes[i] = true;

            Ordering[i]->setLoopHead(header);
        }
}

// Pre: The graph for curProc has been built.
// Post: Each node is tagged with the header of the most nested loop of which it is a member (possibly none).
// The header of each loop stores information on the latching node as well as the type of loop it heads.
void Cfg::structLoops() {
    for (int i = Ordering.size() - 1; i >= 0; i--) {
        PBB curNode = Ordering[i];    // the current node under investigation
        PBB latch = NULL;            // the latching node of the loop

        // If the current node has at least one back edge into it, it is a loop header. If there are numerous back edges
        // into the header, determine which one comes form the proper latching node.
        // The proper latching node is defined to have the following properties:
        //     i) has a back edge to the current node
        //    ii) has the same case head as the current node
        // iii) has the same loop head as the current node
        //    iv) is not an nway node
        //     v) is not the latch node of an enclosing loop
        //    vi) has a lower ordering than all other suitable candiates
        // If no nodes meet the above criteria, then the current node is not a loop header

        std::vector<PBB> &iEdges = curNode->getInEdges();
        for (unsigned int j = 0; j < iEdges.size(); j++) {
            PBB pred = iEdges[j];
            if (pred->getCaseHead() == curNode->getCaseHead() &&  // ii)
                    pred->getLoopHead() == curNode->getLoopHead() &&  // iii)
                    (!latch || latch->ord > pred->ord) &&              // vi)
                    !(pred->getLoopHead() &&
                      pred->getLoopHead()->getLatchNode() == pred) && // v)
                    pred->hasBackEdgeTo(curNode))                      // i)
                latch = pred;
        }

        // if a latching node was found for the current node then it is a loop header.
        if (latch) {
            // define the map that maps each node to whether or not it is within the current loop
            bool* loopNodes = new bool[Ordering.size()];
            for (unsigned int j = 0; j < Ordering.size(); j++)
                loopNodes[j] = false;

            curNode->setLatchNode(latch);

            // the latching node may already have been structured as a conditional header. If it is not also the loop
            // header (i.e. the loop is over more than one block) then reset it to be a sequential node otherwise it
            // will be correctly set as a loop header only later
            if (latch != curNode && latch->getStructType() == Cond)
                latch->setStructType(Seq);

            // set the structured type of this node
            curNode->setStructType(Loop);

            // tag the members of this loop
            tagNodesInLoop(curNode, loopNodes);

            // calculate the type of this loop
            determineLoopType(curNode, loopNodes);

            // calculate the follow node of this loop
            findLoopFollow(curNode, loopNodes);

            // delete the space taken by the loopnodes map
            //delete[] loopNodes;
        }
    }
}

// This routine is called after all the other structuring has been done. It detects conditionals that are in fact the
// head of a jump into/outof a loop or into a case body. Only forward jumps are considered as unstructured backward
//jumps will always be generated nicely.
void Cfg::checkConds() {
    for (unsigned int i = 0; i < Ordering.size(); i++) {
        PBB curNode = Ordering[i];
        std::vector<PBB> &oEdges = curNode->getOutEdges();

        // consider only conditional headers that have a follow and aren't case headers
        if ((curNode->getStructType() == Cond ||
             curNode->getStructType() == LoopCond) && curNode->getCondFollow() && curNode->getCondType() != Case) {
            // define convenient aliases for the relevant loop and case heads and the out edges
            PBB myLoopHead = (curNode->getStructType() == LoopCond ?  curNode : curNode->getLoopHead());
            PBB follLoopHead = curNode->getCondFollow()->getLoopHead();

            // analyse whether this is a jump into/outof a loop
            if (myLoopHead != follLoopHead) {
                // we want to find the branch that the latch node is on for a jump out of a loop
                if (myLoopHead) {
                    PBB myLoopLatch = myLoopHead->getLatchNode();

                    // does the then branch goto the loop latch?
                    if (oEdges[BTHEN]->isAncestorOf(myLoopLatch) || oEdges[BTHEN] == myLoopLatch) {
                        curNode->setUnstructType(JumpInOutLoop);
                        curNode->setCondType(IfElse);
                    }
                    // does the else branch goto the loop latch?
                    else if (oEdges[BELSE]->isAncestorOf(myLoopLatch) || oEdges[BELSE] == myLoopLatch) {
                        curNode->setUnstructType(JumpInOutLoop);
                        curNode->setCondType(IfThen);
                    }
                }

                if (curNode->getUnstructType() == Structured && follLoopHead) {
                    // find the branch that the loop head is on for a jump into a loop body. If a branch has already
                    // been found, then it will match this one anyway

                    // does the else branch goto the loop head?
                    if (oEdges[BTHEN]->isAncestorOf(follLoopHead) || oEdges[BTHEN] == follLoopHead) {
                        curNode->setUnstructType(JumpInOutLoop);
                        curNode->setCondType(IfElse);
                    }

                    // does the else branch goto the loop head?
                    else if (oEdges[BELSE]->isAncestorOf(follLoopHead) || oEdges[BELSE] == follLoopHead) {
                        curNode->setUnstructType(JumpInOutLoop);
                        curNode->setCondType(IfThen);
                    }
                }
            }

            // this is a jump into a case body if either of its children don't have the same same case header as itself
            if (curNode->getUnstructType() == Structured &&
                    (curNode->getCaseHead() != curNode->getOutEdges()[BTHEN]->getCaseHead() ||
                     curNode->getCaseHead() != curNode->getOutEdges()[BELSE]->getCaseHead())) {
                PBB myCaseHead = curNode->getCaseHead();
                PBB thenCaseHead = curNode->getOutEdges()[BTHEN]->getCaseHead();
                PBB elseCaseHead = curNode->getOutEdges()[BELSE]->getCaseHead();

                if (thenCaseHead == myCaseHead &&
                        (!myCaseHead || elseCaseHead != myCaseHead->getCondFollow())) {
                    curNode->setUnstructType(JumpIntoCase);
                    curNode->setCondType(IfElse);
                } else if (elseCaseHead == myCaseHead &&
                           (!myCaseHead || thenCaseHead != myCaseHead->getCondFollow())) {
                    curNode->setUnstructType(JumpIntoCase);
                    curNode->setCondType(IfThen);
                }
            }
        }

        // for 2 way conditional headers that don't have a follow (i.e. are the source of a back edge) and haven't been
        // structured as latching nodes, set their follow to be the non-back edge child.
        if (curNode->getStructType() == Cond &&
                !curNode->getCondFollow() &&
                curNode->getCondType() != Case &&
                curNode->getUnstructType() == Structured) {
            // latching nodes will already have been reset to Seq structured type
            if (curNode->hasBackEdge()) {
                if (curNode->hasBackEdgeTo(curNode->getOutEdges()[BTHEN])) {
                    curNode->setCondType(IfThen);
                    curNode->setCondFollow(curNode->getOutEdges()[BELSE]);
                } else {
                    curNode->setCondType(IfElse);
                    curNode->setCondFollow(curNode->getOutEdges()[BTHEN]);
                }
            }
        }
    }
}
/***************************************************************************//**
 * \brief Structures the control flow graph
 *******************************************************************************/
void Cfg::structure() {
    if (structured) {
        unTraverse();
        return;
    }
    if (findRetNode() == NULL)
        return;
    setTimeStamps();
    findImmedPDom();
    if (!Boomerang::get()->noDecompile) {
        structConds();
        structLoops();
        checkConds();
    }
    structured = true;
}
/*
 * Add/Remove Junction statements
 */

/***************************************************************************//**
 * \brief Add Junction statements
 *******************************************************************************/
void Cfg::addJunctionStatements() {
    for (BasicBlock * pbb : m_listBB) {
        if (pbb->getNumInEdges() > 1 && (pbb->getFirstStmt() == NULL || !pbb->getFirstStmt()->isJunction())) {
            assert(pbb->getRTLs());
            JunctionStatement *j = new JunctionStatement();
            j->setBB(pbb);
            pbb->getRTLs()->front()->prependStmt(j);
        }
    }
}

/***************************************************************************//**
 * \brief Remove Junction statements
 *******************************************************************************/
void Cfg::removeJunctionStatements() {
    for (BasicBlock * pbb : m_listBB) {
        if (pbb->getFirstStmt() && pbb->getFirstStmt()->isJunction()) {
            assert(pbb->getRTLs());
            pbb->getRTLs()->front()->deleteStmt(0);
        }
    }
}
void Cfg::removeUnneededLabels(HLLCode *hll) {
    hll->RemoveUnusedLabels(Ordering.size());
}

#define BBINDEX 0                // Non zero to print <index>: before <statement number>
#define BACK_EDGES 0            // Non zero to generate green back edges
void Cfg::generateDotFile(std::ofstream& of) {
    ADDRESS aret = NO_ADDRESS;
    // The nodes
    //std::list<PBB>::iterator it;
    for (BasicBlock * pbb : m_listBB) {
        of << "       " << "bb" << std::hex << pbb->getLowAddr() << " [" << "label=\"";
        char* p = pbb->getStmtNumber();
#if BBINDEX
        of << std::dec << indices[*it];
        if (p[0] != 'b')
            // If starts with 'b', no statements (something like bb8101c3c).
            of << ":";
#endif
        of << p << " ";
        switch(pbb->getType()) {
            case ONEWAY: of << "oneway"; break;
            case TWOWAY:
                if (pbb->getCond()) {
                    of << "\\n";
                    pbb->getCond()->print(of);
                    of << "\" shape=diamond];\n";
                    continue;
                }
                else
                    of << "twoway";
                break;
            case NWAY: {
                of << "nway";
                Exp* de = pbb->getDest();
                if (de) {
                    of << "\\n";
                    of << de;
                }
                of << "\" shape=trapezium];\n";
                continue;
            }
            case CALL: {
                of << "call";
                Proc* dest = pbb->getDestProc();
                if (dest) of << "\\n" << dest->getName();
                break;
            }
            case RET: {
                of << "ret\" shape=triangle];\n";
                // Remember the (unbique) return BB's address
                aret = pbb->getLowAddr();
                continue;
            }
            case FALL: of << "fall"; break;
            case COMPJUMP: of << "compjump"; break;
            case COMPCALL: of << "compcall"; break;
            case INVALID: of << "invalid"; break;
        }
        of << "\"];\n";
    }

    // Force the one return node to be at the bottom (max rank). Otherwise, with all its in-edges, it will end up in the
    // middle
    if (!aret.isZero())
        of << "{rank=max; bb" << std::hex << aret << "}\n";

    // Close the subgraph
    of << "}\n";

    // Now the edges
    for (BasicBlock * pbb : m_listBB) {
        std::vector<PBB>& outEdges = pbb->getOutEdges();
        for (unsigned int j = 0; j < outEdges.size(); j++) {
            of << "       " << "bb" << std::hex << pbb->getLowAddr() << " -> ";
            of << "bb" << std::hex << outEdges[j]->getLowAddr();
            if (pbb->getType() == TWOWAY) {
                if (j == 0)
                    of << " [label=\"true\"]";
                else
                    of << " [label=\"false\"]";
            }
            of << " [color = \"blue\"];\n";
        }
    }
#if BACK_EDGES
    for (it = m_listBB.begin(); it != m_listBB.end(); it++) {
        std::vector<PBB>& inEdges = (*it)->getInEdges();
        for (unsigned int j = 0; j < inEdges.size(); j++) {
            of << "       " << "bb" << std::hex << (*it)->getLowAddr() << " -> ";
            of << "bb" << std::hex << inEdges[j]->getLowAddr();
            of << " [color = \"green\"];\n";
        }
    }
#endif
}



////////////////////////////////////
//            Liveness             //
////////////////////////////////////

void updateWorkListRev(PBB currBB, std::list<PBB>&workList, std::set<PBB>& workSet) {
    // Insert inedges of currBB into the worklist, unless already there
    for ( BasicBlock * currIn : currBB->getInEdges() ) {
        if (workSet.find(currIn) == workSet.end()) {
            workList.push_front(currIn);
            workSet.insert(currIn);
        }
    }
}

static int progress = 0;
void Cfg::findInterferences(ConnectionGraph& cg) {
    if (m_listBB.size() == 0)
        return;

    std::list<PBB> workList;            // List of BBs still to be processed
    // Set of the same; used for quick membership test
    std::set<PBB> workSet;
    appendBBs(workList, workSet);

    bool change;
    int count = 0;
    while (workList.size() && count < 100000) {
        count++;  // prevent infinite loop
        if (++progress > 20) {
            std::cout << "i" << std::flush;
            progress = 0;
        }
        PBB currBB = workList.back();
        workList.erase(--workList.end());
        workSet.erase(currBB);
        // Calculate live locations and interferences
        change = currBB->calcLiveness(cg, myProc);
        if (!change)
            continue;
        if (DEBUG_LIVENESS) {
            LOG << "Revisiting BB ending with stmt ";
            Statement* last = NULL;
            if (currBB->m_pRtls->size()) {
                RTL* lastRtl = currBB->m_pRtls->back();
                std::list<Statement*>& lst = lastRtl->getList();
                if (lst.size())
                    last = lst.back();
            }
            if (last)
                LOG << last->getNumber();
            else
                LOG << "<none>";
            LOG << " due to change\n";
        }
        updateWorkListRev(currBB, workList, workSet);
    }
}

void Cfg::appendBBs(std::list<PBB>& worklist, std::set<PBB>& workset) {
    // Append my list of BBs to the worklist
    worklist.insert(worklist.end(), m_listBB.begin(), m_listBB.end());
    // Do the same for the workset
    std::copy(m_listBB.begin(),m_listBB.end(),std::inserter(workset,workset.end()));
}

void dumpBB(PBB bb) {
    std::cerr << "For BB at " << std::hex << bb << ":\nIn edges: ";
    int i, n;
    std::vector<PBB> ins = bb->getInEdges();
    std::vector<PBB> outs = bb->getOutEdges();
    n = ins.size();
    for (i=0; i < n; i++)
        std::cerr << ins[i] << " ";
    std::cerr << "\nOut Edges: ";
    n = outs.size();
    for (i=0; i < n; i++)
        std::cerr << outs[i] << " ";
    std::cerr << "\n";
}

/*    pBB-> +----+    +----+ <-pBB
 *   Change | A  | to | A  | where A and B could be empty. S is the string
 *          |    |    |    | instruction (with will branch to itself and to the
 *          +----+    +----+ start of the next instruction, i.e. the start of B,
 *          | S  |      |       if B is non empty).
 *          +----+      V
 *          | B  |    +----+ <-skipBB
 *          |    |    +-b1-+              b1 is just a branch for the skip part
 *          +----+      |
 *                      V
 *                    +----+ <-rptBB
 *                    | S' |              S' = S less the skip and repeat parts
 *                    +-b2-+              b2 is a branch for the repeat part
 *                      |
 *                      V
 *                    +----+ <-newBb
 *                    | B  |
 *                    |    |
 *                    +----+
 * S is an RTL with 6 statements representing one string instruction (so this function is highly specialised for the job
 * of replacing the %SKIP and %RPT parts of string instructions)
 */
/**
 * Split the given BB at the RTL given, and turn it into the BranchStatement given. Sort out all the in and out
 * edges.
 */
PBB Cfg::splitForBranch(PBB pBB, RTL* rtl, BranchStatement* br1, BranchStatement* br2, BB_IT& it) {
#if 0
    std::cerr << "splitForBranch before:\n";
    std::cerr << pBB->prints() << "\n";
#endif

    unsigned i, j;
    std::list<RTL*>::iterator ri;
    // First find which RTL has the split address
    for (ri = pBB->m_pRtls->begin(); ri != pBB->m_pRtls->end(); ri++) {
        if ((*ri) == rtl)
            break;
    }
    assert(ri != pBB->m_pRtls->end());

    bool haveA = (ri != pBB->m_pRtls->begin());

    ADDRESS addr = rtl->getAddress();

    // Make a BB for the br1 instruction

    // Don't give this "instruction" the same address as the rest of the string instruction (causes problems when
    // creating the rptBB). Or if there is no A, temporarily use 0
    ADDRESS a = (haveA) ? addr : ADDRESS::g(0L);
    RTL* skipRtl = new RTL(a, new std::list<Statement*> { br1 }); // list initializer in braces
    BasicBlock * skipBB = newBB(new std::list<RTL*> { skipRtl }, TWOWAY, 2);
    rtl->updateAddress(addr+1);
    if (!haveA) {
        skipRtl->updateAddress(addr);
        // Address addr now refers to the splitBB
        m_mapBB[addr] = skipBB;
        // Fix all predecessors of pBB to point to splitBB instead
        for (unsigned i=0; i < pBB->m_InEdges.size(); i++) {
            PBB pred = pBB->m_InEdges[i];
            for (unsigned j=0; j < pred->m_OutEdges.size(); j++) {
                PBB succ = pred->m_OutEdges[j];
                if (succ == pBB) {
                    pred->m_OutEdges[j] = skipBB;
                    skipBB->addInEdge(pred);
                    break;
                }
            }
        }
    }

    // Remove the SKIP from the start of the string instruction RTL
    std::list<Statement*>& li = rtl->getList();
    assert(li.size() >= 4);
    li.erase(li.begin());
    // Replace the last statement with br2
    std::list<Statement*>::iterator ll = --li.end();
    li.erase(ll);
    li.push_back(br2);

    // Move the remainder of the string RTL into a new BB
    PBB rptBB = newBB(new std::list<RTL*> { *ri }, TWOWAY, 2);
    ri = pBB->m_pRtls->erase(ri);

    // Move the remaining RTLs (if any) to a new list of RTLs
    PBB newBb;
    unsigned oldOutEdges = 0;
    bool haveB = true;
    if (ri != pBB->m_pRtls->end()) {
        auto pRtls = new std::list<RTL*>;
        while (ri != pBB->m_pRtls->end()) {
            pRtls->push_back(*ri);
            ri = pBB->m_pRtls->erase(ri);
        }
        oldOutEdges = pBB->getNumOutEdges();
        newBb = newBB(pRtls, pBB->getType(), oldOutEdges);
        // Transfer the out edges from A to B (pBB to newBb)
        for (i=0; i < oldOutEdges; i++)
            // Don't use addOutEdge, since it will also add in-edges back to pBB
            newBb->m_OutEdges.push_back(pBB->getOutEdge(i));
        //addOutEdge(newBb, pBB->getOutEdge(i));
    } else {
        // The "B" part of the above diagram is empty.
        // Don't create a new BB; just point newBB to the successor of pBB
        haveB = false;
        newBb = pBB->getOutEdge(0);
    }

    // Change pBB to a FALL bb
    pBB->updateType(FALL, 1);
    // Set the first out-edge to be skipBB
    pBB->m_OutEdges.erase(pBB->m_OutEdges.begin(), pBB->m_OutEdges.end());
    addOutEdge(pBB, skipBB);
    // Set the out edges for skipBB. First is the taken (true) leg.
    addOutEdge(skipBB, newBb);
    addOutEdge(skipBB, rptBB);
    // Set the out edges for the rptBB
    addOutEdge(rptBB, skipBB);
    addOutEdge(rptBB, newBb);

    // For each out edge of newBb, change any in-edges from pBB to instead come from newBb
    if (haveB) {
        for (i=0; i < oldOutEdges; i++) {
            PBB succ = newBb->m_OutEdges[i];
            for (j=0; j < succ->m_InEdges.size(); j++) {
                PBB pred = succ->m_InEdges[j];
                if (pred == pBB) {
                    succ->m_InEdges[j] = newBb;
                    break;
                }
            }
        }
    } else {
        // There is no "B" bb (newBb is just the successor of pBB) Fix that one out-edge to point to rptBB
        for (j=0; j < newBb->m_InEdges.size(); j++) {
            PBB pred = newBb->m_InEdges[j];
            if (pred == pBB) {
                newBb->m_InEdges[j] = rptBB;
                break;
            }
        }
    }
    if (!haveA) {
        // There is no A any more. All A's in-edges have been copied to the skipBB. It is possible that the original BB
        // had a self edge (branch to start of self). If so, this edge, now in to skipBB, must now come from newBb (if
        // there is a B) or rptBB if none.  Both of these will already exist, so delete it.
        for (j=0; j < skipBB->m_InEdges.size(); j++) {
            PBB pred = skipBB->m_InEdges[j];
            if (pred == pBB) {
                skipBB->deleteInEdge(pBB);
                break;
            }
        }

#if DEBUG_SPLIT_FOR_BRANCH
        std::cerr << "About to delete pBB: " << std::hex << pBB << "\n";
        dumpBB(pBB);
        dumpBB(skipBB);
        dumpBB(rptBB);
        dumpBB(newBb);
#endif

        // Must delete pBB. Note that this effectively "increments" iterator it
        it = m_listBB.erase(it);
        pBB = NULL;
    } else
        it++;

#if 0
    std::cerr << "splitForBranch after:\n";
    if (pBB) std::cerr << pBB->prints(); else std::cerr << "<null>\n";
    std::cerr << skipBB->prints();
    std::cerr << rptBB->prints();
    std::cerr << newBb->prints() << "\n";
#endif
    return newBb;
}

/**
 * \brief Check for indirect jumps and calls. If any found, decode the extra code and return true
 */
bool Cfg::decodeIndirectJmp(UserProc* proc) {
    bool res = false;
    for (BasicBlock * bb : m_listBB ) {
        res |= bb->decodeIndirectJmp(proc);
    }
    return res;
}
/**
 * \brief Change the BB enclosing stmt to be CALL, not COMPCALL
 */
void Cfg::undoComputedBB(Statement* stmt) {
    for (BasicBlock * bb : m_listBB) {
        if (bb->undoComputedBB(stmt))
            break;
    }
}
//! Find or create an implicit assign for x
Statement* Cfg::findImplicitAssign(Exp* x) {
    Statement* def;
    std::map<Exp*, Statement*, lessExpStar>::iterator it = implicitMap.find(x);
    if (it == implicitMap.end()) {
        // A use with no explicit definition. Create a new implicit assignment
        x = x->clone();                // In case the original gets changed
        def = new ImplicitAssign(x);
        entryBB->prependStmt(def, myProc);
        // Remember it for later so we don't insert more than one implicit assignment for any one location
        // We don't clone the copy in the map. So if the location is a m[...], the same type information is available in
        // the definition as at all uses
        implicitMap[x] = def;
    } else {
        // Use an existing implicit assignment
        def = it->second;
    }
    return def;
}
//! Find the existing implicit assign for x (if any)
Statement* Cfg::findTheImplicitAssign(Exp* x) {
    // As per the above, but don't create an implicit if it doesn't already exist
    auto it = implicitMap.find(x);
    if (it == implicitMap.end())
        return nullptr;
    return it->second;
}
//! Find exiting implicit assign for parameter p
Statement* Cfg::findImplicitParamAssign(Parameter* param) {
    // As per the above, but for parameters (signatures don't get updated with opParams)
    auto it = implicitMap.find(param->getExp());
    if (it == implicitMap.end()) {
        Exp* eParam = Location::param(param->getName());
        it = implicitMap.find(eParam);
    }
    if (it == implicitMap.end())
        return nullptr;
    return it->second;
}
//! Remove an existing implicit assignment for x
void Cfg::removeImplicitAssign(Exp* x) {
    auto it = implicitMap.find(x);
    assert(it != implicitMap.end());
    Statement* ia = it->second;
    implicitMap.erase(it);                // Delete the mapping
    myProc->removeStatement(ia);        // Remove the actual implicit assignment statement as well

}
