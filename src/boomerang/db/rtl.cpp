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

/***************************************************************************/ /**
 * \file       rtl.cpp
 * \brief   Implementation of the classes that describe a low level RTL (
 *               register transfer list)
 ******************************************************************************/


#include "boomerang/db/rtl.h"

#include "boomerang/include/operator.h"          // for OPER::opIntConst

#include "boomerang/db/exp.h"                    // for Const, Exp, DEBUG_BUFSIZE
#include "boomerang/db/statements/branchstatement.h"
#include "boomerang/db/statements/assign.h"

#include "boomerang/util/Log.h"                  // for LOG_VERBOSE
#include "boomerang/util/types.h"             // for Address

#include <QString>
#include <QTextStream>

#include <stdio.h>                      // for stderr
#include <cassert>                      // for assert
#include <cstring>                      // for strncpy

RTL::RTL()
    : m_nativeAddr(Address::ZERO)
{
}


RTL::RTL(Address instNativeAddr, const std::list<Instruction *> *listStmt /*= nullptr*/)
    : m_nativeAddr(instNativeAddr)
{
    if (listStmt) {
        *(std::list<Instruction *> *) this = *listStmt;
    }
}


RTL::RTL(const RTL& other)
    : std::list<Instruction *>()
    , m_nativeAddr(other.m_nativeAddr)
{
    for (auto const& elem : other) {
        push_back(elem->clone());
    }
}


RTL::~RTL()
{
    qDeleteAll(*this);
}


RTL& RTL::operator=(const RTL& other)
{
    if (this != &other) {
        qDeleteAll(*this);
        // Do a deep copy always
        clear();
        const_iterator it;

        for (it = other.begin(); it != other.end(); it++) {
            push_back((*it)->clone());
        }

        m_nativeAddr = other.m_nativeAddr;
    }

    return *this;
}


RTL *RTL::clone() const
{
    std::list<Instruction *> le;

    for (auto const& elem : *this) {
        le.push_back((elem)->clone());
    }

    return new RTL(m_nativeAddr, &le);
}


void RTL::deepCopyList(std::list<Instruction *>& dest) const
{
    for (Instruction *it : *this) {
        dest.push_back(it->clone());
    }
}


void RTL::appendStmt(Instruction *s)
{
    assert(s != nullptr);

    if (not empty()) {
        if (back()->isFlagAssgn()) {
            iterator it = end();
            insert(--it, s);
            return;
        }
    }

    push_back(s);
}


void RTL::appendListStmt(std::list<Instruction *>& le)
{
    for (Instruction *it : le) {
        push_back(it->clone());
    }
}


void RTL::print(QTextStream& os /*= cout*/, bool html /*=false*/) const
{
    if (html) {
        os << "<tr><td>";
    }

    // print out the instruction address of this RTL
    os << m_nativeAddr;

    if (html) {
        os << "</td>";
    }

    // Print the statements
    // First line has 8 extra chars as above
    bool bFirst = true;

    for (Instruction *stmt : *this) {
        if (html) {
            if (!bFirst) {
                os << "<tr><td></td>";
            }

            os << "<td width=\"50\" align=\"center\">";
        }
        else {
            if (bFirst) {
                os << " ";
            }
            else {
                os << qSetFieldWidth(9) << " " << qSetFieldWidth(0);
            }
        }

        if (stmt) {
            stmt->print(os, html);
        }

        // Note: we only put newlines where needed. So none at the end of
        // Statement::print; one here to separate from other statements
        if (html) {
            os << "</td></tr>";
        }

        os << "\n";
        bFirst = false;
    }

    if (empty()) {
        os << '\n'; // New line for NOP
    }
}


void RTL::dump() const
{
    QTextStream q_cerr(stderr);

    print(q_cerr);
}


extern char debug_buffer[];

char *RTL::prints() const
{
    QString     tgt;
    QTextStream ost(&tgt);

    print(ost);
    strncpy(debug_buffer, qPrintable(tgt), DEBUG_BUFSIZE - 1);
    debug_buffer[DEBUG_BUFSIZE - 1] = '\0';
    return debug_buffer;
}


/***************************************************************************/ /**
 * \brief   Output operator for RTL*
 *          Just makes it easier to use e.g. LOG_STREAM() << myRTLptr
 * \param   os output stream to send to
 * \param   r ptr to RTL to print to the stream
 * \returns os (for concatenation)
 ******************************************************************************/
QTextStream& operator<<(QTextStream& os, const RTL *r)
{
    if (r == nullptr) {
        os << "nullptr ";
        return os;
    }

    r->print(os);
    return os;
}


void RTL::simplify()
{
    for (iterator it = begin(); it != end(); ) {
        Instruction *s = *it;
        s->simplify();

        if (s->isBranch()) {
            SharedExp cond = ((BranchStatement *)s)->getCondExpr();

            if (cond && (cond->getOper() == opIntConst)) {
                if (cond->access<Const>()->getInt() == 0) {
                    LOG_VERBOSE(1) << "removing branch with false condition at " << getAddress() << " " << *it << "\n";
                    it = this->erase(it);
                    continue;
                }

                LOG_VERBOSE(1) << "replacing branch with true condition with goto at " << getAddress() << " " << *it
                               << "\n";
                *it = new GotoStatement(((BranchStatement *)s)->getFixedDest());
            }
        }
        else if (s->isAssign()) {
            SharedExp guard = ((Assign *)s)->getGuard();

            if (guard && (guard->isFalse() || (guard->isIntConst() && (guard->access<Const>()->getInt() == 0)))) {
                // This assignment statement can be deleted
                LOG_VERBOSE(1) << "removing assignment with false guard at " << getAddress() << " " << *it << "\n";
                it = erase(it);
                continue;
            }
        }

        it++;
    }
}


bool RTL::isCall() const
{
    if (empty()) {
        return false;
    }

    Instruction *last = this->back();
    return last->getKind() == STMT_CALL;
}


Instruction *RTL::getHlStmt() const
{
    for (auto rit = this->rbegin(); rit != this->rend(); rit++) {
        if ((*rit)->getKind() != STMT_ASSIGN) {
            return *rit;
        }
    }

    return nullptr;
}
