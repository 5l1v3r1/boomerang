#include <iostream>
#include <string>
#include "include/types.h"
#include "include/cfg.h"
#include "include/proc.h"
#include "include/prog.h"
#include "include/boomerang.h"
#include "analysis.h"

#include "typeStubs.cpp"
#include "signatureStubs.cpp"

// Cfg
void Cfg::dominators(DOM *d)
{
}


void Cfg::placePhiFunctions(DOM *d, int memDepth)
{
}


void Cfg::renameBlockVars(DOM *d, int n, int memDepth)
{
}


// Misc
Boomerang::Boomerang()
{
}


Boomerang *Boomerang::boomerang = nullptr;
bool isSwitch(PBB pSwitchBB, Exp *pDest, UserProc *pProc, BinaryFile *pBF)
{
	return false;
}


void processSwitch(PBB pBB, int delta, Cfg *pCfg, TargetQueue& tq, BinaryFile *pBF)
{
}


void Analysis::analyse(UserProc *proc)
{
}


HLLCode *Boomerang::getHLLCode(UserProc *p)
{
	return 0;
}
