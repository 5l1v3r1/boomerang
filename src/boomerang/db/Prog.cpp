#pragma region License
/*
 * This file is part of the Boomerang Decompiler.
 *
 * See the file "LICENSE.TERMS" for information on usage and
 * redistribution of this file, and for a DISCLAIMER OF ALL
 * WARRANTIES.
 */
#pragma endregion License
#include "Prog.h"


#include "boomerang/core/Boomerang.h"
#include "boomerang/core/Project.h"
#include "boomerang/c/ansi-c-parser.h"
#include "boomerang/codegen/ICodeGenerator.h"

#include "boomerang/db/Module.h"
#include "boomerang/db/CFG.h"
#include "boomerang/db/Global.h"
#include "boomerang/db/proc/UserProc.h"
#include "boomerang/db/Register.h"
#include "boomerang/db/RTL.h"
#include "boomerang/db/binary/BinarySymbolTable.h"
#include "boomerang/db/binary/BinaryImage.h"
#include "boomerang/db/binary/BinaryFile.h"
#include "boomerang/db/Signature.h"
#include "boomerang/db/exp/Const.h"
#include "boomerang/db/exp/Terminal.h"
#include "boomerang/db/exp/Location.h"
#include "boomerang/db/proc/LibProc.h"
#include "boomerang/passes/PassManager.h"
#include "boomerang/type/type/ArrayType.h"
#include "boomerang/type/type/CharType.h"
#include "boomerang/type/type/PointerType.h"
#include "boomerang/type/type/SizeType.h"
#include "boomerang/type/type/FloatType.h"
#include "boomerang/type/type/FuncType.h"
#include "boomerang/type/type/VoidType.h"
#include "boomerang/type/type/BooleanType.h"
#include "boomerang/util/Log.h"
#include "boomerang/util/Types.h"
#include "boomerang/util/Util.h" // For lockFileWrite etc
#include "boomerang/type/type/IntegerType.h"
#include "boomerang/frontend/Frontend.h"

#include <QtCore/QFileInfo>
#include <QtCore/QSaveFile>
#include <QtCore/QDir>
#include <QtCore/QString>

#include <cassert>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <vector>
#include <algorithm>
#include <cmath>
#include <queue>
#include <unordered_set>

#ifdef _WIN32
#  include <windows.h>
#  ifndef __MINGW32__
namespace dbghelp
#  endif
{
#  include <dbghelp.h>
}
#endif

#include <sys/types.h>


Prog::Prog(const QString& name, Project *project)
    : m_name(name)
    , m_project(project)
    , m_binaryFile(project ? project->getLoadedBinaryFile() : nullptr)
    , m_defaultFrontend(nullptr)
{
    m_rootModule    = getOrInsertModule(getName());
    assert(m_rootModule != nullptr);
}


Prog::~Prog()
{
}


Module *Prog::getOrInsertModule(const QString& name, const ModuleFactory& fact, IFrontEnd *frontEnd)
{
    for (const auto& m : m_moduleList) {
        if (m->getName() == name) {
            return m.get();
        }
    }

    Module *m = fact.create(name, this, frontEnd ? frontEnd : m_defaultFrontend);
    m_moduleList.push_back(std::unique_ptr<Module>(m));
    return m;
}


void Prog::setFrontEnd(IFrontEnd *frontEnd)
{
    if (m_defaultFrontend) {
        delete m_defaultFrontend;
        m_defaultFrontend = nullptr;
    }

    m_defaultFrontend = frontEnd;

    m_moduleList.clear();
    m_rootModule = getOrInsertModule(m_name);
}


void Prog::setName(const QString& name)
{
    m_name = name;
    if (m_rootModule) {
        m_rootModule->setName(name);
    }
}


bool Prog::isWellFormed() const
{
    bool wellformed = true;

    for (const auto& module : m_moduleList) {
        for (Function *func : *module) {
            if (!func->isLib()) {
                UserProc *proc = static_cast<UserProc *>(func);
                wellformed &= proc->getCFG()->isWellFormed();
            }
        }
    }

    return wellformed;
}


void Prog::finishDecode()
{
    for (const auto& module : m_moduleList) {
        for (Function *func : *module) {
            if (func->isLib()) {
                continue;
            }

            UserProc *p = static_cast<UserProc *>(func);

            if (!p->isDecoded()) {
                continue;
            }

            p->assignProcsToCalls();
            p->finalSimplify();
        }
    }
}


bool Prog::isModuleUsed(Module *c) const
{
    // TODO: maybe module can have no procedures and still be used ?
    return !c->empty();
}


Module *Prog::getModuleForSymbol(const QString& symbolName)
{
    QString             sourceFileName;
    const BinarySymbol *sym = nullptr;
    if (m_binaryFile) {
        sym = m_binaryFile->getSymbols()->findSymbolByName(symbolName);
    }

    if (sym) {
        sourceFileName = sym->belongsToSourceFile();
    }

    if (sourceFileName.isEmpty() || !sourceFileName.endsWith(".c")) {
        return m_rootModule;
    }

    LOG_VERBOSE("Got filename '%1' for symbol '%2'", sourceFileName, symbolName);
    QString moduleName = sourceFileName;
    moduleName.chop(2); // remove .c

    Module *module = findModule(moduleName);

    if (module == nullptr) {
        module = getOrInsertModule(moduleName);
        m_rootModule->addChild(module);
    }

    return module;
}


