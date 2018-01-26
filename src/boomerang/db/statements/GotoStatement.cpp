#pragma region License
/*
 * This file is part of the Boomerang Decompiler.
 *
 * See the file "LICENSE.TERMS" for information on usage and
 * redistribution of this file, and for a DISCLAIMER OF ALL
 * WARRANTIES.
 */
#pragma endregion License
#include "GotoStatement.h"


#include "boomerang/core/Boomerang.h"
#include "boomerang/db/visitor/ExpVisitor.h"
#include "boomerang/db/visitor/StmtVisitor.h"
#include "boomerang/db/visitor/StmtExpVisitor.h"
#include "boomerang/db/visitor/StmtModifier.h"
#include "boomerang/db/visitor/StmtPartModifier.h"
#include "boomerang/util/Log.h"


GotoStatement::GotoStatement()
    : m_dest(nullptr)
    , m_isComputed(false)
{
    m_kind = StmtType::Goto;
}


GotoStatement::GotoStatement(Address uDest)
    : m_isComputed(false)
{
    m_kind = StmtType::Goto;
    m_dest = Const::get(uDest);
}


GotoStatement::~GotoStatement()
{
    if (m_dest) {
        // delete pDest;
    }
}


Address GotoStatement::getFixedDest() const
{
    if (m_dest->getOper() != opIntConst) {
        return Address::INVALID;
    }

    return constDest()->getAddr();
}


void GotoStatement::setDest(SharedExp pd)
{
    m_dest = pd;
}


void GotoStatement::setDest(Address addr)
{
    // This fails in FrontSparcTest, do you really want it to Mike? -trent
    //    assert(addr >= prog.limitTextLow && addr < prog.limitTextHigh);

    m_dest = Const::get(addr);
}


SharedExp GotoStatement::getDest()
{
    return m_dest;
}


const SharedExp GotoStatement::getDest() const
{
    return m_dest;
}


void GotoStatement::adjustFixedDest(int delta)
{
    // Ensure that the destination is fixed.
    if ((m_dest == nullptr) || (m_dest->getOper() != opIntConst)) {
        LOG_ERROR("Can't adjust destination of non-static CTI");
        return;
    }

    Address dest = constDest()->getAddr();
    constDest()->setAddr(dest + delta);
}


bool GotoStatement::search(const Exp& pattern, SharedExp& result) const
{
    result = nullptr;

    if (m_dest) {
        return m_dest->search(pattern, result);
    }

    return false;
}


bool GotoStatement::searchAndReplace(const Exp& pattern, SharedExp replace, bool /*cc*/)
{
    bool change = false;

    if (m_dest) {
        m_dest = m_dest->searchReplaceAll(pattern, replace, change);
    }

    return change;
}


bool GotoStatement::searchAll(const Exp& pattern, std::list<SharedExp>& result) const
{
    if (m_dest) {
        return m_dest->searchAll(pattern, result);
    }

    return false;
}


void GotoStatement::print(QTextStream& os, bool html) const
{
    os << qSetFieldWidth(4) << m_number << qSetFieldWidth(0) << " ";

    if (html) {
        os << "</td><td>";
        os << "<a name=\"stmt" << m_number << "\">";
    }

    os << "GOTO ";

    if (m_dest == nullptr) {
        os << "*no dest*";
    }
    else if (m_dest->getOper() != opIntConst) {
        m_dest->print(os);
    }
    else {
        os << getFixedDest();
    }

    if (html) {
        os << "</a></td>";
    }
}


void GotoStatement::setIsComputed(bool b)
{
    m_isComputed = b;
}


bool GotoStatement::isComputed() const
{
    return m_isComputed;
}


Statement *GotoStatement::clone() const
{
    GotoStatement *ret = new GotoStatement();

    ret->m_dest       = m_dest->clone();
    ret->m_isComputed = m_isComputed;
    // Statement members
    ret->m_bb = m_bb;
    ret->m_proc   = m_proc;
    ret->m_number = m_number;
    return ret;
}


bool GotoStatement::accept(StmtVisitor *visitor)
{
    return visitor->visit(this);
}


void GotoStatement::generateCode(ICodeGenerator *, const BasicBlock *)
{
    // don't generate any code for jumps, they will be handled by the BB
}


void GotoStatement::simplify()
{
    if (isComputed()) {
        m_dest = m_dest->simplifyArith();
        m_dest = m_dest->simplify();
    }
}


bool GotoStatement::usesExp(const Exp& e) const
{
    SharedExp where;

    return m_dest->search(e, where);
}


bool GotoStatement::accept(StmtExpVisitor *v)
{
    bool visitChildren = true;
    bool ret = v->visit(this, visitChildren);

    if (!visitChildren) {
        return ret;
    }

    if (ret && m_dest) {
        ret = m_dest->accept(v->ev);
    }

    return ret;
}


bool GotoStatement::accept(StmtModifier *v)
{
    bool visitChildren = true;
    v->visit(this, visitChildren);

    if (v->m_mod) {
        if (m_dest && visitChildren) {
            m_dest = m_dest->accept(v->m_mod);
        }
    }

    return true;
}


bool GotoStatement::accept(StmtPartModifier *v)
{
    bool visitChildren = true;
    v->visit(this, visitChildren);

    if (m_dest && visitChildren) {
        m_dest = m_dest->accept(v->mod);
    }

    return true;
}
