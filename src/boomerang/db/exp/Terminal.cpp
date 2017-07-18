#include "Terminal.h"

#include "boomerang/util/Log.h"
#include "boomerang/db/visitor.h"


Terminal::Terminal(OPER _op)
    : Exp(_op)
{
}


Terminal::Terminal(const Terminal& o)
    : Exp(o.m_oper)
{
}

SharedExp Terminal::clone() const
{
    return std::make_shared<Terminal>(*this);
}



bool Terminal::operator==(const Exp& o) const
{
    if (m_oper == opWildIntConst) {
        return o.getOper() == opIntConst;
    }

    if (m_oper == opWildStrConst) {
        return o.getOper() == opStrConst;
    }

    if (m_oper == opWildMemOf) {
        return o.getOper() == opMemOf;
    }

    if (m_oper == opWildRegOf) {
        return o.getOper() == opRegOf;
    }

    if (m_oper == opWildAddrOf) {
        return o.getOper() == opAddrOf;
    }

    return((m_oper == opWild) ||  // Wild matches anything
           (o.getOper() == opWild) || (m_oper == o.getOper()));
}

bool Terminal::operator<(const Exp& o) const
{
    return(m_oper < o.getOper());
}



bool Terminal::operator*=(const Exp& o) const
{
    const Exp *other = &o;

    if (o.getOper() == opSubscript) {
        other = o.getSubExp1().get();
    }

    return *this == *other;
}


void Terminal::print(QTextStream& os, bool) const
{
    switch (m_oper)
    {
    case opPC:
        os << "%pc";
        break;

    case opFlags:
        os << "%flags";
        break;

    case opFflags:
        os << "%fflags";
        break;

    case opCF:
        os << "%CF";
        break;

    case opZF:
        os << "%ZF";
        break;

    case opOF:
        os << "%OF";
        break;

    case opNF:
        os << "%NF";
        break;

    case opDF:
        os << "%DF";
        break;

    case opAFP:
        os << "%afp";
        break;

    case opAGP:
        os << "%agp";
        break;

    case opWild:
        os << "WILD";
        break;

    case opAnull:
        os << "%anul";
        break;

    case opFpush:
        os << "FPUSH";
        break;

    case opFpop:
        os << "FPOP";
        break;

    case opWildMemOf:
        os << "m[WILD]";
        break;

    case opWildRegOf:
        os << "r[WILD]";
        break;

    case opWildAddrOf:
        os << "a[WILD]";
        break;

    case opWildIntConst:
        os << "WILDINT";
        break;

    case opWildStrConst:
        os << "WILDSTR";
        break;

    case opNil:
        break;

    case opTrue:
        os << "true";
        break;

    case opFalse:
        os << "false";
        break;

    case opDefineAll:
        os << "<all>";
        break;

    default:
        LOG << "Terminal::print invalid operator " << operToString(m_oper) << "\n";
        assert(false);
    }
}


void Terminal::appendDotFile(QTextStream& of)
{
    of << "e_" << HostAddress(this).toString() << " [shape=parallelogram,label=\"";

    if (m_oper == opWild) {
        // Note: value is -1, so can't index array
        of << "WILD";
    }
    else {
        of << operToString(m_oper);
    }

    of << "\\n" << HostAddress(this).toString();
    of << "\"];\n";
}



bool Terminal::match(const QString& pattern, std::map<QString, SharedConstExp>& bindings)
{
    if (Exp::match(pattern, bindings)) {
        return true;
    }

#ifdef DEBUG_MATCH
    LOG << "terminal::match " << this << " to " << pattern << ".\n";
#endif
    return false;
}


bool Terminal::accept(ExpVisitor *v)
{
    return v->visit(shared_from_base<Terminal>());
}


SharedExp Terminal::accept(ExpModifier *v)
{
    // This is important if we need to modify terminals
    SharedExp val      = v->preVisit(shared_from_base<Terminal>());
    auto      term_res = std::dynamic_pointer_cast<Terminal>(val);

    if (term_res) {
        return v->postVisit(term_res);
    }

    auto ref_res = std::dynamic_pointer_cast<RefExp>(val);

    if (ref_res) {
        return v->postVisit(ref_res);
    }

    assert(false);
    return nullptr;
}


void Terminal::printx(int ind) const
{
    Util::alignStream(LOG_STREAM(), ind) << operToString(m_oper) << "\n";
    LOG_STREAM().flush();
}

