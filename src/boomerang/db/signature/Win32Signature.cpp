#pragma region License
/*
 * This file is part of the Boomerang Decompiler.
 *
 * See the file "LICENSE.TERMS" for information on usage and
 * redistribution of this file, and for a DISCLAIMER OF ALL
 * WARRANTIES.
 */
#pragma endregion License
#include "Win32Signature.h"

#include "boomerang/db/Prog.h"
#include "boomerang/db/proc/UserProc.h"
#include "boomerang/ssl/exp/Binary.h"
#include "boomerang/ssl/exp/Const.h"
#include "boomerang/ssl/exp/Location.h"
#include "boomerang/ssl/exp/Terminal.h"
#include "boomerang/ssl/statements/ImplicitAssign.h"
#include "boomerang/ssl/type/PointerType.h"
#include "boomerang/ssl/type/SizeType.h"
#include "boomerang/util/log/Log.h"


namespace CallingConvention
{
Win32Signature::Win32Signature(const QString &name)
    : Signature(name)
{
    Signature::addReturn(Location::regOf(REG_PENT_EAX));
}


Win32Signature::Win32Signature(Signature &old)
    : Signature(old)
{
}


Win32TcSignature::Win32TcSignature(const QString &name)
    : Win32Signature(name)
{
    Signature::addParameter("this", Location::regOf(REG_PENT_ECX),
                            PointerType::get(VoidType::get()));
}


Win32TcSignature::Win32TcSignature(Signature &old)
    : Win32Signature(old)
{
}


std::shared_ptr<Signature> Win32Signature::clone() const
{
    Win32Signature *n = new Win32Signature(m_name);

    Util::clone(m_params, n->m_params);
    // cloneVec(implicitParams, n->implicitParams);
    Util::clone(m_returns, n->m_returns);

    n->m_ellipsis      = m_ellipsis;
    n->m_preferredName = m_preferredName;

    return std::shared_ptr<Signature>(n);
}


std::shared_ptr<Signature> Win32TcSignature::clone() const
{
    Win32TcSignature *n = new Win32TcSignature(m_name);

    Util::clone(m_params, n->m_params);
    // cloneVec(implicitParams, n->implicitParams);
    Util::clone(m_returns, n->m_returns);

    n->m_ellipsis      = m_ellipsis;
    n->m_preferredName = m_preferredName;

    return std::shared_ptr<Signature>(n);
}


bool Win32Signature::operator==(const Signature &other) const
{
    return Signature::operator==(other);
}


static SharedExp savedReturnLocation = Location::memOf(Location::regOf(REG_PENT_ESP));
static SharedExp stackPlusFour = Binary::get(opPlus, Location::regOf(REG_PENT_ESP), Const::get(4));

bool Win32Signature::qualified(UserProc *p, Signature & /*candidate*/)
{
    if (p->getProg()->getMachine() != Machine::PENTIUM || !p->getProg()->isWin32()) {
        return false;
    }

    LOG_VERBOSE2("Consider promotion to stdc win32 signature for %1", p->getName());

    bool gotcorrectret1, gotcorrectret2 = false;
    SharedExp provenPC = p->getProven(Terminal::get(opPC));
    gotcorrectret1     = provenPC && (*provenPC == *savedReturnLocation);

    if (gotcorrectret1) {
        LOG_VERBOSE2("got pc = m[r[28]]");

        SharedExp provenSP = p->getProven(Location::regOf(REG_PENT_ESP));
        gotcorrectret2     = provenSP && *provenSP == *stackPlusFour;

        if (gotcorrectret2) {
            LOG_VERBOSE2("Got r[28] = r[28] + 4");
        }
    }

    LOG_VERBOSE2("Qualified: %1", (gotcorrectret1 && gotcorrectret2));

    return gotcorrectret1 && gotcorrectret2;
}


void Win32Signature::addReturn(SharedType type, SharedExp e)
{
    if (type->isVoid()) {
        return;
    }

    if (e == nullptr) {
        if (type->isFloat()) {
            e = Location::regOf(REG_PENT_ST0);
        }
        else {
            e = Location::regOf(REG_PENT_EAX);
        }
    }

    Signature::addReturn(type, e);
}


void Win32Signature::addParameter(const QString &name, const SharedExp &e, SharedType type,
                                  const QString &boundMax)
{
    Signature::addParameter(name, e ? e : getArgumentExp(m_params.size()), type, boundMax);
}


SharedExp Win32Signature::getArgumentExp(int n) const
{
    if (n < static_cast<int>(m_params.size())) {
        return Signature::getArgumentExp(n);
    }

    SharedExp esp = Location::regOf(REG_PENT_ESP);

    if ((m_params.size() != 0) && (*m_params[0]->getExp() == *esp)) {
        n--;
    }

    SharedExp e = Location::memOf(Binary::get(opPlus, esp, Const::get((n + 1) * 4)));
    return e;
}


SharedExp Win32TcSignature::getArgumentExp(int n) const
{
    if (n < static_cast<int>(m_params.size())) {
        return Signature::getArgumentExp(n);
    }

    SharedExp esp = Location::regOf(REG_PENT_ESP);

    if (!m_params.empty() && (*m_params[0]->getExp() == *esp)) {
        n--;
    }

    if (n == 0) {
        // It's the first parameter, register ecx
        return Location::regOf(REG_PENT_ECX);
    }

    // Else, it is m[esp+4n)]
    SharedExp e = Location::memOf(Binary::get(opPlus, esp, Const::get(n * 4)));
    return e;
}


std::shared_ptr<Signature> Win32Signature::promote(UserProc * /*p*/)
{
    // no promotions from win32 signature up, yet.
    // a possible thing to investigate would be COM objects
    return shared_from_this();
}


SharedExp Win32Signature::getProven(SharedExp left) const
{
    size_t nparams = m_params.size();

    if ((nparams > 0) && (*m_params[0]->getExp() == *Location::regOf(REG_PENT_ESP))) {
        nparams--;
    }

    if (left->isRegOfConst()) {
        switch (left->access<Const, 1>()->getInt()) {
            // Note: assumes callee pop... not true for cdecl functions!
        case REG_PENT_ESP:
            return Binary::get(opPlus, Location::regOf(REG_PENT_ESP), Const::get(4 + nparams * 4));
        case REG_PENT_EBX: return Location::regOf(REG_PENT_EBX);
        case REG_PENT_EBP: return Location::regOf(REG_PENT_EBP);
        case REG_PENT_ESI: return Location::regOf(REG_PENT_ESI);
        case REG_PENT_EDI:
            return Location::regOf(REG_PENT_EDI);
            // there are other things that must be preserved here, look at calling convention
        }
    }

    return nullptr;
}


bool Win32Signature::isPreserved(SharedExp e) const
{
    if (e->isRegOfConst()) {
        switch (e->access<Const, 1>()->getInt()) {
        case REG_PENT_EBP: // ebp
        case REG_PENT_EBX: // ebx
        case REG_PENT_ESI: // esi
        case REG_PENT_EDI: // edi
        case REG_PENT_BX:  // bx
        case REG_PENT_BP:  // bp
        case REG_PENT_SI:  // si
        case REG_PENT_DI:  // di
        case REG_PENT_BL:  // bl
        case REG_PENT_BH:  // bh
            return true;
        default: return false;
        }
    }

    return false;
}


void Win32Signature::getLibraryDefines(StatementList &defs)
{
    if (defs.size()) {
        return; // Do only once
    }

    auto r24      = Location::regOf(REG_PENT_EAX); // eax
    SharedType ty = SizeType::get(32);

    if (m_returns.size() > 1) { // Ugh - note the stack pointer is the first return still
        ty = m_returns[1]->getType();
    }

    defs.append(new ImplicitAssign(ty, r24));                       // eax
    defs.append(new ImplicitAssign(Location::regOf(REG_PENT_ECX))); // ecx
    defs.append(new ImplicitAssign(Location::regOf(REG_PENT_EDX))); // edx
    defs.append(new ImplicitAssign(Location::regOf(REG_PENT_ESP))); // esp
}


SharedExp Win32TcSignature::getProven(SharedExp left) const
{
    if (left->isRegOfConst()) {
        if (left->access<Const, 1>()->getInt() == REG_PENT_ESP) {
            int nparams = m_params.size();

            if ((nparams > 0) && (*m_params[0]->getExp() == *Location::regOf(REG_PENT_ESP))) {
                nparams--;
            }

            // r28 += 4 + nparams*4 - 4        (-4 because ecx is register param)
            return Binary::get(opPlus, Location::regOf(REG_PENT_ESP),
                               Const::get(4 + nparams * 4 - 4));
        }
    }

    // Else same as for standard Win32 signature
    return Win32Signature::getProven(left);
}
}
