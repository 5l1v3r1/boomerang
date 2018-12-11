#pragma region License
/*
 * This file is part of the Boomerang Decompiler.
 *
 * See the file "LICENSE.TERMS" for information on usage and
 * redistribution of this file, and for a DISCLAIMER OF ALL
 * WARRANTIES.
 */
#pragma endregion License
#include "PentiumFrontEnd.h"

#include "boomerang/db/BasicBlock.h"
#include "boomerang/db/Prog.h"
#include "boomerang/db/binary/BinaryImage.h"
#include "boomerang/db/binary/BinarySymbol.h"
#include "boomerang/db/binary/BinarySymbolTable.h"
#include "boomerang/db/proc/LibProc.h"
#include "boomerang/db/proc/UserProc.h"
#include "boomerang/db/signature/Signature.h"
#include "boomerang/frontend/pentium/CapstoneX86Decoder.h"
#include "boomerang/frontend/pentium/StringInstructionProcessor.h"
#include "boomerang/ssl/RTL.h"
#include "boomerang/ssl/exp/Const.h"
#include "boomerang/ssl/exp/Location.h"
#include "boomerang/ssl/exp/Terminal.h"
#include "boomerang/ssl/exp/Ternary.h"
#include "boomerang/ssl/statements/CallStatement.h"
#include "boomerang/ssl/type/CompoundType.h"
#include "boomerang/ssl/type/FloatType.h"
#include "boomerang/ssl/type/FuncType.h"
#include "boomerang/ssl/type/IntegerType.h"
#include "boomerang/ssl/type/PointerType.h"
#include "boomerang/util/log/Log.h"


void PentiumFrontEnd::bumpRegisterAll(SharedExp e, int min, int max, int delta, int mask)
{
    std::list<SharedExp *> li;
    SharedExp exp = e;
    // Use doSearch, which is normally an internal method of Exp, to avoid problems of replacing the
    // wrong subexpression (in some odd cases)
    Exp::doSearch(*Terminal::get(opWildRegOf), exp, li, false);

    for (SharedExp *it : li) {
        int reg = (*it)->access<Const, 1>()->getInt();

        if ((min <= reg) && (reg <= max)) {
            // Replace the K in r[ K] with a new K
            // **it is a reg[K]
            auto K = (*it)->access<Const, 1>();
            K->setInt(min + ((reg - min + delta) & mask));
        }
    }
}


bool PentiumFrontEnd::processProc(UserProc *function, Address addr)
{
    // Call the base class to do most of the work
    if (!DefaultFrontEnd::processProc(function, addr)) {
        return false;
    }

    // This will get done twice; no harm
    function->setEntryBB();


    // Process away %rpt and %skip
    processStringInst(function);

    // Process code for side effects of overlapped registers
    processOverlapped(function);

    return true;
}


bool PentiumFrontEnd::isHelperFunc(Address dest, Address addr, RTLList &lrtl)
{
    if (dest == Address::INVALID) {
        return false;
    }

    QString name = m_program->getSymbolNameByAddr(dest);

    if (name.isEmpty()) {
        return false;
    }

    // I believe that __xtol is for gcc, _ftol for earlier MSVC compilers, _ftol2 for MSVC V7
    if ((name == "__xtol") || (name == "_ftol") || (name == "_ftol2")) {
        // This appears to pop the top of stack, and converts the result to a 64 bit integer in
        // edx:eax. Truncates towards zero r[tmpl] = ftoi(80, 64, r[32]) r[24] = trunc(64, 32,
        // r[tmpl]) r[26] = r[tmpl] >> 32
        Statement *a = new Assign(IntegerType::get(64),
                                  Location::tempOf(Const::get(const_cast<char *>("tmpl"))),
                                  std::make_shared<Ternary>(opFtoi, Const::get(64), Const::get(32),
                                                            Location::regOf(REG_PENT_ST0)));
        std::unique_ptr<RTL> newRTL(new RTL(addr));
        newRTL->append(a);
        a = new Assign(
            Location::regOf(REG_PENT_EAX),
            std::make_shared<Ternary>(opTruncs, Const::get(64), Const::get(32),
                                      Location::tempOf(Const::get(const_cast<char *>("tmpl")))));
        newRTL->append(a);
        a = new Assign(Location::regOf(REG_PENT_EDX),
                       Binary::get(opShiftR,
                                   Location::tempOf(Const::get(const_cast<char *>("tmpl"))),
                                   Const::get(32)));
        newRTL->append(a);

        // Append this RTL to the list of RTLs for this BB
        lrtl.push_back(std::move(newRTL));

        // Return true, so the caller knows not to create a HLCall
        return true;
    }
    else if (name == "__mingw_allocstack") {
        std::unique_ptr<RTL> newRTL(new RTL(addr));
        newRTL->append(new Assign(
            Location::regOf(REG_PENT_ESP),
            Binary::get(opMinus, Location::regOf(REG_PENT_ESP), Location::regOf(REG_PENT_EAX))));
        lrtl.push_back(std::move(newRTL));
        m_program->removeFunction(name);
        return true;
    }
    else if ((name == "__mingw_frame_init") || (name == "__mingw_cleanup_setup") ||
             (name == "__mingw_frame_end")) {
        LOG_MSG("Found removable call to static lib proc %1 at address %2", name, addr);
        m_program->removeFunction(name);
        return true;
    }
    else {
        // Will be other cases in future
    }

    return false;
}