Function *Prog::createFunction(Address startAddress)
{
    // this test fails when decoding sparc, why?  Please investigate - trent
    // Likely because it is in the Procedure Linkage Table (.plt), which for Sparc is in the data section
    // assert(startAddress >= limitTextLow && startAddress < limitTextHigh);

    // Check if we already have this proc
    Function *existingFunction = findFunction(startAddress);

    if (existingFunction) {      // Exists already ?
        return existingFunction; // Yes, we are done
    }

    Address other = m_binaryFile->getJumpTarget(startAddress);

    if (other != Address::INVALID) {
        startAddress = other;
    }

    existingFunction = findFunction(startAddress);

    if (existingFunction) {      // Exists already ?
        return existingFunction; // Yes, we are done
    }

    QString             procName;
    const BinarySymbol *sym = m_binaryFile->getSymbols()->findSymbolByAddress(startAddress);
    bool                isLibFunction = false;

    if (sym) {
        isLibFunction = sym->isImportedFunction() || sym->isStaticFunction();
        procName      = sym->getName();
    }

    if (procName.isEmpty()) {
        // No name. Give it the name of the start address.
        procName = QString("proc_%1").arg(startAddress.toString());
        LOG_VERBOSE("Assigning name %1 to address %2", procName, startAddress);
    }

    return m_rootModule->createFunction(procName, startAddress, isLibFunction);
}


#if defined(_WIN32) && !defined(__MINGW32__)

SharedType typeFromDebugInfo(int index, DWORD64 ModBase);

SharedType makeUDT(int index, DWORD64 ModBase)
{
    HANDLE hProcess = GetCurrentProcess();
    WCHAR  *name;

    BOOL gotType = dbghelp::SymGetTypeInfo(hProcess, ModBase, index, dbghelp::TI_GET_SYMNAME, &name);
    if (!gotType) {
        return nullptr;
    }

    char nameA[1024];
    WideCharToMultiByte(CP_ACP, 0, name, -1, nameA, sizeof(nameA), 0, nullptr);
    SharedType ty = Type::getNamedType(nameA);

    if (ty) {
        return NamedType::get(nameA);
    }

    auto  cty   = CompoundType::get();
    DWORD count = 0;
    dbghelp::SymGetTypeInfo(hProcess, ModBase, index, dbghelp::TI_GET_CHILDRENCOUNT, &count);
    int FindChildrenSize = sizeof(dbghelp::TI_FINDCHILDREN_PARAMS) + count * sizeof(ULONG);
    dbghelp::TI_FINDCHILDREN_PARAMS *pFC = (dbghelp::TI_FINDCHILDREN_PARAMS *)malloc(FindChildrenSize);
    memset(pFC, 0, FindChildrenSize);
    pFC->Count = count;
    SymGetTypeInfo(hProcess, ModBase, index, dbghelp::TI_FINDCHILDREN, pFC);

    for (unsigned int i = 0; i < count; i++) {
        char fieldName[1024];
        dbghelp::SymGetTypeInfo(hProcess, ModBase, pFC->ChildId[i], dbghelp::TI_GET_SYMNAME, &name);
        WideCharToMultiByte(CP_ACP, 0, name, -1, fieldName, sizeof(fieldName), 0, nullptr);
        DWORD mytype;
        dbghelp::SymGetTypeInfo(hProcess, ModBase, pFC->ChildId[i], dbghelp::TI_GET_TYPE, &mytype);
        cty->addType(typeFromDebugInfo(mytype, ModBase), fieldName);
    }

    Type::addNamedType(nameA, cty);
    ty = Type::getNamedType(nameA);
    assert(ty);
    return NamedType::get(nameA);

}


SharedType typeFromDebugInfo(int index, DWORD64 ModBase)
{
    HANDLE hProcess = GetCurrentProcess();

    int     got;
    DWORD   d;
    ULONG64 lsz = 0;

    got = dbghelp::SymGetTypeInfo(hProcess, ModBase, index, dbghelp::TI_GET_SYMTAG, &d);
    assert(got);
    got = dbghelp::SymGetTypeInfo(hProcess, ModBase, index, dbghelp::TI_GET_LENGTH, &lsz);
    int sz = (int)lsz * 8; // bits

    switch (d)
    {
    case 11:
        return makeUDT(index, ModBase);

    case 13:
        // TODO: signature
        return FuncType::get();

    case 14:
        got = dbghelp::SymGetTypeInfo(hProcess, ModBase, index, dbghelp::TI_GET_TYPE, &d);
        assert(got);
        return PointerType::get(typeFromDebugInfo(d, ModBase));

    case 15:
        got = dbghelp::SymGetTypeInfo(hProcess, ModBase, index, dbghelp::TI_GET_TYPE, &d);
        assert(got);
        got = dbghelp::SymGetTypeInfo(hProcess, ModBase, index, dbghelp::TI_GET_LENGTH, &lsz);
        assert(got);
        return ArrayType::get(typeFromDebugInfo(d, ModBase), (unsigned)lsz);

        break;

    case 16:
        got = dbghelp::SymGetTypeInfo(hProcess, ModBase, index, dbghelp::TI_GET_BASETYPE, &d);
        assert(got);

        switch (d)
        {
        case 1:
            return VoidType::get();

        case 2:
            return CharType::get();

        case 3:
            return CharType::get();

        case 6:  // int
        case 13: // long
            return IntegerType::get(sz, 1);

        case 7:  // unsigned int
        case 14: // ulong
            return IntegerType::get(sz, -1);

        case 8:
            return FloatType::get(sz);

        case 10:
            return BooleanType::get();

        default:
            LOG_FATAL("Unhandled base type %1", (int)d);
        }

        break;

    default:
        LOG_FATAL("Unhandled symtag %1", (int)d);
    }

    return nullptr;
}


