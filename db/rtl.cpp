/*
 * Copyright (C) 1998-2001, The University of Queensland
 * Copyright (C) 2000-2001, Sun Microsystems, Inc
 * Copyright (C) 2002, Trent Waddington
 *
 * See the file "LICENSE.TERMS" for information on usage and
 * redistribution of this file, and for a DISCLAIMER OF ALL
 * WARRANTIES.
 *
 */

/***************************************************************************//**
 * \file       rtl.cc
 * \brief   Implementation of the classes that describe a low level RTL (
 *               register transfer list)
 ******************************************************************************/

/*
 * $Revision$    // 1.33.2.3
 *
 * 08 Apr 02 - Mike: Changes for boomerang
 * 13 May 02 - Mike: expList is no longer a pointer
 * 15 May 02 - Mike: Fixed a nasty bug in updateExp (when update with same
 *                expression as existing)
 * 25 Jul 02 - Mike: RTL is now list of Statements
 */

#include <cassert>
#if defined(_MSC_VER) && _MSC_VER <= 1200
#pragma warning(disable:4786)
#endif

#include <iomanip>            // For setfill
#include <sstream>
#include <cstring>
#include "types.h"
#include "statement.h"
#include "exp.h"
#include "type.h"
#include "register.h"
#include "cfg.h"
#include "proc.h"            // For printing proc names
#include "rtl.h"
#include "prog.h"
#include "hllcode.h"
#include "util.h"
#include "boomerang.h"
#include "visitor.h"
#include "log.h"
// For some reason, MSVC 5.00 complains about use of undefined types a lot
#if defined(_MSC_VER) && _MSC_VER <= 1100
#include "signature.h"        // For MSVC 5.00
#endif

/******************************************************************************
 * RTL methods.
 * Class RTL represents low-level register transfer lists.
 *****************************************************************************/

/***************************************************************************//**
 * FUNCTION:        RTL::RTL
 * \brief        Constructor.
 * PARAMETERS:        <none>
 * \returns             N/a
 ******************************************************************************/
RTL::RTL()
  : nativeAddr(ADDRESS::g(0L))
{ }

/***************************************************************************//**
 * FUNCTION:        RTL::RTL
 * \brief        Constructor.
 * PARAMETERS:        instNativeAddr - the native address of the instruction
 *                    listExp - ptr to existing list of Exps
 * \returns             N/a
 ******************************************************************************/
RTL::RTL(ADDRESS instNativeAddr, std::list<Statement*>* listStmt /*= nullptr*/)
    : nativeAddr(instNativeAddr) {
    if (listStmt)
        stmtList = *listStmt;
}

/***************************************************************************//**
 * FUNCTION:        RTL::RTL
 * \brief        Copy constructor. A deep clone is made of the given object
 *                    so that the lists of Exps do not share memory.
 * PARAMETERS:        other: RTL to copy from
 * \returns             N/a
 ******************************************************************************/
RTL::RTL(const RTL& other) : nativeAddr(other.nativeAddr) {
    std::list<Statement*>::const_iterator it;
    for (it = other.stmtList.begin(); it != other.stmtList.end(); it++) {
        stmtList.push_back((*it)->clone());
    }
}

/***************************************************************************//**
 * FUNCTION:        RTL::~RTL
 * \brief        Destructor.
 * PARAMETERS:        <none>
 * \returns             N/a
 ******************************************************************************/
RTL::~RTL() { }

/***************************************************************************//**
 * FUNCTION:        RTL::operator=
 * \brief        Assignment copy (deep).
 * PARAMETERS:        other - RTL to copy
 * \returns             a reference to this object
 ******************************************************************************/
RTL& RTL::operator=(RTL& other) {
    if (this != &other) {
        // Do a deep copy always
        iterator it;
        for (it = other.stmtList.begin(); it != other.stmtList.end(); it++)
            stmtList.push_back((*it)->clone());

        nativeAddr = other.nativeAddr;
    }
    return *this;
}

/***************************************************************************//**
 * FUNCTION:        RTL:clone
 * \brief        Deep copy clone; deleting the clone will not affect this
 *                     RTL object
 * PARAMETERS:        <none>
 * \returns             Pointer to a new RTL that is a clone of this one
 ******************************************************************************/
RTL* RTL::clone() {
    std::list<Statement*> le;
    iterator it;

    for (it = stmtList.begin(); it != stmtList.end(); it++) {
        le.push_back((*it)->clone());
    }

    RTL* ret = new RTL(nativeAddr, &le);
    return ret;
}