PentiumFrontEnd::PentiumFrontEnd(BinaryFile *binaryFile, Prog *prog)
    : DefaultFrontEnd(binaryFile, prog)
{
    m_decoder.reset(new CapstoneX86Decoder(prog));
}


PentiumFrontEnd::~PentiumFrontEnd()
{
}


Address PentiumFrontEnd::findMainEntryPoint(bool &gotMain)
{
    Address start = m_binaryFile->getMainEntryPoint();

    if (start != Address::INVALID) {
        gotMain = true;
        return start;
    }

    gotMain = false;
    start   = m_binaryFile->getEntryPoint();

    if (start.isZero() || (start == Address::INVALID)) {
        return Address::INVALID;
    }

    int numInstructionsLeft = 100;
    Address addr            = start;
    Address prevAddr        = Address::INVALID; // address of previous instruction

    BinarySymbolTable *symbols = m_program->getBinaryFile()->getSymbols();
    // Look for 3 calls in a row in the first 100 instructions, with no other instructions between
    // them. This is the "windows" pattern. Another windows pattern: call to GetModuleHandleA
    // followed by a push of eax and then the call to main.  Or a call to __libc_start_main
    Address dest;

    do {
        DecodeResult inst;
        decodeSingleInstruction(addr, inst);

        if (inst.rtl == nullptr) {
            // Must have gotten out of step
            break;
        }

        const CallStatement *call = nullptr;

        if (!inst.rtl->empty()) {
            call = (inst.rtl->back()->getKind() == StmtType::Call)
                       ? static_cast<CallStatement *>(inst.rtl->back())
                       : nullptr;
        }

        const BinarySymbol *sym = (call && call->isCallToMemOffset())
                                      ? symbols->findSymbolByAddress(
                                            call->getDest()->access<Const, 1>()->getAddr())
                                      : nullptr;

        if (sym && sym->isImportedFunction() && (sym->getName() == "GetModuleHandleA")) {
            const int oldInstLength = inst.numBytes;

            if (decodeSingleInstruction(addr + oldInstLength, inst) && (inst.rtl->size() == 2)) {
                // using back instead of rtl[1], since size()==2
                const Assign *asgn = dynamic_cast<Assign *>(inst.rtl->back());

                if (asgn && (*asgn->getRight() == *Location::regOf(REG_PENT_EAX))) {
                    decodeSingleInstruction(addr + oldInstLength + inst.numBytes, inst);

                    if (!inst.rtl->empty()) {
                        CallStatement *toMain = dynamic_cast<CallStatement *>(inst.rtl->back());

                        if (toMain && (toMain->getFixedDest() != Address::INVALID)) {
                            symbols->createSymbol(toMain->getFixedDest(), "WinMain");
                            gotMain = true;
                            return toMain->getFixedDest();
                        }
                    }
                }
            }
        }

        if (call && ((dest = (call->getFixedDest())) != Address::INVALID)) {
            const QString destSym = m_program->getSymbolNameByAddr(dest);

            if (destSym == "__libc_start_main") {
                // This is a gcc 3 pattern. The first parameter will be a pointer to main.
                // Assume it's the 5 byte push immediately preceeding this instruction.
                // For newer GCCs, this is not the case. Make sure the code only fails
                // instead of crashing.
                // Note: For GCC3, the RTL has the following pattern:
                //   m[esp-4] = K
                //   esp = esp-4
                decodeSingleInstruction(prevAddr, inst);
                if (inst.valid && inst.rtl->size() == 2 && inst.rtl->front()->isAssign()) {
                    Assign *a     = static_cast<Assign *>(inst.rtl->front()); // Get m[esp-4] = K
                    SharedExp rhs = a->getRight();
                    if (rhs->isIntConst()) {
                        gotMain = true;
                        return Address(rhs->access<Const>()->getInt()); // TODO: use getAddr ?
                    }
                }
            }
        }

        prevAddr = addr;

        const GotoStatement *gs = static_cast<const GotoStatement *>(call);
        if (gs && (gs->getKind() == StmtType::Goto)) {
            // Example: Borland often starts with a branch
            // around some debug info
            addr = gs->getFixedDest();
        }
        else {
            addr += inst.numBytes;
        }
    } while (--numInstructionsLeft > 0);

    // Last chance check: look for _main (e.g. Borland programs)
    const BinarySymbol *sym = symbols->findSymbolByName("_main");

    if (sym) {
        return sym->getLocation();
    }

    // Not ideal; we must return start
    LOG_WARN("main function not found, falling back to entry point");

    if (symbols->findSymbolByAddress(start) == nullptr) {
        symbols->createSymbol(start, "_start");
    }

    return start;
}