int debugRegister(int r)
{
    switch (r)
    {
    case 2:
        return 26; // edx

    case 4:
        return 25; // ecx

    case 8:
        return 29; // ebp
    }

    assert(false);
    return -1;
}


BOOL CALLBACK addSymbol(dbghelp::PSYMBOL_INFO symInfo, ULONG /*SymbolSize*/, PVOID UserContext)
{
    Function *proc = static_cast<Function *>(UserContext);

    if (symInfo->Flags & SYMFLAG_PARAMETER) {
        SharedType ty = typeFromDebugInfo(symInfo->TypeIndex, symInfo->ModBase);

        if (symInfo->Flags & SYMFLAG_REGREL) {
            assert(symInfo->Register == 8); // ebp
            proc->getSignature()->addParameter(
                ty, symInfo->Name,
                Location::memOf(Binary::get(opPlus, Location::regOf(28), Const::get((int)symInfo->Address - 4))));
        }
        else if (symInfo->Flags & SYMFLAG_REGISTER) {
            proc->getSignature()->addParameter(ty, symInfo->Name, Location::regOf(debugRegister(symInfo->Register)));
        }
    }
    else if ((symInfo->Flags & SYMFLAG_LOCAL) && !proc->isLib()) {
        UserProc *uproc = static_cast<UserProc *>(proc);
        assert(symInfo->Flags & SYMFLAG_REGREL);
        assert(symInfo->Register == 8);
        SharedExp memref =
            Location::memOf(Binary::get(opMinus, Location::regOf(28), Const::get(-((int)symInfo->Address - 4))));
        SharedType ty = typeFromDebugInfo(symInfo->TypeIndex, symInfo->ModBase);
        uproc->addLocal(ty, symInfo->Name, memref);
    }

    return TRUE;
}

#endif


void Prog::removeFunction(const QString& name)
{
    Function *function = findFunction(name);

    if (function) {
        function->removeFromModule();
        Boomerang::get()->alertFunctionRemoved(function);
        // FIXME: this function removes the function from module, but it leaks it
    }
}


Module *Prog::createModule(const QString& name, Module *parentModule, const ModuleFactory& factory)
{
    if (parentModule == nullptr) {
        parentModule = m_rootModule;
    }

    Module *module = m_rootModule->find(name);

    if (module && (module->getUpstream() == parentModule)) {
        // a module already exists
        return nullptr;
    }

    module = factory.create(name, this, this->getFrontEnd());
    parentModule->addChild(module);
    m_moduleList.push_back(std::unique_ptr<Module>(module));
    return module;
}


int Prog::getNumFunctions(bool userOnly) const
{
    int n = 0;

    if (userOnly) {
        for (const auto& m : m_moduleList) {
            for (Function *proc : *m) {
                if (!proc->isLib()) {
                    n++;
                }
            }
        }
    }
    else {
        for (const auto& module : m_moduleList) {
            n += module->size();
        }
    }

    return n;
}


Function *Prog::findFunction(Address entryAddr) const
{
    for (const auto& m : m_moduleList) {
        Function *proc = m->getFunction(entryAddr);

        if (proc != nullptr) {
            assert(proc != reinterpret_cast<Function *>(-1));
            return proc;
        }
    }

    return nullptr;
}


Function *Prog::findFunction(const QString& name) const
{
    for (const auto& module : m_moduleList) {
        Function *f = module->getFunction(name);

        if (f) {
            assert(f != reinterpret_cast<Function*>(-1));
            return f;
        }
    }

    return nullptr;
}


LibProc *Prog::getOrCreateLibraryProc(const QString& name)
{
    Function *existingProc = findFunction(name);

    if (existingProc && existingProc->isLib()) {
        return static_cast<LibProc *>(existingProc);
    }

    return static_cast<LibProc *>(m_rootModule->createFunction(name, Address::INVALID, true));
}


Platform Prog::getFrontEndId() const
{
    return m_defaultFrontend->getType();
}


std::shared_ptr<Signature> Prog::getDefaultSignature(const char *name) const
{
    return m_defaultFrontend->getDefaultSignature(name);
}


bool Prog::isWin32() const
{
    return m_defaultFrontend && m_defaultFrontend->isWin32();
}


QString Prog::getGlobalName(Address uaddr) const
{
    // FIXME: inefficient
    for (auto& glob : m_globals) {
        if (glob->containsAddress(uaddr)) {
            return glob->getName();
        }
    }

    return getSymbolNameByAddress(uaddr);
}


Address Prog::getGlobalAddr(const QString& name) const
{
    Global *glob = getGlobal(name);

    if (glob) {
        return glob->getAddress();
    }

    auto symbol = m_binaryFile->getSymbols()->findSymbolByName(name);
    return symbol ? symbol->getLocation() : Address::INVALID;
}


Global *Prog::getGlobal(const QString& name) const
{
    auto iter = std::find_if(m_globals.begin(), m_globals.end(),
        [&name](const std::shared_ptr<Global>& g) -> bool {
            return g->getName() == name;
        });

    return iter != m_globals.end() ? iter->get() : nullptr;
}


