#pragma region License
/*
 * This file is part of the Boomerang Decompiler.
 *
 * See the file "LICENSE.TERMS" for information on usage and
 * redistribution of this file, and for a DISCLAIMER OF ALL
 * WARRANTIES.
 */
#pragma endregion License
#include "mipsdecoder.h"


#include "boomerang/core/Boomerang.h"
#include "boomerang/core/Project.h"
#include "boomerang/db/Prog.h"
#include "boomerang/db/proc/Proc.h"
#include "boomerang/db/RTL.h"
#include "boomerang/util/Log.h"

#include <cassert>


MIPSDecoder::MIPSDecoder(Prog *prog)
    : NJMCDecoder(prog)
{
    m_rtlDict.readSSLFile(prog->getProject()->getSettings()->getDataDirectory().absoluteFilePath("ssl/mips.ssl"),
        prog->getProject()->getSettings()->debugDecoder);
}


bool MIPSDecoder::decodeInstruction(Address pc, ptrdiff_t delta, DecodeResult& result)
{
    Q_UNUSED(pc);
    Q_UNUSED(delta);

    // ADDRESS hostPC = pc+delta;

    // Clear the result structure;
    result.reset();
    // The actual list of instantiated statements
    // std::list<Statement*>* stmts = nullptr;
    // ADDRESS nextPC = Address::INVALID;
    // Decoding goes here.... TODO
    result.valid = false;
    return result.valid;
}
