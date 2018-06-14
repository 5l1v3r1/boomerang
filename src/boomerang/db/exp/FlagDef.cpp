#pragma region License
/*
 * This file is part of the Boomerang Decompiler.
 *
 * See the file "LICENSE.TERMS" for information on usage and
 * redistribution of this file, and for a DISCLAIMER OF ALL
 * WARRANTIES.
 */
#pragma endregion License
#include "FlagDef.h"


#include "boomerang/db/RTL.h"
#include "boomerang/visitor/expmodifier/ExpModifier.h"
#include "boomerang/visitor/expvisitor/ExpVisitor.h"


FlagDef::FlagDef(const SharedExp& params, const SharedRTL& _rtl)
    : Unary(opFlagDef, params)
    , m_rtl(_rtl)
{
}


FlagDef::~FlagDef()
{
}


bool FlagDef::accept(ExpVisitor *v)
{
    bool visitChildren = true;
    if (!v->preVisit(shared_from_base<FlagDef>(), visitChildren)) {
        return false;
    }

    if (visitChildren) {
        if (!subExp1->accept(v)) {
            return false;
        }
    }

    return v->postVisit(shared_from_base<FlagDef>());
}


SharedExp FlagDef::accept(ExpModifier *mod)
{
    bool visitChildren = true;
    SharedExp ret        = mod->preModify(shared_from_base<FlagDef>(), visitChildren);
    std::shared_ptr<FlagDef> flgdef_ret = std::dynamic_pointer_cast<FlagDef>(ret);

    if (visitChildren) {
        subExp1 = subExp1->accept(mod);
    }

    assert(flgdef_ret);
    return mod->postModify(flgdef_ret);
}