void PentiumFrontEnd::processStringInst(UserProc *proc)
{
    StringInstructionProcessor(proc).processStringInstructions();
}


void PentiumFrontEnd::processOverlapped(UserProc *proc)
{
    // first, lets look for any uses of the registers
    std::set<RegNum> usedRegs;
    StatementList stmts;
    proc->getStatements(stmts);

    for (Statement *s : stmts) {
        LocationSet locs;
        s->addUsedLocs(locs);

        for (SharedExp l : locs) {
            if (l->isRegOfConst()) {
                usedRegs.insert(l->access<Const, 1>()->getInt());
            }
        }
    }

    std::set<BasicBlock *> bbs;

    for (Statement *s : stmts) {
        if (isOverlappedRegsProcessed(s->getBB())) { // never redo processing
            continue;
        }

        bbs.insert(s->getBB());

        if (!s->isAssignment()) {
            continue;
        }

        std::unique_ptr<RTL>
            overlapResult = m_decoder->getDict()->getRegDB()->processOverlappedRegs(
                static_cast<Assignment *>(s), usedRegs);

        if (overlapResult) {
            for (Statement *res : *overlapResult) {
                proc->insertStatementAfter(s, res->clone());
            }
        }
    }

    // set a flag for every BB we've processed so we don't do them again
    m_overlappedRegsProcessed.insert(bbs.begin(), bbs.end());
}


