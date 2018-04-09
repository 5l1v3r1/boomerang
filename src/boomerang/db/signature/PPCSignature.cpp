#pragma region License
/*
 * This file is part of the Boomerang Decompiler.
 *
 * See the file "LICENSE.TERMS" for information on usage and
 * redistribution of this file, and for a DISCLAIMER OF ALL
 * WARRANTIES.
 */
#pragma endregion License
#include "PPCSignature.h"


#include "boomerang/db/exp/Location.h"
#include "boomerang/util/Log.h"
#include "boomerang/db/proc/UserProc.h"
#include "boomerang/db/statements/ImplicitAssign.h"
#include "boomerang/db/exp/Terminal.h"
#include "boomerang/db/Prog.h"


namespace CallingConvention
{
namespace StdC
{


PPCSignature::PPCSignature(const QString& name)
    : Signature(name)
{
    Signature::addReturn(Location::regOf(1));
    // Signature::addImplicitParameter(PointerType::get(new IntegerType()), "r1",
    //                                 Location::regOf(1), nullptr);
    // FIXME: Should also add m[r1+4] as an implicit parameter? Holds return address
}


PPCSignature::PPCSignature(Signature& old)
    : Signature(old)
{
}


std::shared_ptr<Signature> PPCSignature::clone() const
{
    PPCSignature *n = new PPCSignature(m_name);

    Util::clone(m_params, n->m_params);
    // n->implicitParams = implicitParams;
    Util::clone(m_returns, n->m_returns);
    n->m_ellipsis      = m_ellipsis;
    n->m_preferredName = m_preferredName;

    if (m_preferredReturn) {
        n->m_preferredReturn = m_preferredReturn->clone();
    }
    else {
        n->m_preferredReturn = nullptr;
    }

    n->m_preferredParams = m_preferredParams;
    n->m_unknown         = m_unknown;
    return std::shared_ptr<Signature>(n);
}


SharedExp PPCSignature::getArgumentExp(int n) const
{
    if (n < static_cast<int>(m_params.size())) {
        return Signature::getArgumentExp(n);
    }

    SharedExp e;

    if (n >= 8) {
        // PPCs pass the ninth and subsequent parameters at m[%r1+8],
        // m[%r1+12], etc.
        e = Location::memOf(Binary::get(opPlus, Location::regOf(1), Const::get(8 + (n - 8) * 4)));
    }
    else {
        e = Location::regOf(3 + n);
    }

    return e;
}


void PPCSignature::addReturn(SharedType type, SharedExp e)
{
    if (type->isVoid()) {
        return;
    }

    if (e == nullptr) {
        e = Location::regOf(3);
    }

    Signature::addReturn(type, e);
}


void PPCSignature::addParameter(const QString& name, const SharedExp& e,
                                SharedType type, const QString& boundMax)
{
    Signature::addParameter(name, e ? e : getArgumentExp(m_params.size()), type, boundMax);
}


SharedExp PPCSignature::getProven(SharedExp left) const
{
    if (left->isRegOfK()) {
        int r = left->access<Const, 1>()->getInt();

        switch (r)
        {
        case 1: // stack
            return left;
        }
    }

    return nullptr;
}


bool PPCSignature::isPreserved(SharedExp e) const
{
    if (e->isRegOfK()) {
        int r = e->access<Const, 1>()->getInt();
        return r == 1;
    }

    return false;
}


// Return a list of locations defined by library calls
void PPCSignature::getLibraryDefines(StatementList& defs)
{
    if (defs.size() > 0) {
        return; // Do only once
    }

    for (int r = 3; r <= 12; ++r) {
        defs.append(new ImplicitAssign(Location::regOf(r))); // Registers 3-12 are volatile (caller save)
    }
}



bool PPCSignature::qualified(UserProc *p, Signature& /*candidate*/)
{
    LOG_VERBOSE2("Consider promotion to stdc PPC signature for %1", p->getName());

    Platform plat = p->getProg()->getFrontEndId();

    if (plat != Platform::PPC) {
        return false;
    }

    LOG_VERBOSE2("Promoted to StdC::PPCSignature (always qualifies)");

    return true;
}

}
}
