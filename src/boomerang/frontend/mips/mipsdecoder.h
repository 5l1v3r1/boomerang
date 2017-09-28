#pragma region License
/*
 * This file is part of the Boomerang Decompiler.
 *
 * See the file "LICENSE.TERMS" for information on usage and
 * redistribution of this file, and for a DISCLAIMER OF ALL
 * WARRANTIES.
 */
#pragma endregion License
#pragma once


/****************************************************************
 *
 * FILENAME
 *
 *   \file mipsfrontend.h
 *
 * PURPOSE
 *
 *   Skeleton for MIPS disassembly.
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

#include "boomerang/frontend/NJMCDecoder.h"

#include <cstddef>

class Prog;
struct DecodeResult;

class MIPSDecoder : public NJMCDecoder
{
public:
    /// \copydoc NJMCDecoder::NJMCDecoder
    MIPSDecoder(Prog *prog);

    /// \copydoc NJMCDecoder::decodeInstruction

    /****************************************************************************/ /**
    * \brief   Attempt to decode the high level instruction at a given
    *              address and return the corresponding HL type (e.g. CallStatement,
    *              GotoStatement etc). If no high level instruction exists at the
    *              given address, then simply return the RTL for the low level
    *              instruction at this address. There is an option to also
    *              include the low level statements for a HL instruction.
    * \param   pc - the native address of the pc
    * \param   delta - the difference between the above address and the
    *              host address of the pc (i.e. the address that the pc is at in the loaded object file)
    * \returns a DecodeResult structure containing all the information
    *              gathered during decoding
    *********************************************************************************/
    virtual bool decodeInstruction(Address pc, ptrdiff_t delta, DecodeResult& result) override;
};