void PentiumFrontEnd::extraProcessCall(CallStatement *call, const RTLList &BB_rtls)
{
    if (!call->getDestProc()) {
        return;
    }

    // looking for function pointers
    auto calledSig = call->getDestProc()->getSignature();

    for (int i = 0; i < calledSig->getNumParams(); i++) {
        // check param type
        SharedType paramType = calledSig->getParamType(i);
        SharedType points_to;
        std::shared_ptr<CompoundType> compound;
        bool paramIsFuncPointer = false, paramIsCompoundWithFuncPointers = false;

        if (paramType->resolvesToPointer()) {
            points_to = paramType->as<PointerType>()->getPointsTo();

            if (points_to->resolvesToFunc()) {
                paramIsFuncPointer = true;
            }
            else if (points_to->resolvesToCompound()) {
                compound = points_to->as<CompoundType>();

                for (int n = 0; n < compound->getNumMembers(); n++) {
                    if (compound->getMemberTypeByIdx(n)->resolvesToPointer() &&
                        compound->getMemberTypeByIdx(n)
                            ->as<PointerType>()
                            ->getPointsTo()
                            ->resolvesToFunc()) {
                        paramIsCompoundWithFuncPointers = true;
                    }
                }
            }
        }

        if ((paramIsFuncPointer == false) && (paramIsCompoundWithFuncPointers == false)) {
            continue;
        }

        // count pushes backwards to find arg
        SharedExp found = nullptr;
        int pushcount   = 0;

        for (RTLList::const_reverse_iterator itr = BB_rtls.rbegin();
             itr != BB_rtls.rend() && !found; ++itr) {
            RTL *rtl = itr->get();

            for (auto rtl_iter = rtl->rbegin(); rtl_iter != rtl->rend(); ++rtl_iter) {
                Statement *stmt = *rtl_iter;

                if (stmt->isAssign()) {
                    Assign *asgn = static_cast<Assign *>(stmt);

                    if (asgn->getLeft()->isRegN(REG_PENT_ESP) &&
                        (asgn->getRight()->getOper() == opMinus)) {
                        pushcount++;
                    }
                    else if ((pushcount == i + 2) && asgn->getLeft()->isMemOf() &&
                             (asgn->getLeft()->getSubExp1()->getOper() == opMinus) &&
                             asgn->getLeft()->getSubExp1()->getSubExp1()->isRegN(REG_PENT_ESP) &&
                             asgn->getLeft()->getSubExp1()->getSubExp2()->isIntConst()) {
                        found = asgn->getRight();
                        break;
                    }
                }
            }
        }

        if (found == nullptr) {
            continue;
        }

        Address a;

        if (found->isIntConst()) {
            a = Address(found->access<Const>()->getInt());
        }
        else if (found->isAddrOf() && found->getSubExp1()->isGlobal()) {
            QString name = found->access<Const, 1, 1>()->getStr();

            if (m_program->getGlobalByName(name) == nullptr) {
                continue;
            }

            a = m_program->getGlobalAddrByName(name);
        }
        else {
            continue;
        }

        // found one.
        if (paramIsFuncPointer) {
            LOG_VERBOSE(
                "Found a new procedure at address %1 from inspecting parameters of call to '%2'.",
                a, call->getDestProc()->getName());
            Function *proc = m_program->getOrCreateFunction(a);
            auto sig       = paramType->as<PointerType>()
                           ->getPointsTo()
                           ->as<FuncType>()
                           ->getSignature()
                           ->clone();
            sig->setName(proc->getName());
            sig->setForced(true);
            proc->setSignature(sig);
            continue;
        }

        // linkers putting rodata in data sections is a continual annoyance
        // we just have to assume the pointers don't change before we pass them at least once.
        // if (!prog->isReadOnly(a))
        //    continue;

        for (int n = 0; n < compound->getNumMembers(); n++) {
            if (compound->getMemberTypeByIdx(n)->resolvesToPointer() &&
                compound->getMemberTypeByIdx(n)
                    ->as<PointerType>()
                    ->getPointsTo()
                    ->resolvesToFunc()) {
                Address d = Address(m_program->getBinaryFile()->getImage()->readNative4(a));
                LOG_VERBOSE(
                    "Found a new procedure at address %1 from inspecting parameters of call to %2",
                    d, call->getDestProc()->getName());

                Function *proc = m_program->getOrCreateFunction(d);
                auto sig       = compound->getMemberTypeByIdx(n)
                               ->as<PointerType>()
                               ->getPointsTo()
                               ->as<FuncType>()
                               ->getSignature()
                               ->clone();
                sig->setName(proc->getName());
                sig->setForced(true);
                proc->setSignature(sig);
            }

            a += compound->getMemberTypeByIdx(n)->getSize() / 8;
        }
    }

    // some Pentium specific ellipsis processing
    if (calledSig->hasEllipsis()) {
        // count pushes backwards to find a push of 0
        bool found    = false;
        int pushcount = 0;

        for (RTLList::const_reverse_iterator itr = BB_rtls.rbegin();
             itr != BB_rtls.rend() && !found; ++itr) {
            RTL *rtl = itr->get();

            for (auto rtl_iter = rtl->rbegin(); rtl_iter != rtl->rend(); ++rtl_iter) {
                Statement *stmt = *rtl_iter;

                if (stmt->isAssign()) {
                    Assign *asgn = static_cast<Assign *>(stmt);

                    if (asgn->getLeft()->isRegN(REG_PENT_ESP) &&
                        (asgn->getRight()->getOper() == opMinus)) {
                        pushcount++;
                    }
                    else if (asgn->getLeft()->isMemOf() &&
                             (asgn->getLeft()->getSubExp1()->getOper() == opMinus) &&
                             asgn->getLeft()->getSubExp1()->getSubExp1()->isRegN(REG_PENT_ESP) &&
                             asgn->getLeft()->getSubExp1()->getSubExp2()->isIntConst()) {
                        if (asgn->getRight()->isIntConst()) {
                            int n = asgn->getRight()->access<Const>()->getInt();

                            if (n == 0) {
                                found = true;
                                break;
                            }
                        }
                    }
                }
            }
        }

        if (found && (pushcount > 1)) {
            call->setSigArguments();
            call->setNumArguments(pushcount - 1);
        }
    }
}
