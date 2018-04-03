#pragma region License
/*
 * This file is part of the Boomerang Decompiler.
 *
 * See the file "LICENSE.TERMS" for information on usage and
 * redistribution of this file, and for a DISCLAIMER OF ALL
 * WARRANTIES.
 */
#pragma endregion License
#include "TypedExp.h"


#include "boomerang/core/Boomerang.h"
#include "boomerang/db/visitor/expmodifier/ExpModifier.h"
#include "boomerang/db/visitor/expvisitor/ExpVisitor.h"
#include "boomerang/type/type/Type.h"
#include "boomerang/util/Log.h"


TypedExp::TypedExp(SharedExp e1)
    : Unary(opTypedExp, e1)
    , m_type(nullptr)
{
}


TypedExp::TypedExp(SharedType ty, SharedExp e1)
    : Unary(opTypedExp, e1)
    , m_type(ty)
{
}


TypedExp::TypedExp(const TypedExp& o)
    : Unary(o)
{
    m_type    = o.m_type->clone();
}


SharedExp TypedExp::clone() const
{
    return std::make_shared<TypedExp>(m_type, subExp1->clone());
}


bool TypedExp::operator==(const Exp& o) const
{
    if (static_cast<const TypedExp &>(o).m_oper == opWild) {
        return true;
    }

    if (static_cast<const TypedExp &>(o).m_oper != opTypedExp) {
        return false;
    }

    // This is the strict type version
    if (*m_type != *static_cast<const TypedExp &>(o).m_type) {
        return false;
    }

    return *this->getSubExp1() == *o.getSubExp1();
}


bool TypedExp::operator<<(const Exp& o) const   // Type insensitive
{
    if (m_oper < o.getOper()) {
        return true;
    }

    if (m_oper > o.getOper()) {
        return false;
    }

    return *subExp1 << *o.getSubExp1();
}


bool TypedExp::operator<(const Exp& o) const   // Type sensitive
{
    if (m_oper < o.getOper()) {
        return true;
    }

    if (m_oper > o.getOper()) {
        return false;
    }

    if (*m_type < *static_cast<const TypedExp &>(o).m_type) {
        return true;
    }

    if (*static_cast<const TypedExp &>(o).m_type < *m_type) {
        return false;
    }

    return *subExp1 < *o.getSubExp1();
}


bool TypedExp::operator*=(const Exp& o) const
{
    const Exp *other = &o;

    if (o.getOper() == opSubscript) {
        other = o.getSubExp1().get();
    }

    if (other->getOper() == opWild) {
        return true;
    }

    if (other->getOper() != opTypedExp) {
        return false;
    }

    // This is the strict type version
    if (*m_type != *static_cast<const TypedExp *>(other)->m_type) {
        return false;
    }

    return *getSubExp1() *= *other->getSubExp1();
}


void TypedExp::print(QTextStream& os, bool html) const
{
    os << " ";
    m_type->starPrint(os);
    SharedConstExp p1 = this->getSubExp1();
    p1->print(os, html);
}


void TypedExp::appendDotFile(QTextStream& of)
{
    of << "e_" << HostAddress(this) << " [shape=record,label=\"{";
    of << "opTypedExp\\n" << HostAddress(this) << " | ";
    // Just display the C type for now
    of << m_type->getCtype() << " | <p1>";
    of << " }\"];\n";
    subExp1->appendDotFile(of);
    of << "e_" << HostAddress(this) << ":p1->e_" << HostAddress(subExp1.get()) << ";\n";
}


SharedExp TypedExp::polySimplify(bool& changed)
{
    SharedExp res = shared_from_this();

    if (subExp1->getOper() == opRegOf) {
        // type cast on a reg of.. hmm.. let's remove this
        res  = res->getSubExp1();
        changed = true;
        return res;
    }

    subExp1 = subExp1->simplify();
    return res;
}


bool TypedExp::accept(ExpVisitor *v)
{
    bool visitChildren = true;
    bool ret = v->preVisit(shared_from_base<TypedExp>(), visitChildren);

    if (!visitChildren) {
        return ret;
    }

    if (ret) {
        ret = subExp1->accept(v);
    }

    return ret;
}


SharedExp TypedExp::accept(ExpModifier *v)
{
    bool visitChildren;
    auto ret          = v->preModify(shared_from_base<TypedExp>(), visitChildren);
    auto typedexp_ret = std::dynamic_pointer_cast<TypedExp>(ret);

    if (visitChildren) {
        subExp1 = subExp1->accept(v);
    }

    assert(typedexp_ret);
    return v->postModify(typedexp_ret);
}


void TypedExp::printx(int ind) const
{
    LOG_MSG("%1%2 %3", QString(ind, ' '), operToString(m_oper), m_type->getCtype());
    printChild(subExp1, ind);
}


SharedType TypedExp::ascendType()
{
    return m_type;
}

void TypedExp::descendType(SharedType, bool&, Statement*) {}