// visit this RTL, and all its Statements
bool RTL::accept(StmtVisitor* visitor) {
    // Might want to do something at the RTL level:
    if (!visitor->visit(this)) return false;
    iterator it;
    for (it = stmtList.begin(); it != stmtList.end(); it++) {
        if (! (*it)->accept(visitor)) return false;
    }
    return true;
}

/***************************************************************************//**
 * FUNCTION:        RTL::deepCopyList
 * \brief        Make a copy of this RTLs list of Exp* to the given list
 * PARAMETERS:        Ref to empty list to copy to
 * \returns             Nothing
 ******************************************************************************/
void RTL::deepCopyList(std::list<Statement*>& dest) {
    std::list<Statement*>::iterator it;

    for (it = stmtList.begin(); it != stmtList.end(); it++) {
        dest.push_back((*it)->clone());
    }
}

/***************************************************************************//**
 * FUNCTION:        RTL::appendStmt
 * \brief        Append the given Statement at the end of this RTL
 * NOTE:            Exception: Leaves any flag call at the end (so may push exp
 *                     to second last position, instead of last)
 * NOTE:            stmt is NOT copied. This is different to how UQBT was!
 * PARAMETERS:        s: pointer to Statement to append
 * \returns             Nothing
 ******************************************************************************/
void RTL::appendStmt(Statement* s) {
    if (stmtList.size()) {
        if (stmtList.back()->isFlagAssgn()) {
            iterator it = stmtList.end();
            stmtList.insert(--it, s);
            return;
        }
    }
    stmtList.push_back(s);
}

/***************************************************************************//**
 * FUNCTION:        RTL::prependStmt
 * \brief        Prepend the given Statement at the start of this RTL
 * NOTE:            No clone of the statement is made. This is different to how UQBT was
 * PARAMETERS:        s: Ptr to Statement to prepend
 * \returns             Nothing
 ******************************************************************************/
void RTL::prependStmt(Statement* s) {
    stmtList.push_front(s);
}

/***************************************************************************//**
 * FUNCTION:        RTL::appendListStmt
 * \brief        Append a given list of Statements to this RTL
 * NOTE:            A copy of the Statements in le are appended
 * PARAMETERS:        rtl: list of Exps to insert
 * \returns             Nothing
 ******************************************************************************/
void RTL::appendListStmt(std::list<Statement*>& le) {
    iterator it;
    for (it = le.begin();  it != le.end();    it++) {
        stmtList.insert(stmtList.end(), (*it)->clone());
    }
}

/***************************************************************************//**
 * FUNCTION:        RTL::appendRTL
 * \brief        Append the Statemens of another RTL to this object
 * NOTE:            A copy of the Statements in r are appended
 * PARAMETERS:        r: reterence to RTL whose Exps we are to insert
 * \returns             Nothing
 ******************************************************************************/
void RTL::appendRTL(RTL& r) {
    appendListStmt(r.stmtList);
}

/***************************************************************************//**
 * FUNCTION:        RTL::insertStmt
 * \brief        Insert the given Statement before index i
 * NOTE:            No copy of stmt is made. This is different to UQBT
 * PARAMETERS:        s: pointer to the Statement to insert
 *                    i: position to insert before (0 = first)
 * \returns             Nothing
 ******************************************************************************/
void RTL::insertStmt(Statement* s, unsigned i) {
    // Check that position i is not out of bounds
    assert (i < stmtList.size() || stmtList.size() == 0);

    // Find the position
    iterator pp = stmtList.begin();
    for (; i > 0; i--, pp++);

    // Do the insertion
    stmtList.insert(pp, s);
}

void RTL::insertStmt(Statement* s, iterator it) {
    stmtList.insert(it, s);
}

/***************************************************************************//**
 * FUNCTION:        RTL::updateStmt
 * \brief        Replace the ith Statement with the given one
 * PARAMETERS:        s: pointer to the new Exp
 *                    i: index of Exp position (0 = first)
 * \returns             Nothing
 ******************************************************************************/
void RTL::updateStmt(Statement *s, unsigned i) {
    // Check that position i is not out of bounds
    assert (i < stmtList.size());

    // Find the position
    iterator pp = stmtList.begin();
    for (; i > 0; i--, pp++);

    // Note that sometimes we might update even when we don't know if it's
    // needed, e.g. after a searchReplace.
    // In that case, don't update, and especially don't delete the existing
    // statement (because it's also the one we are updating!)
    if (*pp != s) {
        // Do the update
        if (*pp) ;//delete *pp;
        *pp = s;
    }
}

