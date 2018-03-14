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


#include "boomerang/passes/Pass.h"


class LocationSet;


class StatementPropagationPass : public IPass
{
public:
    StatementPropagationPass();

public:
    bool execute(UserProc *proc) override;

private:
    /// Find the locations that are used by a live, dominating phi-function
    void findLiveAtDomPhi(UserProc *proc, LocationSet& usedByDomPhi);

};