bool Prog::markGlobalUsed(Address uaddr, SharedType knownType)
{
    for (auto& glob : m_globals) {
        if (glob->containsAddress(uaddr)) {
            if (knownType) {
                glob->meetType(knownType);
            }

            return true;
        }
    }

    if (m_binaryFile->getImage()->getSectionByAddr(uaddr) == nullptr) {
        LOG_VERBOSE("Refusing to create a global at address %1 "
                    "that is in no known section of the binary", uaddr);
        return false;
    }

    QString    name = newGlobalName(uaddr);
    SharedType ty;

    if (knownType) {
        ty = knownType;

        if (ty->resolvesToArray() && ty->as<ArrayType>()->isUnbounded()) {
            SharedType baseType = ty->as<ArrayType>()->getBaseType();
            int        baseSize = 0;

            if (baseType) {
                baseSize = baseType->getSizeInBytes();
            }

            auto symbol = m_binaryFile->getSymbols()->findSymbolByName(name);
            int  sz     = symbol ? symbol->getSize() : 0;

            if (sz && baseSize) {
                // Note: since ty is a pointer and has not been cloned, this will also set the type for knownType
                ty->as<ArrayType>()->setLength(sz / baseSize);
            }
        }
    }
    else {
        ty = guessGlobalType(name, uaddr);
    }

    m_globals.insert(std::make_shared<Global>(ty, uaddr, name, this));

    LOG_VERBOSE("globalUsed: name %1, address %2, %3 type %4",
                name, uaddr, knownType ? "known" : "guessed", ty->getCtype());

    return true;
}


std::shared_ptr<ArrayType> Prog::makeArrayType(Address startAddr, SharedType baseType)
{
    QString name = newGlobalName(startAddr);

    // TODO: fix the case of missing symbol table interface
    auto symbol = m_binaryFile->getSymbols()->findSymbolByName(name);

    if (!symbol || (symbol->getSize() == 0)) {
        return ArrayType::get(baseType); // An "unbounded" array
    }

    size_t size   = symbol->getSize();
    int elemSize = baseType->getSizeInBytes();
    if (elemSize < 1) {
        elemSize = 1;
    }

    return ArrayType::get(baseType, size / elemSize);
}


SharedType Prog::guessGlobalType(const QString& globalName, Address globAddr) const
{
#if defined(_WIN32) && !defined(__MINGW32__)
    HANDLE               hProcess = GetCurrentProcess();
    dbghelp::SYMBOL_INFO *sym     = (dbghelp::SYMBOL_INFO *)malloc(sizeof(dbghelp::SYMBOL_INFO) + 1000);
    sym->SizeOfStruct = sizeof(*sym);
    sym->MaxNameLen   = 1000;
    sym->Name[0]      = 0;
    BOOL got = dbghelp::SymFromAddr(hProcess, globAddr.value(), 0, sym);

    if (got && *sym->Name && sym->TypeIndex) {
        assert(globalName == sym->Name);
        return typeFromDebugInfo(sym->TypeIndex, sym->ModBase);
    }
#endif
    auto symbol = m_binaryFile->getSymbols()->findSymbolByName(globalName);
    int  sz     = symbol ? symbol->getSize() : 0;

    if (sz == 0) {
        // Check if it might be a string
        const char *str = getStringConstant(globAddr);

        if (str) {
            // return char* and hope it is dealt with properly
            return PointerType::get(CharType::get());
        }
    }

    SharedType ty;

    switch (sz)
    {
    case 1:
    case 2:
    case 4:
    case 8:
        ty = IntegerType::get(sz * 8);
        break;

    default:
        ty = std::make_shared<ArrayType>(std::make_shared<CharType>(), sz);
    }

    return ty;
}


QString Prog::newGlobalName(Address uaddr)
{
    QString globalName = getGlobalName(uaddr);

    if (!globalName.isEmpty()) {
        return globalName;
    }

    globalName = QString("global%1_%2").arg(m_globals.size()).arg(uaddr.value(), 0, 16);
    LOG_VERBOSE("Naming new global '%1' at address %2", globalName, uaddr);
    return globalName;
}


SharedType Prog::getGlobalType(const QString& name) const
{
    for (auto& global : m_globals) {
        if (global->getName() == name) {
            return global->getType();
        }
    }

    return nullptr;
}


void Prog::setGlobalType(const QString& name, SharedType ty)
{
    // FIXME: inefficient
    for (auto& gl : m_globals) {
        if (gl->getName() == name) {
            gl->setType(ty);
            return;
        }
    }
}


const char *Prog::getStringConstant(Address uaddr, bool knownString /* = false */) const
{
    const BinarySection *si = m_binaryFile->getImage()->getSectionByAddr(uaddr);

    // Too many compilers put constants, including string constants, into read/write sections
    // if (si && si->bReadOnly)
    if (si && !si->isAddressBss(uaddr)) {
        // At this stage, only support ascii, null terminated, non unicode strings.
        // At least 4 of the first 6 chars should be printable ascii
        char *p = reinterpret_cast<char *>((si->getHostAddr() - si->getSourceAddr() + uaddr).value());

        if (knownString) {
            // No need to guess... this is hopefully a known string
            return p;
        }

        int  printable = 0;
        char last      = 0;

        for (int i = 0; i < 6; i++) {
            char c = p[i];

            if (c == 0) {
                break;
            }

            if ((c >= ' ') && (c < '\x7F')) {
                printable++;
            }

            last = c;
        }

        if (printable >= 4) {
            return p;
        }

        // Just a hack while type propagations are not yet ready
        if ((last == '\n') && (printable >= 2)) {
            return p;
        }
    }

    return nullptr;
}


