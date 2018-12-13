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


#include "boomerang/frontend/NJMCDecoder.h"


/**
 * The definition of the instruction decoder for ST20.
 */
class ST20Decoder : public NJMCDecoder
{
public:
    /// \copydoc NJMCDecoder::NJMCDecoder
    ST20Decoder(Project *project);
    ST20Decoder(const ST20Decoder &other) = delete;
    ST20Decoder(ST20Decoder &&other)      = default;

    virtual ~ST20Decoder() override = default;

    ST20Decoder &operator=(const ST20Decoder &other) = delete;
    ST20Decoder &operator=(ST20Decoder &&other) = default;

public:
    /// \copydoc NJMCDecoder::decodeInstruction
    /**
     * Decodes a machine instruction and returns an RTL instance. In all cases a single instruction
     * is decoded.
     * \param pc    the native address of the pc
     * \param delta the difference between the above address and the host address of the pc
     *              (i.e. the address that the pc is at in the
     *              loaded object file)
     * \returns     a DecodeResult structure containing all the information gathered during decoding
     */
    virtual bool decodeInstruction(Address pc, ptrdiff_t delta, DecodeResult &result) override;

private:
    DWord getDword(intptr_t lc); // TODO: switch back to using ADDRESS objects
    SWord getWord(intptr_t lc);
    Byte getByte(intptr_t lc);
};