void RTL::deleteStmt(unsigned i) {
    // check that position i is not out of bounds
    assert (i < stmtList.size());

    // find the position
    iterator pp = stmtList.begin();
    for (; i > 0; i--, pp++);

    // do the delete
    stmtList.erase(pp);
}

void RTL::deleteLastStmt() {
    assert(stmtList.size());
    stmtList.erase(--stmtList.end());
}

void RTL::replaceLastStmt(Statement* repl) {
    assert(stmtList.size());
    Statement*& last = stmtList.back();
    last = repl;
}


/***************************************************************************//**
 * FUNCTION:        RTL::getNumStmt
 * \brief        Get the number of Statements in this RTL
 * PARAMETERS:        None
 * \returns             Integer number of Statements
 ******************************************************************************/
int RTL::getNumStmt() {
    return stmtList.size();
}

/***************************************************************************//**
 * FUNCTION:        RTL::at
 * \brief        Provides indexing on a list. Changed from operator[] so that
 *                    we keep in mind it is linear in its execution time.
 * PARAMETERS:        i - the index of the element we want (0 = first)
 * \returns             the element at the given index or nullptr if the index is out
 *                    of bounds
 ******************************************************************************/
Statement* RTL::elementAt(unsigned i) {
    iterator it;
    for (it = stmtList.begin();     i > 0 && it != stmtList.end();     i--, it++);
    if (it == stmtList.end()) {
        return nullptr;
    }
    return *it;
}

/***************************************************************************//**
 * FUNCTION:        RTL::print
 * \brief        Prints this object to a stream in text form.
 * PARAMETERS:        os - stream to output to (often cout or cerr)
 * \returns             <nothing>
 ******************************************************************************/
void RTL::print(std::ostream& os /*= cout*/, bool html /*=false*/) {

    if (html)
        os << "<tr><td>";
    // print out the instruction address of this RTL
    os << std::hex << std::setfill('0') << std::setw(8) << nativeAddr;
    os << std::dec << std::setfill(' ');      // Ugh - why is this needed?
    if (html)
        os << "</td>";

    // Print the statements
    // First line has 8 extra chars as above
    bool bFirst = true;
    iterator ss;
    for (ss = stmtList.begin(); ss != stmtList.end(); ss++) {
        Statement* stmt = *ss;
        if (html) {
            if (!bFirst) os << "<tr><td></td>";
            os << "<td width=\"50\" align=\"center\">";
        } else {
            if (bFirst) os << " ";
            else        os << std::setw(9) << " ";
        }
        if (stmt) stmt->print(os, html);
        // Note: we only put newlines where needed. So none at the end of
        // Statement::print; one here to separate from other statements
        if (html)
            os << "</td></tr>";
        os << "\n";
        bFirst = false;
    }
    if (stmtList.empty()) os << std::endl;       // New line for NOP
}

void RTL::dump() {
    print(std::cerr);
}

extern char debug_buffer[];

char* RTL::prints() {
    std::ostringstream ost;
    print(ost);
    strncpy(debug_buffer, ost.str().c_str(), DEBUG_BUFSIZE-1);
    debug_buffer[DEBUG_BUFSIZE-1] = '\0';
    return debug_buffer;
}

/***************************************************************************//**
 * \brief        Output operator for RTL*
 * Just makes it easier to use e.g. std::cerr << myRTLptr
 * \param os: output stream to send to
 *        r: ptr to RTL to print to the stream
 * \returns             copy of os (for concatenation)
 ******************************************************************************/
std::ostream& operator<<(std::ostream& os, RTL* r) {
    if (r == nullptr) {os << "nullptr "; return os;}
    r->print(os);
    return os;
}

/***************************************************************************//**
 * \brief Set the nativeAddr field
 * \param addr Native address
 * \returns             Nothing
 ******************************************************************************/
void RTL::updateAddress(ADDRESS addr) {
    nativeAddr = addr;
}

/***************************************************************************//**
 * \brief        Replace all instances of search with replace.
 * PARAMETERS:        search - ptr to an expression to search for
 *                    replace - ptr to the expression with which to replace it
 * \returns             <nothing>
 ******************************************************************************/
bool RTL::searchAndReplace(Exp* search, Exp* replace) {
    bool ch = false;
    for (iterator it = stmtList.begin(); it != stmtList.end(); it++)
        ch |= (*it)->searchAndReplace(search, replace);
    return ch;
}

/***************************************************************************//**
 * \brief        Find all instances of the search expression
 * \param search - a location to search for
 * \param result - a list which will have any matching exprs
 *                 appended to it
 * \returns true if there were any matches
 ******************************************************************************/