double Prog::getFloatConstant(Address uaddr, bool& ok, int bits) const
{
    ok = true;
    const BinarySection *si = m_binaryFile->getImage()->getSectionByAddr(uaddr);

    if (si && si->isReadOnly()) {
        if (bits == 64) { // TODO: handle 80bit floats ?
            return m_binaryFile->getImage()->readNativeFloat8(uaddr);
        }
        else {
            assert(bits == 32);
            return m_binaryFile->getImage()->readNativeFloat4(uaddr);
        }
    }

    ok = false;
    return 0.0;
}


QString Prog::getSymbolNameByAddress(Address dest) const
{
    auto sym = m_binaryFile->getSymbols()->findSymbolByAddress(dest);

    return sym ? sym->getName() : "";
}


const BinarySection *Prog::getSectionByAddr(Address a) const
{
    return m_binaryFile->getImage()->getSectionByAddr(a);
}


Address Prog::getLimitTextLow() const
{
    return m_binaryFile->getImage()->getLimitTextLow();
}


Address Prog::getLimitTextHigh() const
{
    return m_binaryFile->getImage()->getLimitTextHigh();
}


bool Prog::isReadOnly(Address a) const
{
    return m_binaryFile->getImage()->isReadOnly(a);
}


int Prog::readNative1(Address a) const
{
    return m_binaryFile->getImage()->readNative1(a);
}


int Prog::readNative2(Address a) const
{
    return m_binaryFile->getImage()->readNative2(a);
}


int Prog::readNative4(Address a) const
{
    return m_binaryFile->getImage()->readNative4(a);
}


void Prog::decodeEntryPoint(Address entryAddr)
{
    Function *func = findFunction(entryAddr);

    if (!func || (!func->isLib() && !static_cast<UserProc *>(func)->isDecoded())) {
        if (!Util::inRange(entryAddr, m_binaryFile->getImage()->getLimitTextLow(), m_binaryFile->getImage()->getLimitTextHigh())) {
            LOG_WARN("Attempt to decode entrypoint at address %1 outside text area", entryAddr);
            return;
        }

        m_defaultFrontend->decode(entryAddr);
        finishDecode();
    }


    if (!func) {
        func = findFunction(entryAddr);

        // Chek if there is a library thunk at entryAddr
        if (!func) {
            Address jumpTarget = m_binaryFile->getJumpTarget(entryAddr);

            if (jumpTarget != Address::INVALID) {
                func = findFunction(jumpTarget);
            }
        }
    }

    assert(func);

    if (!func->isLib()) { // -sf procs marked as __nodecode are treated as library procs (?)
        m_entryProcs.push_back(static_cast<UserProc *>(func));
    }
}


void Prog::addEntryPoint(Address entryAddr)
{
    Function *func = findFunction(entryAddr);
    if (!func) {
        func = createFunction(entryAddr);
    }

    if (func && !func->isLib()) {
        m_entryProcs.push_back(static_cast<UserProc *>(func));
    }
}


bool Prog::isDynamicLinkedProcPointer(Address dest) const
{
    auto sym = m_binaryFile->getSymbols()->findSymbolByAddress(dest);
    return sym && sym->isImportedFunction();
}


const QString& Prog::getDynamicProcName(Address addr) const
{
    static QString dyn("dynamic");
    auto           sym = m_binaryFile->getSymbols()->findSymbolByAddress(addr);

    return sym ? sym->getName() : dyn;
}


void Prog::decompile()
{
    assert(!m_moduleList.empty());
    getNumFunctions();
    LOG_VERBOSE("%1 procedures", getNumFunctions(false));

    // Start decompiling each entry point
    for (UserProc *up : m_entryProcs) {
        LOG_VERBOSE("Decompiling entry point '%1'", up->getName());
        up->decompile();
    }

    // Just in case there are any Procs not in the call graph.

    if (SETTING(decodeMain) && SETTING(decodeChildren)) {
        bool foundone = true;

        while (foundone) {
            foundone = false;

            for (const auto& module : m_moduleList) {
                for (Function *pp : *module) {
                    if (pp->isLib()) {
                        continue;
                    }

                    UserProc *proc = static_cast<UserProc *>(pp);

                    if (proc->isDecompiled()) {
                        continue;
                    }
                    proc->decompile();
                    foundone = true;
                }
            }
        }
    }

    globalTypeAnalysis();

    if (SETTING(decompile) && SETTING(removeReturns)) {
        // A final pass to remove returns not used by any caller
        LOG_VERBOSE("Performing global removal of unused returns...");

        // Repeat until no change. Not 100% sure if needed.
        while (removeUnusedReturns()) {
        }
    }

    globalTypeAnalysis();

    // Now it is OK to transform out of SSA form
    fromSSAForm();
    removeUnusedGlobals();
    LOG_MSG("Decompilation finished.");
}


