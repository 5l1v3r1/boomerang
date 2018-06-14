#pragma region License
/*
 * This file is part of the Boomerang Decompiler.
 *
 * See the file "LICENSE.TERMS" for information on usage and
 * redistribution of this file, and for a DISCLAIMER OF ALL
 * WARRANTIES.
 */
#pragma endregion License
#include "CallBypasser.h"


#include "boomerang/db/statements/CallStatement.h"
#include "boomerang/db/exp/Location.h"
#include "boomerang/db/exp/RefExp.h"


CallBypasser::CallBypasser(Statement* enclosing)
    : m_enclosingStmt(enclosing)
{
}


SharedExp CallBypasser::postModify(const std::shared_ptr<RefExp>& exp)
{
    // If child was modified, simplify now
    SharedExp ret = exp;

    if (!(m_unchanged & m_mask)) {
        ret = exp->simplify();
    }

    m_mask >>= 1;
    // Note: r (the pointer) will always == ret (also the pointer) here, so the below is safe and avoids a cast
    Statement     *def  = exp->getDef();
    CallStatement *call = dynamic_cast<CallStatement *>(def);

    if (call) {
        assert(std::dynamic_pointer_cast<RefExp>(ret));
        bool ch;
        ret = call->bypassRef(std::static_pointer_cast<RefExp>(ret), ch);

        if (ch) {
            m_unchanged &= ~m_mask;
            setModified(true);
            // Now have to recurse to do any further bypassing that may be required
            // E.g. bypass the two recursive calls in fibo?? FIXME: check!
            CallBypasser *bp = new CallBypasser(m_enclosingStmt);
            SharedExp result = ret->accept(bp);
            delete bp;
            return result;
        }
    }

    // Else just leave as is (perhaps simplified)
    return ret;
}


SharedExp CallBypasser::postModify(const std::shared_ptr<Location>& exp)
{
    // Hack to preserve a[m[x]]. Can likely go when ad hoc TA goes.
    bool isAddrOfMem = exp->isAddrOf() && exp->getSubExp1()->isMemOf();

    if (isAddrOfMem) {
        return exp;
    }

    SharedExp ret = exp;

    if (!(m_unchanged & m_mask)) {
        ret = exp->simplify();
    }

    m_mask >>= 1;
    return ret;
}
