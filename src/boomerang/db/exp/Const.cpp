#pragma region License
/*
 * This file is part of the Boomerang Decompiler.
 *
 * See the file "LICENSE.TERMS" for information on usage and
 * redistribution of this file, and for a DISCLAIMER OF ALL
 * WARRANTIES.
 */
#pragma endregion License
#include "Const.h"


#include "boomerang/db/Visitor.h"
#include "boomerang/core/Boomerang.h"
#include "boomerang/util/Util.h"
#include "boomerang/util/Log.h"
#include "boomerang/type/type/ArrayType.h"
#include "boomerang/type/type/VoidType.h"
#include "boomerang/type/type/PointerType.h"
#include "boomerang/type/type/IntegerType.h"
#include "boomerang/type/type/CharType.h"
#include "boomerang/type/type/FloatType.h"


Const::Const(uint32_t i)
    : Exp(opIntConst)
    , m_conscript(0)
    , m_type(VoidType::get())
{
    m_value.i = i;
}


Const::Const(int i)
    : Exp(opIntConst)
    , m_conscript(0)
    , m_type(VoidType::get())
{
    m_value.i = i;
}


Const::Const(QWord ll)
    : Exp(opLongConst)
    , m_conscript(0)
    , m_type(VoidType::get())
{
    m_value.ll = ll;
}


Const::Const(double d)
    : Exp(opFltConst)
    , m_conscript(0)
    , m_type(VoidType::get())
{
    m_value.d = d;
}


Const::Const(const QString& p)
    : Exp(opStrConst)
    , m_conscript(0)
    , m_type(VoidType::get())
{
    m_string = p;
}


Const::Const(Function *p)
    : Exp(opFuncConst)
    , m_conscript(0)
    , m_type(VoidType::get())
{
    m_value.pp = p;
}


Const::Const(Address a)
    : Exp(opIntConst)
    , m_conscript(0)
    , m_type(VoidType::get())
{
    m_value.ll = a.value();
}


// Copy constructor
Const::Const(const Const& o)
    : Exp(o.m_oper)
{
    m_value           = o.m_value;
    m_conscript = o.m_conscript;
    m_type      = o.m_type;
    m_string    = o.m_string;
}


bool Const::operator<(const Exp& o) const
{
    if (m_oper < o.getOper()) {
        return true;
    }

    if (m_oper > o.getOper()) {
        return false;
    }

    if (m_conscript) {
        if (m_conscript < ((Const&)o).m_conscript) {
            return true;
        }

        if (m_conscript > ((Const&)o).m_conscript) {
            return false;
        }
    }
    else if (((Const&)o).m_conscript) {
        return true;
    }

    switch (m_oper)
    {
    case opIntConst:
        return m_value.i < ((Const&)o).m_value.i;

    case opLongConst:
        return m_value.ll < ((Const&)o).m_value.ll;

    case opFltConst:
        return m_value.d < ((Const&)o).m_value.d;

    case opStrConst:
        return m_string < ((Const&)o).m_string;

    default:
        LOG_FATAL("Invalid operator %1", operToString(m_oper));
    }

    return false;
}


bool Const::operator*=(const Exp& o) const
{
    const Exp *other = &o;

    if (o.getOper() == opSubscript) {
        other = o.getSubExp1().get();
    }

    return *this == *other;
}


SharedExp Const::genConstraints(SharedExp result)
{
    if (result->isTypeVal()) {
        // result is a constant type, or possibly a partial type such as ptr(alpha)
        SharedType t     = result->access<TypeVal>()->getType();
        bool       constraintMatched = false;

        switch (m_oper)
        {
        case opLongConst:
        // An integer constant is compatible with any size of integer, as long is it is in the right range
        // (no test yet) FIXME: is there an endianness issue here?
        case opIntConst:
            constraintMatched = t->isInteger();

            // An integer constant can also match a pointer to something.  Assume values less than 0x100 can't be a
            // pointer
            if ((unsigned)m_value.i >= 0x100) {
                constraintMatched |= t->isPointer();
            }

            // We can co-erce 32 bit constants to floats
            constraintMatched |= t->isFloat();
            break;

        case opStrConst:

            if (t->isPointer()) {
                auto ptr_type = std::static_pointer_cast<PointerType>(t);
                constraintMatched = ptr_type->getPointsTo()->isChar() ||
                        (ptr_type->getPointsTo()->isArray() &&
                         (ptr_type->getPointsTo())->as<ArrayType>()->getBaseType()->isChar());
            }

            break;

        case opFltConst:
            constraintMatched = t->isFloat();
            break;

        default:
            break;
        }

        if (constraintMatched) {
            // This constant may require a cast or a change of format. So we generate a constraint.
            // Don't clone 'this', so it can be co-erced after type analysis
            return Binary::get(opEquals, Unary::get(opTypeOf, shared_from_this()), result->clone());
        }
        else {
            // Doesn't match
            return Terminal::get(opFalse);
        }
    }

    // result is a type variable, which is constrained by this constant
    SharedType t;

    switch (m_oper)
    {
    case opIntConst:
        {
            // We have something like local1 = 1234.  Either they are both integer, or both pointer
            SharedType intt = IntegerType::get(0);
            SharedType alph = PointerType::newPtrAlpha();
            return Binary::get(
                opOr, Binary::get(
                    opAnd, Binary::get(opEquals, result->clone(), TypeVal::get(intt)),
                    Binary::get(opEquals,
                                Unary::get(opTypeOf,
                                           // Note: don't clone 'this', so we can change the Const after type analysis!
                                           shared_from_this()),
                                TypeVal::get(intt))),
                Binary::get(opAnd, Binary::get(opEquals, result->clone(), TypeVal::get(alph)),
                            Binary::get(opEquals, Unary::get(opTypeOf, shared_from_this()), TypeVal::get(alph))));
        }

    case opLongConst:
        t = IntegerType::get(64);
        break;

    case opStrConst:
        t = PointerType::get(CharType::get());
        break;

    case opFltConst:
        t = FloatType::get(64);     // size is not known. Assume double for now
        break;

    default:
        return nullptr;
    }

    auto      tv = TypeVal::get(t);
    SharedExp e  = Binary::get(opEquals, result->clone(), tv);
    return e;
}