void Prog::removeUnusedGlobals()
{
    LOG_MSG("Removing unused global variables...");

    // seach for used globals
    std::list<SharedExp> usedGlobals;

    for (const auto& module : m_moduleList) {
        for (Function *func : *module) {
            if (func->isLib()) {
                continue;
            }

            UserProc *proc = static_cast<UserProc *>(func);
            Location search(opGlobal, Terminal::get(opWild), proc);
            // Search each statement in u, excepting implicit assignments (their uses don't count, since they don't really
            // exist in the program representation)
            StatementList           stmts;
            StatementList::iterator ss;
            proc->getStatements(stmts);

            for (Statement *s : stmts) {
                if (s->isImplicit()) {
                    continue; // Ignore the uses in ImplicitAssigns
                }

                bool found = s->searchAll(search, usedGlobals);

                if (found && DEBUG_UNUSED) {
                    LOG_VERBOSE("A global is used by stmt %1", s->getNumber());
                }
            }
        }
    }

    // make a map to find a global by its name (could be a global var too)
    QMap<QString, std::shared_ptr<Global>> namedGlobals;

    for (auto& g : m_globals) {
        namedGlobals[g->getName()] = g;
    }

    // Rebuild the globals vector. Delete the unused globals only after re-inserting them
    std::set<std::shared_ptr<Global>> oldGlobals = m_globals;
    m_globals.clear();

    for (const SharedExp& e : usedGlobals) {
        if (DEBUG_UNUSED) {
            LOG_MSG(" %1 is used", e);
        }

        QString name(e->access<Const, 1>()->getStr());
        auto& usedGlobal = namedGlobals[name];

        if (usedGlobal) {
            m_globals.insert(usedGlobal);
        }
        else {
            LOG_WARN("An expression refers to a nonexistent global");
        }
    }
}


bool Prog::removeUnusedReturns()
{
    // For each UserProc. Each proc may process many others, so this may duplicate some work. Really need a worklist of
    // procedures not yet processed.
    // Define a workset for the procedures who have to have their returns checked
    // This will be all user procs, except those undecoded (-sf says just trust the given signature)
    std::set<UserProc *> removeRetSet;
    bool                 change = false;

    for (const auto& module : m_moduleList) {
        for (Function *pp : *module) {
            UserProc *proc = dynamic_cast<UserProc *>(pp);

            if ((nullptr == proc) || !proc->isDecoded()) {
                continue; // e.g. use -sf file to just prototype the proc
            }

            removeRetSet.insert(proc);
        }
    }

    // The workset is processed in arbitrary order. May be able to do better, but note that sometimes changes propagate
    // down the call tree (no caller uses potential returns for child), and sometimes up the call tree (removal of
    // returns and/or dead code removes parameters, which affects all callers).
    while (!removeRetSet.empty()) {
        auto it = removeRetSet.begin(); // Pick the first element of the set
        const bool removedReturns = (*it)->removeRedundantReturns(removeRetSet);
        if (removedReturns) {
            // Removing returns changes the uses of the callee.
            // So we have to do type analyis to update the use information.
            PassManager::get()->executePass(PassID::LocalTypeAnalysis, *it);
        }
        change |= removedReturns;

        // Note: removing the currently processed item here should prevent unnecessary reprocessing of self recursive
        // procedures
        removeRetSet.erase(it); // Remove the current element (may no longer be the first)
    }

    return change;
}


void Prog::fromSSAForm()
{
    LOG_MSG("Transforming from SSA form ...");

    for (const auto& module : m_moduleList) {
        for (Function *pp : *module) {
            if (pp->isLib()) {
                continue;
            }

            UserProc *proc = static_cast<UserProc *>(pp);

            if (SETTING(verboseOutput) && !SETTING(dotFile).isEmpty()) {
                proc->printDFG();
            }

            PassManager::get()->executePass(PassID::FromSSAForm, proc);
        }
    }
}


void Prog::globalTypeAnalysis()
{
    if (DEBUG_TA) {
        LOG_VERBOSE("### Start global data-flow-based type analysis ###");
    }

    for (const auto& module : m_moduleList) {
        for (Function *pp : *module) {
            UserProc *proc = dynamic_cast<UserProc *>(pp);

            if (!proc || !proc->isDecoded()) {
                continue;
            }

            // FIXME: this just does local TA again. Need to meet types for all parameter/arguments, and return/results!
            // This will require a repeat until no change loop
            LOG_VERBOSE("Global type analysis for '%1'", proc->getName());
            PassManager::get()->executePass(PassID::LocalTypeAnalysis, proc);
        }
    }

    if (DEBUG_TA) {
        LOG_VERBOSE("### End type analysis ###");
    }
}


void Prog::printCallGraph(const QString &filename) const
{
    QSaveFile saveFile(Boomerang::get()->getSettings()->getOutputDirectory().absoluteFilePath(filename));

    if (!saveFile.open(QFile::WriteOnly)) {
        LOG_ERROR("Cannot open output file '%1' for callgraph output", filename);
        return;
    }

    QTextStream ost(&saveFile);

    ost << "digraph callgraph\n";
    ost << "{\n";

    std::queue<Function *>           procList;
    std::unordered_set<Function *>   seen;

    for (Function *entry : m_entryProcs) {
        // We have to explicitly write entry procedures here
        // because not every entry procedure has callees (e.g. hello world main)
        ost << "    " << entry->getName() << ";\n";
        procList.push(entry);
    }

    while (!procList.empty()) {
        Function *currentProc = procList.front();
        procList.pop();

        if (currentProc == reinterpret_cast<Function *>(-1)) {
            continue;
        }
        else if (seen.find(currentProc) != seen.end()) {
            continue; // already processed
        }
        seen.insert(currentProc);

        UserProc *up = static_cast<UserProc *>(currentProc);

        for (Function *callee : up->getCallees()) {
            ost << "    " << up->getName() << " -> " << callee->getName() << ";\n";

            if (seen.find(callee) == seen.end() && !callee->isLib()) {
                procList.push(callee);
            }
        }
    }

    ost << "}\n";

    ost.flush();
    saveFile.commit();
}