bool RTL::searchAll(Exp* search, std::list<Exp *> &result) {
    bool found = false;
    for (iterator it = stmtList.begin(); it != stmtList.end(); it++) {
        Statement *e = *it;
        Exp* res;
        if (e->search(search, res)) {
            found = true;
            result.push_back(res);
        }
    }
    return found;
}

/***************************************************************************//**
 * FUNCTION:        RTL::clear
 * \brief        Clear the list of Exps
 * PARAMETERS:        None
 * \returns             Nothing
 ******************************************************************************/
void RTL::clear() {
    stmtList.clear();
}

/***************************************************************************//**
 * FUNCTION:        RTL::insertAssign
 * \brief        Prepends or appends an assignment to the front or back of
 *                      this RTL
 * NOTE:            Is this really used? What about types?
 * ASSUMES:            Assumes that pLhs and pRhs are "new" Exp's that are
 *                    not part of other Exps. (Otherwise, there will be problems
 *                    when deleting this Exp)
 *                    If size == -1, assumes there is already at least one assign-
 *                      ment in this RTL
 * PARAMETERS:        pLhs: ptr to Exp to place on LHS
 *                    pRhs: ptr to Exp to place on the RHS
 *                    prep: true if prepend (else append)
 *                    type: type of the transfer, or nullptr
 * \returns             <nothing>
 ******************************************************************************/
void RTL::insertAssign(Exp* pLhs, Exp* pRhs, bool prep,
                       Type* type /*= nullptr */) {
    // Generate the assignment expression
    Assign* asgn = new Assign(type, pLhs, pRhs);
    if (prep)
        prependStmt(asgn);
    else
        appendStmt(asgn);
}

/***************************************************************************//**
 * FUNCTION:        RTL::insertAfterTemps
 * \brief        Inserts an assignment at or near the top of this RTL, after
 *                      any assignments to temporaries. If the last assignment
 *                      is to a temp, the insertion is done before that last
 *                      assignment
 * ASSUMES:            Assumes that ssLhs and ssRhs are "new" Exp's that are
 *                    not part of other Exps. (Otherwise, there will be problems
 *                    when deleting this Exp)
 *                    If type == nullptr, assumes there is already at least one
 *                      assignment in this RTL (?)
 * NOTE:            Hopefully this is only a temporary measure
 * PARAMETERS:        pLhs: ptr to Exp to place on LHS
 *                    pRhs: ptr to Exp to place on the RHS
 *                    size: size of the transfer, or -1 to be the same as the
 *                      first assign this RTL
 * \returns             <nothing>
 ******************************************************************************/
void RTL::insertAfterTemps(Exp* pLhs, Exp* pRhs, Type* type     /* nullptr */) {
    iterator it;
    // First skip all assignments with temps on LHS
    for (it = stmtList.begin(); it != stmtList.end(); it++) {
        Statement *s = *it;
        if (!s->isAssign())
            break;
        Exp* LHS = ((Assign*)s)->getLeft();
        if (LHS->isTemp())
            break;
    }

    // Now check if the next Stmt is an assignment
    if ((it == stmtList.end()) || !(*it)->isAssign()) {
        // There isn't an assignment following. Use the previous Exp to insert
        // before
        if (it != stmtList.begin())
            it--;
    }

    if (type == nullptr)
        type = getType();

    // Generate the assignment expression
    Assign* asgn = new Assign(type, pLhs, pRhs);

    // Insert before "it"
    stmtList.insert(it, asgn);
}

/***************************************************************************//**
 * FUNCTION:        RTL::getType
 * \brief        Get the "type" for this RTL. Just gets the type of
 *                      the first assignment Exp
 * NOTE:            The type of the first assign may not be the type that you
 *                      want!
 * PARAMETERS:        None
 * \returns             A pointer to the type
 ******************************************************************************/
Type* RTL::getType() {
    iterator it;
    for (it = stmtList.begin(); it != stmtList.end(); it++) {
        Statement *e = *it;
        if (e->isAssign())
            return ((Assign*)e)->getType();
    }
    return new IntegerType();    //    Default to 32 bit integer if no assignments
}

/***************************************************************************//**
 * \brief      Return true if this RTL affects the condition codes
 * \note          Assumes that if there is a flag call Exp, then it is the last
 * \returns           Boolean as above
 ******************************************************************************/
