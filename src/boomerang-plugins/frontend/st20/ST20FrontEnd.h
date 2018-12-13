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


#include "boomerang/frontend/DefaultFrontEnd.h"


/**
 * Contains routines to manage the decoding of st20
 * instructions and the instantiation to RTLs, removing st20
 * dependent features such as delay slots in the process.
 */
class ST20FrontEnd : public DefaultFrontEnd
{
public:
    /// \copydoc IFrontEnd::IFrontEnd
    ST20FrontEnd(Project *project);
    ST20FrontEnd(const ST20FrontEnd &other) = delete;
    ST20FrontEnd(ST20FrontEnd &&other)      = default;

    /// \copydoc IFrontEnd::~IFrontEnd
    virtual ~ST20FrontEnd() override = default;

    ST20FrontEnd &operator=(const ST20FrontEnd &other) = delete;
    ST20FrontEnd &operator=(ST20FrontEnd &&other) = default;

public:
    /// \copydoc IFrontEnd::processProc
    virtual bool processProc(UserProc *proc, Address entryAddr) override;

    /// \copydoc IFrontEnd::getMainEntryPoint
    virtual Address findMainEntryPoint(bool &gotMain) override;
};