void printProcsRecursive(Function *function, int indent, QTextStream& f, std::set<Function *>& seen)
{
    bool fisttime = false;

    if (seen.find(function) == seen.end()) {
        seen.insert(function);
        fisttime = true;
    }

    for (int i = 0; i < indent; i++) {
        f << "     ";
    }

    if (!function->isLib() && fisttime) { // seen lib proc
        f << function->getEntryAddress();
        f << " __nodecode __incomplete void " << function->getName() << "();\n";

        UserProc *proc = static_cast<UserProc *>(function);

        for (Function *callee : proc->getCallees()) {
            printProcsRecursive(callee, indent + 1, f, seen);
        }

        for (int i = 0; i < indent; i++) {
            f << "     ";
        }

        f << "// End of " << function->getName() << "\n";
    }
    else {
        f << "// " << function->getName() << "();\n";
    }
}


void Prog::updateLibrarySignatures()
{
    for (const auto& m : m_moduleList) {
        m->updateLibrarySignatures();
    }
}


Machine Prog::getMachine() const
{
    return m_binaryFile->getMachine();
}


void Prog::printSymbolsToFile() const
{
    LOG_VERBOSE("Entering Prog::printSymbolsToFile");
    QString   fname = Boomerang::get()->getSettings()->getOutputDirectory().absoluteFilePath("symbols.h");
    QSaveFile tgt(fname);

    if (!tgt.open(QFile::WriteOnly)) {
        LOG_ERROR("Cannot open %1 for writing", fname);
        return;
    }

    QTextStream f(&tgt);

    /* Print procs */
    f << "/* Functions: */\n";
    std::set<Function *> seen;

    for (UserProc *up : m_entryProcs) {
        printProcsRecursive(up, 0, f, seen);
    }

    f << "/* Leftovers: */\n";

    for (const auto& m : m_moduleList) {
        for (Function *pp : *m) {
            if (!pp->isLib() && (seen.find(pp) == seen.end())) {
                printProcsRecursive(pp, 0, f, seen);
            }
        }
    }

    f.flush();
    tgt.commit();
    LOG_VERBOSE("Leaving Prog::printSymbolsToFile");
}


Module *Prog::findModule(const QString& name) const
{
    return m_rootModule->find(name);
}


void Prog::readSymbolFile(const QString& fname)
{
    std::unique_ptr<AnsiCParser> parser = nullptr;

    try {
        parser.reset(new AnsiCParser(qPrintable(fname), false));
    }
    catch (const char *) {
        LOG_ERROR("Cannot read symbol file '%1'", fname);
        return;
    }

    Platform plat = getFrontEndId();
    CallConv cc   = isWin32() ? CallConv::Pascal : CallConv::C;

    parser->yyparse(plat, cc);
    Module *targetModule = getRootModule();

    for (Symbol *sym : parser->symbols) {
        if (sym->sig) {
            QString name = sym->sig->getName();
            targetModule = getModuleForSymbol(name);
            auto bin_sym       = m_binaryFile->getSymbols()->findSymbolByAddress(sym->addr);
            bool do_not_decode = (bin_sym && bin_sym->isImportedFunction()) ||
                                 // NODECODE isn't really the right modifier; perhaps we should have a LIB modifier,
                                 // to specifically specify that this function obeys library calling conventions
                                 sym->mods->noDecode;
            Function *p = targetModule->createFunction(name, sym->addr, do_not_decode);

            if (!sym->mods->incomplete) {
                p->setSignature(sym->sig->clone());
                p->getSignature()->setForced(true);
            }
        }
        else {
            QString name = sym->name;

            if (sym->name.isEmpty()) {
                name = newGlobalName(sym->addr);
            }

            SharedType ty = sym->ty;

            if (ty == nullptr) {
                ty = guessGlobalType(name, sym->addr);
            }

            m_globals.insert(std::make_shared<Global>(ty, sym->addr, name, this));
        }
    }

    for (SymbolRef *ref : parser->refs) {
        m_defaultFrontend->addRefHint(ref->m_addr, ref->m_name);
    }
}