bool RTL::areFlagsAffected() {
    if (stmtList.size() == 0) return false;
    // Get an iterator to the last RT
    iterator it = stmtList.end();
    if (it == stmtList.begin())
        return false;            // No expressions at all
    it--;                        // Will now point to the end of the list
    Statement *e = *it;
    // If it is a flag call, then the CCs are affected
    return e->isFlagAssgn();
}

void RTL::generateCode(HLLCode *hll, BasicBlock *pbb, int indLevel) {
    for (iterator it = stmtList.begin(); it != stmtList.end(); it++) {
        (*it)->generateCode(hll, pbb, indLevel);
    }
}

void RTL::simplify() {
    for (iterator it = stmtList.begin(); it != stmtList.end(); /*it++*/) {
        Statement *s = *it;
        s->simplify();
        if (s->isBranch()) {
            Exp *cond =     ((BranchStatement*)s)->getCondExpr();
            if (cond && cond->getOper() == opIntConst) {
                if (((Const*)cond)->getInt() == 0) {
                    if (VERBOSE)
                        LOG << "removing branch with false condition at " << getAddress()  << " " << *it << "\n";
                    it = stmtList.erase(it);
                    continue;
                } else {
                    if (VERBOSE)
                        LOG << "replacing branch with true condition with goto at " << getAddress() << " " << *it <<
                               "\n";
                    *it = new GotoStatement(((BranchStatement*)s)->getFixedDest());
                }
            }
        } else if (s->isAssign()) {
            Exp* guard = ((Assign*)s)->getGuard();
            if (guard && (guard->isFalse() || (guard->isIntConst() && ((Const*)guard)->getInt() == 0))) {
                // This assignment statement can be deleted
                if (VERBOSE)
                    LOG << "removing assignment with false guard at " << getAddress() << " " << *it << "\n";
                it = stmtList.erase(it);
                continue;
            }
        }
        it++;
    }
}

/***************************************************************************//**
 * \brief        Return true if this is an unmodified compare instruction
 *                      of a register with an operand
 * \note            Will also match a subtract if the flags are set
 * \note            expOperand, if set, is not cloned
 * \note            Assumes that the first subtract on the RHS is the actual
 *                      compare operation
 * \param iReg: ref to integer to set with the register index
 * \param expOperand: ref to ptr to expression of operand
 * \returns True if found
 ******************************************************************************/
bool RTL::isCompare(int& iReg, Exp*& expOperand) {
    // Expect to see a subtract, then a setting of the flags
    // Dest of subtract should be a register (could be the always zero register)
    if (getNumStmt() < 2)
        return false;
    // Could be first some assignments to temporaries
    // But the actual compare could also be an assignment to a temporary
    // So we search for the first RHS with an opMinus, that has a LHS to
    // a register (whether a temporary or a machine register)
    int i=0;
    Exp* rhs;
    Statement* cur;
    do {
        cur = elementAt(i);
        if (cur->getKind() != STMT_ASSIGN)
            return false;
        rhs = ((Assign*)cur)->getRight();
        i++;
    } while (rhs->getOper() != opMinus && i < getNumStmt());
    if (rhs->getOper() != opMinus)
        return false;
    // We have a subtract assigning to a register.
    // Check if there is a subflags last
    Statement* last = elementAt(getNumStmt()-1);
    if (!last->isFlagAssgn())
        return false;
    Exp* sub = ((Binary*)rhs)->getSubExp1();
    // Should be a compare of a register and something (probably a constant)
    if (!sub->isRegOf())
        return false;
    // Set the register and operand expression, and return true
    iReg = ((Const*)((Unary*)sub)->getSubExp1())->getInt();
    expOperand = ((Binary*)rhs)->getSubExp2();
    return true;
}

bool RTL::isGoto() {
    if (stmtList.empty())
        return false;
    Statement* last = stmtList.back();
    return last->getKind() == STMT_GOTO;
}

bool RTL::isBranch() {
    if (stmtList.empty())
        return false;
    Statement* last = stmtList.back();
    return last->getKind() == STMT_BRANCH;
}

bool RTL::isCall() {
    if (stmtList.empty())
        return false;
    Statement* last = stmtList.back();
    return last->getKind() == STMT_CALL;
}

// Use this slow function when you can't be sure that the HL Statement is last
Statement* RTL::getHlStmt() {
    reverse_iterator rit;
    for (rit = stmtList.rbegin(); rit != stmtList.rend(); rit++) {
        if ((*rit)->getKind() != STMT_ASSIGN)
            return *rit;
    }
    return nullptr;
}

int RTL::setConscripts(int n, bool bClear) {
    StmtConscriptSetter ssc(n, bClear);
    accept(&ssc);
    return ssc.getLast();
}
