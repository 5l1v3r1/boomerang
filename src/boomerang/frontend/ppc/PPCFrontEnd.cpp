#pragma region License
/*
 * This file is part of the Boomerang Decompiler.
 *
 * See the file "LICENSE.TERMS" for information on usage and
 * redistribution of this file, and for a DISCLAIMER OF ALL
 * WARRANTIES.
 */
#pragma endregion License
#include "PPCFrontEnd.h"


#include "boomerang/db/proc/ProcCFG.h"
#include "boomerang/db/proc/UserProc.h"
#include "boomerang/db/Prog.h"
#include "boomerang/db/signature/Signature.h"
#include "boomerang/frontend/ppc/PPCDecoder.h"
#include "boomerang/ssl/exp/Location.h"
#include "boomerang/ssl/Register.h"
#include "boomerang/ssl/RTL.h"
#include "boomerang/util/log/Log.h"

#include <cassert>
#include <iomanip>
#include <sstream>


PPCFrontEnd::PPCFrontEnd(BinaryFile *binaryFile, Prog *prog)
    : DefaultFrontEnd(binaryFile, prog)
{
    m_decoder.reset(new PPCDecoder(prog));
}


Address PPCFrontEnd::findMainEntryPoint(bool& gotMain)
{
    gotMain = true;
    Address start = m_binaryFile->getMainEntryPoint();

    if (start != Address::INVALID) {
        return start;
    }

    start   = m_binaryFile->getEntryPoint();
    gotMain = false;

    if (start == Address::INVALID) {
        return Address::INVALID;
    }

    gotMain = true;
    return start;
}


bool PPCFrontEnd::processProc(UserProc *proc, Address entryAddr)
{
    // Call the base class to do most of the work
    if (!DefaultFrontEnd::processProc(proc, entryAddr)) {
        return false;
    }

    // This will get done twice; no harm
    proc->setEntryBB();

    return true;
}
