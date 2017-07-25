#define sign_extend(N, SIZE)    (((int)((N) << (sizeof(unsigned) * 8 - (SIZE)))) >> (sizeof(unsigned) * 8 - (SIZE)))

// #line 1 "frontend/machine/mips/decoder.m"

/****************************************************************
 *
 * FILENAME
 *
 *   \file decoder.m
 *
 * PURPOSE
 *
 * Decoding MIPS
 *
 * AUTHOR
 *
 *   \author Markus Gothe, nietzsche@lysator.liu.se
 *
 * REVISION
 *
 *   $Id$
 *
 *****************************************************************/

#include "mipsdecoder.h"

#include "boomerang/core/BinaryFileFactory.h" // For SymbolByAddress()
#include "boomerang/util/Log.h"

#include "boomerang/db/Prog.h"
#include "boomerang/db/Proc.h"
#include "boomerang/db/RTL.h"

#include <cassert>


MIPSDecoder::MIPSDecoder(Prog *_prog)
    : NJMCDecoder(_prog)
{
    m_rtlDict.readSSLFile(Boomerang::get()->getDataDirectory().absoluteFilePath("frontend/machine/mips/mips.ssl"));
}


DecodeResult& MIPSDecoder::decodeInstruction(Address pc, ptrdiff_t delta)
{
    Q_UNUSED(pc);
    Q_UNUSED(delta);

    static DecodeResult result;
    // ADDRESS hostPC = pc+delta;

    // Clear the result structure;
    result.reset();

    // The actual list of instantiated statements
    // std::list<Statement*>* stmts = nullptr;
    // ADDRESS nextPC = Address::INVALID;
    // Decoding goes here....

    return result;
}