SharedExp Prog::readNativeAs(Address uaddr, SharedType type) const
{
    SharedExp            e   = nullptr;
    const BinarySection *si = getSectionByAddr(uaddr);

    if (si == nullptr) {
        return nullptr;
    }

    if (type->resolvesToPointer()) {
        Address init = Address(readNative4(uaddr));

        if (init.isZero()) {
            return Const::get(0);
        }

        QString name = getGlobalName(init);

        if (!name.isEmpty()) {
            // TODO: typecast?
            return Location::global(name, nullptr);
        }

        if (type->as<PointerType>()->getPointsTo()->resolvesToChar()) {
            const char *str = getStringConstant(init);

            if (str != nullptr) {
                return Const::get(str);
            }
        }
    }

    if (type->resolvesToCompound()) {
        std::shared_ptr<CompoundType> c = type->as<CompoundType>();
        auto n = e = Terminal::get(opNil);

        for (unsigned int i = 0; i < c->getNumTypes(); i++) {
            Address    addr = uaddr + c->getOffsetTo(i) / 8;
            SharedType t    = c->getTypeAtIdx(i);
            auto       v    = readNativeAs(addr, t);

            if (v == nullptr) {
                LOG_MSG("Unable to read native address %1 as type %2", addr, t->getCtype());
                v = Const::get(-1);
            }

            if (n->isNil()) {
                n = Binary::get(opList, v, n);
                e = n;
            }
            else {
                assert(n->getSubExp2()->getOper() == opNil);
                n->setSubExp2(Binary::get(opList, v, n->getSubExp2()));
                n = n->getSubExp2();
            }
        }

        return e;
    }

    if (type->resolvesToArray() && type->as<ArrayType>()->getBaseType()->resolvesToChar()) {
        const char *str = getStringConstant(uaddr, true);

        if (str) {
            // Make a global string
            return Const::get(str);
        }
    }

    if (type->resolvesToArray()) {
        int  nelems  = -1;
        QString name     = getGlobalName(uaddr);
        int     base_sz = type->as<ArrayType>()->getBaseType()->getSize() / 8;

        if (!name.isEmpty()) {
            auto symbol = m_binaryFile->getSymbols()->findSymbolByName(name);
            nelems = symbol ? symbol->getSize() : 0;
            assert(base_sz);
            nelems /= base_sz;
        }

        auto n = e = Terminal::get(opNil);

        for (int i = 0; i < nelems; i++) {
            auto v = readNativeAs(uaddr + i * base_sz, type->as<ArrayType>()->getBaseType());

            if (v == nullptr) {
                break;
            }

            if (n->isNil()) {
                n = Binary::get(opList, v, n);
                e = n;
            }
            else {
                assert(n->getSubExp2()->getOper() == opNil);
                n->setSubExp2(Binary::get(opList, v, n->getSubExp2()));
                n = n->getSubExp2();
            }

            // "null" terminated
            if ((nelems == -1) && v->isConst() && (v->access<Const>()->getInt() == 0)) {
                break;
            }
        }
    }

    if (type->resolvesToInteger() || type->resolvesToSize()) {
        int size;

        if (type->resolvesToInteger()) {
            size = type->as<IntegerType>()->getSize();
        }
        else {
            size = type->as<SizeType>()->getSize();
        }

        switch (size)
        {
        case 8:
            return Const::get(m_binaryFile->getImage()->readNative1(uaddr));

        case 16:
            // Note: must respect endianness
            return Const::get(m_binaryFile->getImage()->readNative2(uaddr));

        case 32:
            return Const::get(m_binaryFile->getImage()->readNative4(uaddr));

        case 64:
            return Const::get(m_binaryFile->getImage()->readNative8(uaddr));
        }
    }

    if (!type->resolvesToFloat()) {
        return e;
    }

    switch (type->as<FloatType>()->getSize())
    {
    case 32:
        return Const::get(m_binaryFile->getImage()->readNativeFloat4(uaddr));

    case 64:
        return Const::get(m_binaryFile->getImage()->readNativeFloat8(uaddr));
    }

    return e;
}


bool Prog::reDecode(UserProc *proc)
{
    QTextStream os(stderr); // rtl output target

    return m_defaultFrontend->processProc(proc->getEntryAddress(), proc, os);
}


void Prog::decodeFragment(UserProc *proc, Address a)
{
    if ((a >= m_binaryFile->getImage()->getLimitTextLow()) && (a < m_binaryFile->getImage()->getLimitTextHigh())) {
        m_defaultFrontend->decodeFragment(proc, a);
    }
    else {
        LOG_ERROR("Attempt to decode fragment at address %1 outside text area", a);
    }
}


SharedExp Prog::addReloc(SharedExp e, Address location)
{
    assert(e->isConst());

    if (!m_binaryFile->isRelocationAt(location)) {
        return e;
    }

    // relocations have been applied to the constant, so if there is a
    // relocation for this lc then we should be able to replace the constant
    // with a symbol.
    Address c_addr = e->access<Const>()->getAddr();
    const BinarySymbol *bin_sym = m_binaryFile->getSymbols()->findSymbolByAddress(c_addr);

    if (bin_sym != nullptr) {
        unsigned int sz = bin_sym->getSize(); // TODO: fix the case of missing symbol table interface

        if (getGlobal(bin_sym->getName()) == nullptr) {
            m_globals.insert(std::make_shared<Global>(SizeType::get(sz * 8), c_addr, bin_sym->getName(), this));
        }

        return Unary::get(opAddrOf, Location::global(bin_sym->getName(), nullptr));
    }
    else {
        const char *str = getStringConstant(c_addr);

        if (str) {
            e = Const::get(str);
        }
        else {
            // check for accesses into the middle of symbols
            for (const BinarySymbol *sym : *m_binaryFile->getSymbols()) {
                unsigned int sz = sym->getSize();

                if ((sym->getLocation() < c_addr) && ((sym->getLocation() + sz) > c_addr)) {
                    int off = (c_addr - sym->getLocation()).value();
                    e = Binary::get(opPlus,
                                    Unary::get(opAddrOf,
                                               Location::global(sym->getName(), nullptr)),
                                    Const::get(off));
                    break;
                }
            }
        }
    }

    return e;
}


bool Prog::isStringConstant(Address a) const
{
    const BinarySection *si = static_cast<const BinarySection *>(m_binaryFile->getImage()->getSectionByAddr(a));

    return si && si->isAttributeInRange("StringsSection", a, a + 1);
}


bool Prog::isCFStringConstant(Address a) const
{
    return isStringConstant(a);
}
