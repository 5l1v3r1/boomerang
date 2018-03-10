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


/// Note: call the below after translating from SSA form
/// FIXME: this can be done before transforming out of SSA form now, surely...
class UnusedLocalRemovalPass : public IPass
{
public:
    UnusedLocalRemovalPass();

public:
    bool execute(UserProc *proc) override;
};
