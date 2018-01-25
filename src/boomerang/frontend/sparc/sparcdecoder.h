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

class Prog;
struct DecodeResult;

class SparcMachine
{
public:
    /**
     * Decode the register on the RHS
     * \note    Replaces r[0] with const 0
     * \note    Not used by DIS_RD since don't want 0 on LHS
     * \param   reg_no - register (0-31)
     * \returns        the expression representing the register
     */
    SharedExp dis_RegRhs(uint8_t reg_no);
};

/**
 * The implementation of the instruction decoder for Sparc.
 */
class SparcDecoder : public NJMCDecoder
{
public:
    /// \copydoc NJMCDecoder::NJMCDecoder
    SparcDecoder(Prog *prog);
    SparcDecoder(const SparcDecoder& other) = delete;
    SparcDecoder(SparcDecoder&& other) = default;

    virtual ~SparcDecoder() override = default;

    SparcDecoder& operator=(const SparcDecoder& other) = delete;
    SparcDecoder& operator=(SparcDecoder&& other) = default;

public:
    /// \copydoc NJMCDecoder::decodeInstruction
    virtual bool decodeInstruction(Address pc, ptrdiff_t delta, DecodeResult& result) override;

    /**
     * Check to see if the instruction at the given offset is a restore instruction.
     *
     * \param      hostPC pointer to the code in question (host address)
     * \returns    True if a match found
     */
    bool isRestore(HostAddress hostPC);

private:

    /*
     * Various functions to decode the operands of an instruction into
     * a SemStr representation.
     */

    /**
     * Converts a dynamic address to a Exp* expression.
     * E.g. %o7 --> r[ 15 ]
     *
     * \param pc   the instruction stream address of the dynamic address
     * \param size redundant parameter on SPARC
     * \returns the Exp* representation of the given address
     */
    SharedExp dis_Eaddr(HostAddress pc, int size = 0);

    /**
     * Decode the register or immediate at the given address.
     *
     * \note         Used via macro DIS_ROI
     * \param        pc an address in the instruction stream
     * \returns      the register or immediate at the given address
     */
    SharedExp dis_RegImm(HostAddress pc);

    /**
     * Decode the register on the LHS
     * \param   r register (0-31)
     * \returns the expression representing the register
     */
    SharedExp dis_RegLhs(unsigned r);

    /**
     * Create an RTL for a Bx instruction
     *
     * \param pc - the location counter
     * \param stmts - ptr to list of Statement pointers
     * \param name - instruction name (e.g. "BNE,a", or "BPNE")
     * \returns            Pointer to newly created RTL, or nullptr if invalid
     */
    std::unique_ptr<RTL> createBranchRtl(Address pc, std::unique_ptr<RTL> stmts, const char *name);

    /**
     * Check to see if the instructions at the given offset match any callee prologue,
     * i.e. does it look like this offset is a pointer to a function?
     *
     * \param hostPC pointer to the code in question (host address)
     * \returns True if a match found
     */
    bool isFuncPrologue(HostAddress hostPC);

    /**
     * Returns the double starting at the given address.
     * \param     lc - address at which to decode the double
     * \returns   the decoded double
     */
    DWord getDword(HostAddress lc);

private:
    std::unique_ptr<SparcMachine> machine;
};
