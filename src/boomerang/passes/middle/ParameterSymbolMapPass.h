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


class ParameterSymbolMapPass : public IPass
{
public:
    ParameterSymbolMapPass();

public:
    bool execute(UserProc *proc) override;
};