QString Const::getFuncName() const
{
    return m_value.pp->getName();
}


bool Const::accept(ExpVisitor *v)
{
    return v->visit(shared_from_base<Const>());
}


SharedExp Const::accept(ExpModifier *v)
{
    auto ret       = v->preVisit(shared_from_base<Const>());
    auto const_ret = std::dynamic_pointer_cast<Const>(ret);

    assert(const_ret);
    return v->postVisit(const_ret);
}


void Const::printx(int ind) const
{
    LOG_MSG("%1%2", QString(ind, ' '), operToString(m_oper));

    switch (m_oper)
    {
    case opIntConst:
        LOG_MSG("%1", m_value.i);
        break;

    case opStrConst:
        LOG_MSG("\"%1\"", m_string);
        break;

    case opFltConst:
        LOG_MSG("%1", m_value.d);
        break;

    case opFuncConst:
        LOG_MSG(m_value.pp->getName());
        break;

    default:
        LOG_MSG("?%1?", (int)m_oper);
        break;
    }

    if (m_conscript) {
        LOG_MSG("\"%1\"", m_conscript);
    }
}


SharedExp Const::clone() const
{
    // Note: not actually cloning the Type* type pointer. Probably doesn't matter with GC
    return Const::get(*this);
}


void Const::print(QTextStream& os, bool) const
{
    switch (m_oper)
    {
    case opIntConst:

        if ((m_value.i < -1000) || (m_value.i > 1000)) {
            os << "0x" << QString::number(m_value.i, 16);
        }
        else {
            os << m_value.i;
        }

        break;

    case opLongConst:

        if (((long long)m_value.ll < -1000LL) || ((long long)m_value.ll > 1000LL)) {
            os << "0x" << QString::number(m_value.ll, 16) << "LL";
        }
        else {
            os << m_value.ll << "LL";
        }

        break;

    case opFltConst:
        os << QString("%1").arg(m_value.d); // respects English locale
        break;

    case opStrConst:
        os << "\"" << m_string << "\"";
        break;

    default:
        LOG_FATAL("Invalid operator %1", operToString(m_oper));
    }

    if (m_conscript) {
        os << "\\" << m_conscript << "\\";
    }

#ifdef DUMP_TYPES
    if (type) {
        os << "T(" << type->prints() << ")";
    }
#endif
}


void Const::printNoQuotes(QTextStream& os) const
{
    if (m_oper == opStrConst) {
        os << m_string;
    }
    else {
        print(os);
    }
}


void Const::appendDotFile(QTextStream& of)
{
    // We define a unique name for each node as "e_0x123456" if the address of "this" == 0x123456
    of << "e_" << HostAddress(this).toString() << " [shape=record,label=\"{";
    of << operToString(m_oper) << "\\n" << HostAddress(this).toString() << " | ";

    switch (m_oper)
    {
    case opIntConst:
        of << m_value.i;
        break;

    case opFltConst:
        of << m_value.d;
        break;

    case opStrConst:
        of << "\\\"" << m_string << "\\\"";
        break;

    // Might want to distinguish this better, e.g. "(func*)myProc"
    case opFuncConst:
        of << m_value.pp->getName();
        break;

    default:
        break;
    }

    of << " }\"];\n";
}


bool Const::match(const QString& pattern, std::map<QString, SharedConstExp>& bindings)
{
    if (Exp::match(pattern, bindings)) {
        return true;
    }

#ifdef DEBUG_MATCH
    LOG_MSG("Matching %1 to %2.", this, pattern);
#endif
    return false;
}


bool Const::operator==(const Exp& o) const
{
    // Note: the casts of o to Const& are needed, else op is protected! Duh.
    if (o.getOper() == opWild) {
        return true;
    }

    if ((o.getOper() == opWildIntConst) && (m_oper == opIntConst)) {
        return true;
    }

    if ((o.getOper() == opWildStrConst) && (m_oper == opStrConst)) {
        return true;
    }

    if (m_oper != o.getOper()) {
        return false;
    }

    if ((m_conscript && (m_conscript != ((Const&)o).m_conscript)) || ((Const&)o).m_conscript) {
        return false;
    }

    switch (m_oper)
    {
    case opIntConst:
        return m_value.i == ((Const&)o).m_value.i;

    case opLongConst:
        return m_value.ll == ((Const&)o).m_value.ll;

    case opFltConst:
        return m_value.d == ((Const&)o).m_value.d;

    case opStrConst:
        return m_string == ((Const&)o).m_string;

    default:
        LOG_FATAL("Invalid operator %1", operToString(m_oper));
    }

    return false;
}
