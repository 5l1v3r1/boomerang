#pragma region License
/*
 * This file is part of the Boomerang Decompiler.
 *
 * See the file "LICENSE.TERMS" for information on usage and
 * redistribution of this file, and for a DISCLAIMER OF ALL
 * WARRANTIES.
 */
#pragma endregion License
#include "RTL.h"


#include "boomerang/core/Boomerang.h"
#include "boomerang/db/statements/BranchStatement.h"
#include "boomerang/db/statements/Assign.h"
#include "boomerang/db/exp/Operator.h"        // for OPER::opIntConst

#include "boomerang/util/Log.h"               // for LOG_VERBOSE
#include "boomerang/util/Types.h"             // for Address

#include <QString>
#include <QTextStream>

#include <stdio.h>                      // for stderr
#include <cassert>                      // for assert
#include <cstring>                      // for strncpy


RTL::RTL(Address instrAddr, const std::list<Statement *> *listStmt /*= nullptr*/)
    : m_nativeAddr(instrAddr)
{
    if (listStmt) {
        m_stmts = *listStmt;
    }
}


RTL::RTL(Address instrAddr, const std::initializer_list<Statement *>& statements)
    : m_stmts(statements)
    , m_nativeAddr(instrAddr)
{
}


RTL::RTL(const RTL& other)
    : m_nativeAddr(other.m_nativeAddr)
{
    append(other.m_stmts);
}


RTL::~RTL()
{
    qDeleteAll(m_stmts);
}


RTL& RTL::operator=(const RTL& other)
{
    if (this == &other) {
        return *this;
    }

    m_nativeAddr = other.m_nativeAddr;

    // Do a deep copy always
    qDeleteAll(*this);
    clear();

    other.deepCopyList(m_stmts);
    return *this;
}


void RTL::deepCopyList(std::list<Statement *>& dest) const
{
    for (const Statement *it : *this) {
        dest.push_back(it->clone());
    }
}


void RTL::append(Statement *s)
{
    assert(s != nullptr);

    if (!empty() && back()->isFlagAssign()) {
        insert(std::prev(end()), s);
        return;
    }

    m_stmts.push_back(s);
}


void RTL::append(const std::list<Statement *>& stmts)
{
    for (Statement *stmt : stmts) {
        m_stmts.push_back(stmt->clone());
    }
}


void RTL::print(QTextStream& os, bool html) const
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
    bool firstStmt = true;

    for (Statement *stmt : *this) {
        if (html) {
            if (!firstStmt) {
                os << "<tr><td></td>";
            }

            os << "<td width=\"50\" align=\"center\">";
        }
        else {
            if (firstStmt) {
                os << " ";
            }
            else {
                os << qSetFieldWidth(11) << " " << qSetFieldWidth(0);
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
        firstStmt = false;
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


char *RTL::prints() const
{
    QString     tgt;
    QTextStream ost(&tgt);

    print(ost);
    strncpy(debug_buffer, qPrintable(tgt), DEBUG_BUFSIZE - 1);
    debug_buffer[DEBUG_BUFSIZE - 1] = '\0';
    return debug_buffer;
}


/**
 * Output operator for RTL *, just makes it easier to use e.g. LOG_STREAM() << myRTLptr
 * \param   os output stream to send to
 * \param   r ptr to RTL to print to the stream
 * \returns os (for concatenation)
 */
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
    for (iterator it = begin(); it != end();) {
        Statement *s = *it;
        s->simplify();

        if (s->isBranch()) {
            SharedExp cond = static_cast<BranchStatement *>(s)->getCondExpr();

            if (cond && (cond->getOper() == opIntConst)) {
                if (cond->access<Const>()->getInt() == 0) {
                    LOG_VERBOSE("Removing branch with false condition at %1 %2", getAddress(), *it);
                    it = this->erase(it);
                    continue;
                }

                LOG_VERBOSE("Replacing branch with true condition with goto at %1 %2",
                            getAddress(), *it);
                BasicBlock *bb = (*it)->getBB();
                *it = new GotoStatement(static_cast<BranchStatement *>(s)->getFixedDest());
                (*it)->setBB(bb);
            }
        }
        else if (s->isAssign()) {
            SharedExp guard = static_cast<Assign *>(s)->getGuard();

            if (guard && (guard->isFalse() || (guard->isIntConst() && (guard->access<Const>()->getInt() == 0)))) {
                // This assignment statement can be deleted
                LOG_VERBOSE("Removing assignment with false guard at %1 %2", getAddress(), *it);
                it = erase(it);
                continue;
            }
        }

        ++it;
    }
}


bool RTL::isCall() const
{
    if (empty()) {
        return false;
    }

    Statement *last = this->back();
    return last->getKind() == StmtType::Call;
}


Statement *RTL::getHlStmt() const
{
    for (auto rit = rbegin(); rit != rend(); ++rit) {
        if ((*rit)->getKind() != StmtType::Assign) {
            return *rit;
        }
    }

    return nullptr;
}
