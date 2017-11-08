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


// Class PPCFrontEnd: derived from FrontEnd, with source machine specific
// behaviour

#include "boomerang/frontend/Frontend.h"

class IFrontEnd;
class PPCDecoder;
class CallStatement;
struct DecodeResult;


/**
 * Contains routines to manage the decoding of ppc
 * instructions and the instantiation to RTLs, removing sparc
 * dependent features such as delay slots in the process. These
 * functions replace Frontend.cpp for decoding sparc instructions.
 */
class PPCFrontEnd : public IFrontEnd
{
public:
    /// \copydoc IFrontEnd::IFrontEnd
    PPCFrontEnd(IFileLoader *pLoader, Prog *Program);

    /// \copydoc IFrontEnd::~IFrontEnd
    virtual ~PPCFrontEnd();

    /// \copydoc IFrontEnd::getFrontEndId
    virtual Platform getType() const override { return Platform::PPC; }

    /// \copydoc IFrontEnd::processProc
    virtual bool processProc(Address uAddr, UserProc *pProc, QTextStream& os, bool frag = false, bool spec = false) override;

    /// \copydoc IFrontEnd::getDefaultParams
    virtual std::vector<SharedExp>& getDefaultParams() override;

    /// \copydoc IFrontEnd::getDefaultReturns
    virtual std::vector<SharedExp>& getDefaultReturns() override;

    /// \copydoc IFrontEnd::getMainEntryPoint
    virtual Address getMainEntryPoint(bool& gotMain) override;
};
