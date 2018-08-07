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


#include "boomerang/db/proc/UserProc.h"
#include "boomerang/db/Prog.h"
#include "boomerang/ssl/exp/Binary.h"
#include "boomerang/ssl/exp/Const.h"
#include "boomerang/ssl/exp/Location.h"
#include "boomerang/ssl/exp/Terminal.h"
#include "boomerang/ssl/statements/ImplicitAssign.h"
#include "boomerang/util/log/Log.h"


namespace CallingConvention
{
namespace StdC
{


PPCSignature::PPCSignature(const QString& name)
    : Signature(name)
{
    Signature::addReturn(Location::regOf(REG_PPC_G1));
    // Signature::addImplicitParameter(PointerType::get(new IntegerType()), "r1",
    //                                 Location::regOf(REG_PPC_G1), nullptr);
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
        e = Location::memOf(Binary::get(opPlus, Location::regOf(REG_PPC_G1), Const::get(8 + (n - 8) * 4)));
    }
    else {
        e = Location::regOf(REG_PPC_G3 + n);
    }

    return e;
}


void PPCSignature::addReturn(SharedType type, SharedExp e)
{
    if (type->isVoid()) {
        return;
    }

    if (e == nullptr) {
        e = Location::regOf(REG_PPC_G3);
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
    if (!left->isRegOfConst()) {
        return nullptr;
    }

    const int regIdx = left->access<Const, 1>()->getInt();
    return (regIdx == REG_PPC_G1) ? left : nullptr;
}


bool PPCSignature::isPreserved(SharedExp e) const
{
    if (e->isRegOfConst()) {
        const int regIdx = e->access<Const, 1>()->getInt();
        if (regIdx == REG_PPC_G1) {
            return true;
        }
    }

    return false;
}


// Return a list of locations defined by library calls
void PPCSignature::getLibraryDefines(StatementList& defs)
{
    if (defs.size() > 0) {
        return; // Do only once
    }

    for (int r = REG_PPC_G3; r <= REG_PPC_G12; ++r) {
        defs.append(new ImplicitAssign(Location::regOf(r))); // Registers 3-12 are volatile (caller save)
    }
}



bool PPCSignature::qualified(UserProc *p, Signature& /*candidate*/)
{
    LOG_VERBOSE2("Consider promotion to stdc PPC signature for %1", p->getName());

    if (p->getProg()->getMachine() != Machine::PPC) {
        return false;
    }

    LOG_VERBOSE2("Promoted to StdC::PPCSignature (always qualifies)");

    return true;
}

}
}
