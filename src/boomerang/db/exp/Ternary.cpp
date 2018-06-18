#pragma region License
/*
 * This file is part of the Boomerang Decompiler.
 *
 * See the file "LICENSE.TERMS" for information on usage and
 * redistribution of this file, and for a DISCLAIMER OF ALL
 * WARRANTIES.
 */
#pragma endregion License
#include "Ternary.h"


#include "boomerang/core/Boomerang.h"
#include "boomerang/db/Prog.h"
#include "boomerang/db/exp/Const.h"
#include "boomerang/db/exp/Location.h"
#include "boomerang/db/proc/UserProc.h"
#include "boomerang/visitor/expvisitor/ExpVisitor.h"
#include "boomerang/visitor/expmodifier/ExpModifier.h"
#include "boomerang/type/type/FloatType.h"
#include "boomerang/type/type/IntegerType.h"
#include "boomerang/type/type/VoidType.h"
#include "boomerang/util/Log.h"


Ternary::Ternary(OPER _op, SharedExp _e1, SharedExp _e2, SharedExp _e3)
    : Binary(_op, _e1, _e2)
{
    subExp3 = _e3;
    assert(subExp1 && subExp2 && subExp3);
}


Ternary::Ternary(const Ternary& o)
    : Binary(o)
{
    subExp3 = o.subExp3->clone();
    assert(subExp1 && subExp2 && subExp3);
}


Ternary::~Ternary()
{
    if (subExp3 != nullptr) {
        // delete subExp3;
    }
}


void Ternary::setSubExp3(SharedExp e)
{
    if (subExp3 != nullptr) {
        // delete subExp3;
    }

    subExp3 = e;
    assert(subExp1 && subExp2 && subExp3);
}


SharedExp Ternary::getSubExp3()
{
    assert(subExp1 && subExp2 && subExp3);
    return subExp3;
}


SharedConstExp Ternary::getSubExp3() const
{
    assert(subExp1 && subExp2 && subExp3);
    return subExp3;
}


SharedExp& Ternary::refSubExp3()
{
    assert(subExp1 && subExp2 && subExp3);
    return subExp3;
}


SharedExp Ternary::clone() const
{
    assert(subExp1 && subExp2 && subExp3);
    std::shared_ptr<Ternary> c = std::make_shared<Ternary>(m_oper, subExp1->clone(), subExp2->clone(), subExp3->clone());
    return c;
}


bool Ternary::operator==(const Exp& o) const
{
    if (o.getOper() == opWild) {
        return true;
    }
    else if (nullptr == dynamic_cast<const Ternary *>(&o)) {
        return false;
    }

    const Ternary& otherTern = static_cast<const Ternary &>(o);

    return
        m_oper == otherTern.m_oper &&
        *subExp1 == *otherTern.getSubExp1() &&
        *subExp2 == *otherTern.getSubExp2() &&
        *subExp3 == *otherTern.getSubExp3();
}


bool Ternary::operator<(const Exp& o) const
{
    if (m_oper != o.getOper()) {
        return m_oper < o.getOper();
    }

    const Ternary& otherTern = static_cast<const Ternary&>(o);

    if (*subExp1 < *otherTern.getSubExp1()) {
        return true;
    }

    if (*otherTern.getSubExp1() < *subExp1) {
        return false;
    }

    if (*subExp2 < *otherTern.getSubExp2()) {
        return true;
    }

    if (*otherTern.getSubExp2() < *subExp2) {
        return false;
    }

    return *subExp3 < *otherTern.getSubExp3();
}


bool Ternary::operator*=(const Exp& o) const
{
    const Exp *other = &o;

    if (o.getOper() == opSubscript) {
        other = o.getSubExp1().get();
    }

    if (other->getOper() == opWild) {
        return true;
    }

    if (m_oper != other->getOper()) {
        return false;
    }

    if (!(*subExp1 *= *other->getSubExp1())) {
        return false;
    }

    if (!(*subExp2 *= *other->getSubExp2())) {
        return false;
    }

    return *subExp3 *= *other->getSubExp3();
}


void Ternary::printr(QTextStream& os, bool) const
{
    // The function-like operators don't need parentheses
    switch (m_oper)
    {
    // The "function-like" ternaries
    case opTruncu:
    case opTruncs:
    case opZfill:
    case opSgnEx:
    case opFsize:
    case opItof:
    case opFtoi:
    case opFround:
    case opFtrunc:
    case opOpTable:
        // No paren case
        print(os);
        return;

    default:
        break;
    }

    // All other cases, we use the parens
    os << "(" << this << ")";
}


void Ternary::print(QTextStream& os, bool html) const
{
    SharedConstExp p1 = this->getSubExp1();
    SharedConstExp p2 = this->getSubExp2();
    SharedConstExp p3 = this->getSubExp3();

    switch (m_oper)
    {
    // The "function-like" ternaries
    case opTruncu:
    case opTruncs:
    case opZfill:
    case opSgnEx:
    case opFsize:
    case opItof:
    case opFtoi:
    case opFround:
    case opFtrunc:
    case opOpTable:

        switch (m_oper)
        {
        case opTruncu:
            os << "truncu(";
            break;

        case opTruncs:
            os << "truncs(";
            break;

        case opZfill:
            os << "zfill(";
            break;

        case opSgnEx:
            os << "sgnex(";
            break;

        case opFsize:
            os << "fsize(";
            break;

        case opItof:
            os << "itof(";
            break;

        case opFtoi:
            os << "ftoi(";
            break;

        case opFround:
            os << "fround(";
            break;

        case opFtrunc:
            os << "ftrunc(";
            break;

        case opOpTable:
            os << "optable(";
            break;

        default:
            break; // For warning
        }

        // Use print not printr here, since , has the lowest precendence of all.
        // Also it makes it the same as UQBT, so it's easier to test
        if (p1) {
            p1->print(os, html);
        }
        else {
            os << "<nullptr>";
        }

        os << ",";

        if (p2) {
            p2->print(os, html);
        }
        else {
            os << "<nullptr>";
        }

        os << ",";

        if (p3) {
            p3->print(os, html);
        }
        else {
            os << "<nullptr>";
        }

        os << ")";
        return;

    default:
        break;
    }

    // Else must be ?: or @ (traditional ternary operators)
    if (p1) {
        p1->printr(os, html);
    }
    else {
        os << "<nullptr>";
    }

    if (m_oper == opTern) {
        os << " ? ";

        if (p2) {
            p2->printr(os, html);
        }
        else {
            os << "<nullptr>";
        }

        os << " : "; // Need wide spacing here

        if (p3) {
            p3->print(os, html);
        }
        else {
            os << "<nullptr>";
        }
    }
    else if (m_oper == opAt) {
        os << "@";

        if (p2) {
            p2->printr(os, html);
        }
        else {
            os << "nullptr>";
        }

        os << ":";

        if (p3) {
            p3->printr(os, html);
        }
        else {
            os << "nullptr>";
        }
    }
    else {
        LOG_FATAL("Invalid operator %1", operToString(m_oper));
    }
}


void Ternary::doSearchChildren(const Exp& pattern, std::list<SharedExp *>& li, bool once)
{
    doSearch(pattern, subExp1, li, once);

    if (once && !li.empty()) {
        return;
    }

    doSearch(pattern, subExp2, li, once);

    if (once && !li.empty()) {
        return;
    }

    doSearch(pattern, subExp3, li, once);
}


SharedExp Ternary::simplifyArith()
{
    subExp1 = subExp1->simplifyArith();
    subExp2 = subExp2->simplifyArith();
    subExp3 = subExp3->simplifyArith();
    return shared_from_this();
}


SharedExp Ternary::simplifyAddr()
{
    subExp1 = subExp1->simplifyAddr();
    subExp2 = subExp2->simplifyAddr();
    subExp3 = subExp3->simplifyAddr();
    return shared_from_this();
}


bool Ternary::acceptVisitor(ExpVisitor *v)
{
    bool visitChildren = true;
    if (!v->preVisit(shared_from_base<Ternary>(), visitChildren)) {
        return false;
    }

    if (visitChildren) {
        if (!subExp1->acceptVisitor(v) || !subExp2->acceptVisitor(v) || !subExp3->acceptVisitor(v)) {
            return false;
        }
    }

    return v->postVisit(shared_from_base<Ternary>());
}


void Ternary::printx(int ind) const
{
    LOG_MSG("%1%2", QString(ind, ' '), operToString(m_oper));

    printChild(subExp1, ind);
    printChild(subExp2, ind);
    printChild(subExp3, ind);
}


SharedType Ternary::ascendType()
{
    switch (m_oper)
    {
    case opFsize:
        return FloatType::get(subExp2->access<Const>()->getInt());

    case opZfill:
    case opSgnEx:
        {
            const int toSize = subExp2->access<Const>()->getInt();
            return Type::newIntegerLikeType(toSize, m_oper == opZfill ? -1 : 1);
        }

    default:
        break;
    }

    return VoidType::get();
}


void Ternary::descendType(SharedType /*parentType*/, bool& changed, Statement *s)
{
    switch (m_oper)
    {
    case opFsize:
        subExp3->descendType(FloatType::get(subExp1->access<Const>()->getInt()), changed, s);
        break;

    case opZfill:
    case opSgnEx:
        {
            int        fromSize = subExp1->access<Const>()->getInt();
            SharedType fromType;
            fromType = Type::newIntegerLikeType(fromSize, m_oper == opZfill ? -1 : 1);
            subExp3->descendType(fromType, changed, s);
            break;
        }

    default:
        break;
    }
}


SharedExp Ternary::acceptPreModifier(ExpModifier *mod, bool& visitChildren)
{
    return mod->preModify(access<Ternary>(), visitChildren);
}


SharedExp Ternary::acceptChildModifier(ExpModifier* mod)
{
    subExp1 = subExp1->acceptModifier(mod);
    subExp2 = subExp2->acceptModifier(mod);
    subExp3 = subExp3->acceptModifier(mod);
    return shared_from_this();
}


SharedExp Ternary::acceptPostModifier(ExpModifier *mod)
{
    return mod->postModify(access<Ternary>());
}
