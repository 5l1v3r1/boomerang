/*
 * Copyright (C) 1998-2001, The University of Queensland
 * Copyright (C) 2001, Sun Microsystems, Inc
 * Copyright (C) 2002-2003, Trent Waddington
 *
 * See the file "LICENSE.TERMS" for information on usage and
 * redistribution of this file, and for a DISCLAIMER OF ALL
 * WARRANTIES.
 *
 */

/***************************************************************************//**
 * \file     prog.cpp
 * \brief    Implementation of the program class. Holds information of
 *                interest to the whole program.
 ******************************************************************************/

/*
 * $Revision$    // 1.126.2.14
 *
 * 18 Apr 02 - Mike: Mods for boomerang
 * 26 Apr 02 - Mike: common.hs read relative to BOOMDIR
 * 20 Jul 04 - Mike: Got rid of BOOMDIR
 * 03 Mar 06 - tamlin: prevent arrays from crossing section boundaries
 * 20 Mar 11 - Mike: Added missing braces in Prog::getFloatConstant()
 */

/***************************************************************************//**
 * Dependencies.
 ******************************************************************************/

#include <cassert>
#if defined(_MSC_VER) && _MSC_VER <= 1200
#pragma warning(disable:4786)
// ? How does the following do any good?
#define for if(0){}else for
#endif
#if defined(_MSC_VER) && _MSC_VER >= 1400
#pragma warning(disable:4996)        // Warnings about e.g. _strdup deprecated in VS 2005
#endif

#include <cassert>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <vector>
#include <algorithm>
#include <cmath>
#ifdef _WIN32
#include <direct.h>                    // For Windows mkdir()
#endif

#include "type.h"
#include "cluster.h"
#include "types.h"
#include "statement.h"
#include "exp.h"
#include "cfg.h"
#include "proc.h"
#include "util.h"                    // For lockFileWrite etc
#include "register.h"
#include "rtl.h"
#include "BinaryFile.h"
#include "frontend.h"
#include "prog.h"
#include "signature.h"
#include "boomerang.h"
#include "ansi-c-parser.h"
#include "config.h"
#include "managed.h"
#include "log.h"

#ifdef _WIN32
#undef NO_ADDRESS
#include <windows.h>
#ifndef __MINGW32__
namespace dbghelp {
#include <dbghelp.h>
};
#endif
#undef NO_ADDRESS
#define NO_ADDRESS (ADDRESS::g(-1))
#endif

#include <sys/stat.h>
#include <sys/types.h>

Prog::Prog() :
    pBF(nullptr),
    pFE(nullptr),
    m_iNumberedProc(1),
    m_rootCluster(new Cluster("prog")) {
    // Default constructor
}

void Prog::setFrontEnd(FrontEnd *pFE) {
    pBF = pFE->getBinaryFile();
    this->pFE = pFE;
    if (pBF && pBF->getFilename()) {
        m_name = pBF->getFilename();
        m_rootCluster = new Cluster(getNameNoPathNoExt().c_str());
    }
}

Prog::Prog(const char* name) :
    pBF(nullptr),
    pFE(nullptr),
    m_name(name),
    m_iNumberedProc(1),
    m_rootCluster(new Cluster(getNameNoPathNoExt().c_str())) {
    // Constructor taking a name. Technically, the allocation of the space for the name could fail, but this is unlikely
    m_path = m_name;
}

Prog::~Prog() {
    delete pBF;
    delete pFE;
    for (Proc* proc : m_procs) {
        delete proc;
    }
    m_procs.clear();
}
//! Assign a name to this program
void Prog::setName (const char *name) {
    m_name = name;
    m_rootCluster->setName(name);
}

char* Prog::getName() {
    return (char*) m_name.c_str();
}

//! Well form all the procedures/cfgs in this program
bool Prog::wellForm() {
    bool wellformed = true;

    for (Proc* proc : m_procs)
        if (!proc->isLib()) {
            UserProc *u = (UserProc*)proc;
            wellformed &= u->getCFG()->wellFormCfg();
        }
    return wellformed;
}

// was in analysis.cpp
//! last fixes after decoding everything
void Prog::finishDecode() {
    for (Proc* pProc : m_procs) {
        if (pProc->isLib())
            continue;
        UserProc *p = (UserProc*)pProc;
        if (!p->isDecoded())
            continue;
        p->assignProcsToCalls();
        p->finalSimplify();
    }

}
//! Generate dotty file
void Prog::generateDotFile() {
    assert(Boomerang::get()->dotFile);
    std::ofstream of(Boomerang::get()->dotFile);
    of << "digraph Cfg {" << std::endl;

    for (Proc* pProc : m_procs) {
        if (pProc->isLib())
            continue;
        UserProc *p = (UserProc*)pProc;
        if (!p->isDecoded())
            continue;
        // Subgraph for the proc name
        of << "\nsubgraph cluster_" << p->getName() << " {\n" << "       color=gray;\n    label=" << p->getName() <<
              ";\n";
        // Generate dotty CFG for this proc
        p->getCFG()->generateDotFile(of);
    }
    of << "}";
    of.close();

}

void Prog::generateCode(Cluster *cluster, UserProc *proc, bool intermixRTL) {
    std::string basedir = m_rootCluster->makeDirs();
    std::ofstream os;
    if (cluster) {
        cluster->openStream("c");
        cluster->closeStreams();
    }
    if (cluster == nullptr || cluster == m_rootCluster) {
        os.open(m_rootCluster->getOutPath("c"));
        if (proc == nullptr) {
            HLLCode *code = Boomerang::get()->getHLLCode();
            bool global = false;
            if (Boomerang::get()->noDecompile) {
                const char *sections[] = { "rodata", "data", "data1", 0 };
                for (int j = 0; sections[j]; j++) {
                    std::string str = ".";
                    str += sections[j];
                    PSectionInfo info = pBF->GetSectionInfoByName(str.c_str());
                    str = "start_";
                    str    += sections[j];
                    code->AddGlobal(str.c_str(), new IntegerType(32, -1), new Const(info ? info->uNativeAddr : NO_ADDRESS));
                    str = sections[j];
                    str += "_size";
                    code->AddGlobal(str.c_str(), new IntegerType(32, -1), new Const(info ? info->uSectionSize : (unsigned int)-1));
                    Exp *l = new Terminal(opNil);
                    for (unsigned int i = 0; info && i < info->uSectionSize; i++) {
                        int n = pBF->readNative1(info->uNativeAddr + info->uSectionSize - 1 - i);
                        if (n < 0)
                            n = 256 + n;
                        l = new Binary(opList, new Const(n), l);
                    }
                    code->AddGlobal(sections[j], new ArrayType(new IntegerType(8, -1), info ? info->uSectionSize : 0), l);
                }
                code->AddGlobal("source_endianness", new IntegerType(), new Const(getFrontEndId() != PLAT_PENTIUM));
                os << "#include \"boomerang.h\"\n\n";
                global = true;
            }
            for (std::set<Global*>::iterator it1 = globals.begin(); it1 != globals.end(); it1++) {
                // Check for an initial value
                Exp *e = (*it1)->getInitialValue(this);
                //                if (e) {
                code->AddGlobal((*it1)->getName(), (*it1)->getType(), e);
                global = true;
                //                }
            }
            if (global)
                code->print(os);        // Avoid blank line if no globals
        }
    }

    // First declare prototypes for all but the first proc
    std::list<Proc*>::iterator it = m_procs.begin();
    bool first = true, proto = false;
    for (Proc* pProc : m_procs) {
        if (pProc->isLib()) continue;
        if (first) {
            first = false;
            continue;
        }
        proto = true;
        UserProc* up = (UserProc*)pProc;
        HLLCode *code = Boomerang::get()->getHLLCode(up);
        code->AddPrototype(up);                    // May be the wrong signature if up has ellipsis
        if (cluster == nullptr || cluster == m_rootCluster)
            code->print(os);
    }
    if ((proto && cluster == nullptr) || cluster == m_rootCluster)
        os << "\n";                // Separate prototype(s) from first proc

    for (Proc* pProc : m_procs) {
        if (pProc->isLib())
            continue;
        UserProc *up = (UserProc*)pProc;
        if (!up->isDecoded()) continue;
        if (proc != nullptr && up != proc)
            continue;
        up->getCFG()->compressCfg();
        HLLCode *code = Boomerang::get()->getHLLCode(up);
        up->generateCode(code);
        if (up->getCluster() == m_rootCluster) {
            if (cluster == nullptr || cluster == m_rootCluster)
                code->print(os);
        } else {
            if (cluster == nullptr || cluster == up->getCluster()) {
                up->getCluster()->openStream("c");
                code->print(up->getCluster()->getStream());
            }
        }
    }
    os.close();
    m_rootCluster->closeStreams();
}

void Prog::generateRTL(Cluster *cluster, UserProc *proc) {
    for (Proc* pProc : m_procs) {
        if (pProc->isLib())
            continue;
        UserProc *p = (UserProc*)pProc;
        if (!p->isDecoded())
            continue;
        if (proc != nullptr && p != proc)
            continue;
        if (cluster != nullptr && p->getCluster() != cluster)
            continue;

        p->getCluster()->openStream("rtl");
        p->print(p->getCluster()->getStream());
    }
    m_rootCluster->closeStreams();
}

Statement *Prog::getStmtAtLex(Cluster *cluster, unsigned int begin, unsigned int end) {
    for (Proc* pProc : m_procs) {
        if (pProc->isLib())
            continue;
        UserProc *p = (UserProc*)pProc;
        if (!p->isDecoded())
            continue;
        if (cluster != nullptr && p->getCluster() != cluster)
            continue;

        if (p->getCluster() == cluster) {
            Statement *s = p->getStmtAtLex(begin, end);
            if (s)
                return s;
        }
    }
    return nullptr;
}


const char *Cluster::makeDirs() {
    std::string path;
    if (parent)
        path = parent->makeDirs();
    else
        path = Boomerang::get()->getOutputPath();
    if (getNumChildren() > 0 || parent == nullptr) {
        path = path + "/" + name;
#ifdef _WIN32
        mkdir(path.c_str());
#else
        mkdir(path.c_str(), 0777);
#endif
    }
    return strdup(path.c_str());
}

void Cluster::removeChild(Cluster *n) {
    auto it= children.begin();
    for (; it != children.end(); it++)
        if (*it == n)
            break;
    assert(it != children.end());
    children.erase(it);
}

void Cluster::addChild(Cluster *n) {
    if (n->parent)
        n->parent->removeChild(n);
    children.push_back(n);
    n->parent = this;
}

Cluster *Cluster::find(const char *nam) {
    if (name == nam)
        return this;
    for (Cluster * child : children) {
        Cluster *c = child->find(nam);
        if (c)
            return c;
    }
    return nullptr;
}

const char *Cluster::getOutPath(const char *ext) {
    std::string basedir = makeDirs();
    // Ugh - should probably return a whole std::string
    return strdup((basedir + "/" + name + "." + ext).c_str());
}

void Cluster::openStream(const char *ext) {
    if (out.is_open())
        return;
    out.open(getOutPath(ext));
    stream_ext = ext;
    if (!strcmp(ext, "xml")) {
        out << "<?xml version=\"1.0\"?>\n";
        if (parent != nullptr)
            out << "<procs>\n";
    }
}

void Cluster::openStreams(const char *ext) {
    openStream(ext);
    for (Cluster * child : children)
        child->openStreams(ext);
}

void Cluster::closeStreams() {
    if (out.is_open()) {
        out.close();
    }
    for (Cluster * child : children)
        child->closeStreams();
}

bool Prog::clusterUsed(Cluster *c) {
    for (Proc* pProc : m_procs)
        if (pProc->getCluster() == c)
            return true;
    return false;
}

Cluster    *Prog::getDefaultCluster(const char *name) {
    const char *cfname = nullptr;
    if (pBF)
        cfname = pBF->getFilenameSymbolFor(name);
    if (cfname == nullptr)
        return m_rootCluster;
    if (strcmp(cfname + strlen(cfname) - 2, ".c"))
        return m_rootCluster;
    LOG << "got filename " << cfname << " for " << name << "\n";
    char *fname = strdup(cfname);
    fname[strlen(fname) - 2] = 0;
    Cluster *c = findCluster(fname);
    if (c == nullptr) {
        c = new Cluster(fname);
        m_rootCluster->addChild(c);
    }
    return c;
}

void Prog::generateCode(std::ostream &os) {
    HLLCode *code = Boomerang::get()->getHLLCode();
    for ( Global *glob : globals) {
        // Check for an initial value
        Exp *e = nullptr;
        e = glob->getInitialValue(this);
        if (e)
            code->AddGlobal(glob->getName(), glob->getType(), e);
    }
    code->print(os);
    delete code;
    for (Proc* pProc : m_procs) {
        if (pProc->isLib())
            continue;
        UserProc *p = (UserProc*)pProc;
        if (!p->isDecoded())
            continue;
        p->getCFG()->compressCfg();
        code = Boomerang::get()->getHLLCode(p);
        p->generateCode(code);
        code->print(os);
        delete code;
    }
}

//! Print this program (primarily for debugging)
void Prog::print(std::ostream &out) {
    for (Proc* pProc : m_procs) {
        if (pProc->isLib())
            continue;
        UserProc *p = (UserProc*)pProc;
        if (!p->isDecoded())
            continue;

        // decoded userproc.. print it
        p->print(out);
    }
}

//! clear the prog object \note deletes everything!
void Prog::clear() {
    m_name = std::string("");
    for (Proc* pProc : m_procs)
        delete pProc;
    m_procs.clear();
    m_procLabels.clear();
    delete pBF;
    pBF = nullptr;
    delete pFE;
    pFE = nullptr;
}

/***************************************************************************//**
 *
 * \note     Formally Frontend::newProc
 * \brief    Call this function when a procedure is discovered (usually by
 *                  decoding a call instruction). That way, it is given a name
 *                  that can be displayed in the dot file, etc. If we assign it
 *                  a number now, then it will retain this number always
 * \param uAddr - Native address of the procedure entry point
 * \returns        Pointer to the Proc object, or 0 if this is a deleted (not to
 *                  be decoded) address
 ******************************************************************************/
Proc* Prog::setNewProc(ADDRESS uAddr) {
    // this test fails when decoding sparc, why?  Please investigate - trent
    // Likely because it is in the Procedure Linkage Table (.plt), which for Sparc is in the data section
    //assert(uAddr >= limitTextLow && uAddr < limitTextHigh);
    // Check if we already have this proc
    Proc* pProc = findProc(uAddr);
    if (pProc == (Proc*)-1)            // Already decoded and deleted?
        return 0;                    // Yes, exit with 0
    if (pProc)
        // Yes, we are done
        return pProc;
    ADDRESS other = pBF->IsJumpToAnotherAddr(uAddr);
    if (other != NO_ADDRESS)
        uAddr = other;
    const char* pName = pBF->SymbolByAddress(uAddr);
    bool bLib = pBF->IsDynamicLinkedProc(uAddr) | pBF->IsStaticLinkedLibProc(uAddr);
    if (pName == nullptr) {
        // No name. Give it a numbered name
        std::ostringstream ost;
        ost << "proc" << m_iNumberedProc++;
        pName = strdup(ost.str().c_str());
        if (VERBOSE)
            LOG << "assigning name " << pName << " to addr " << uAddr << "\n";
    }
    pProc = newProc(pName, uAddr, bLib);
    return pProc;
}

#if defined(_WIN32) && !defined(__MINGW32__)

Type *typeFromDebugInfo(int index, DWORD64 ModBase);

Type *makeUDT(int index, DWORD64 ModBase) {
    HANDLE hProcess = GetCurrentProcess();
    int got;
    WCHAR *name;
    got = dbghelp::SymGetTypeInfo(hProcess, ModBase, index, dbghelp::TI_GET_SYMNAME, &name);
    assert(got);
    if (got) {
        char nameA[1024];
        WideCharToMultiByte(CP_ACP,0,name,-1,nameA,sizeof(nameA),0,nullptr);
        Type *ty = Type::getNamedType(nameA);
        if (ty)
            return new NamedType(nameA);
        CompoundType *cty = new CompoundType();
        DWORD count = 0;
        got = dbghelp::SymGetTypeInfo(hProcess, ModBase, index, dbghelp::TI_GET_CHILDRENCOUNT, &count);
        int FindChildrenSize = sizeof(dbghelp::TI_FINDCHILDREN_PARAMS) + count*sizeof(ULONG);
        dbghelp::TI_FINDCHILDREN_PARAMS* pFC = (dbghelp::TI_FINDCHILDREN_PARAMS*)malloc( FindChildrenSize );
        memset( pFC, 0, FindChildrenSize );
        pFC->Count = count;
        got = SymGetTypeInfo( hProcess, ModBase, index, dbghelp::TI_FINDCHILDREN, pFC );
        for (unsigned int i = 0; i < count; i++) {
            char fieldName[1024];
            got = dbghelp::SymGetTypeInfo(hProcess, ModBase, pFC->ChildId[i], dbghelp::TI_GET_SYMNAME, &name);
            WideCharToMultiByte(CP_ACP,0,name,-1,fieldName,sizeof(fieldName),0,nullptr);
            DWORD mytype;
            got = dbghelp::SymGetTypeInfo(hProcess, ModBase, pFC->ChildId[i], dbghelp::TI_GET_TYPE, &mytype);
            cty->addType(typeFromDebugInfo(mytype, ModBase), fieldName);
        }
        Type::addNamedType(nameA, cty);
        ty = Type::getNamedType(nameA);
        assert(ty);
        return new NamedType(nameA);
    }
    return nullptr;
}

Type *typeFromDebugInfo(int index, DWORD64 ModBase) {
    HANDLE hProcess = GetCurrentProcess();

    int got;
    DWORD d;
    ULONG64 lsz = 0;
    got = dbghelp::SymGetTypeInfo(hProcess, ModBase, index, dbghelp::TI_GET_SYMTAG, &d);
    assert(got);
    got = dbghelp::SymGetTypeInfo(hProcess, ModBase, index, dbghelp::TI_GET_LENGTH, &lsz);
    int sz = (int)lsz * 8;  // bits

    switch(d) {
        case 11:
            return makeUDT(index, ModBase);
        case 13:
            // TODO: signature
            return new FuncType();
        case 14:
            got = dbghelp::SymGetTypeInfo(hProcess, ModBase, index, dbghelp::TI_GET_TYPE, &d);
            assert(got);
            return new PointerType(typeFromDebugInfo(d, ModBase));
        case 15:
            got = dbghelp::SymGetTypeInfo(hProcess, ModBase, index, dbghelp::TI_GET_TYPE, &d);
            assert(got);
            got = dbghelp::SymGetTypeInfo(hProcess, ModBase, index, dbghelp::TI_GET_LENGTH, &lsz);
            assert(got);
            return new ArrayType(typeFromDebugInfo(d, ModBase), (unsigned)lsz);
            break;
        case 16:
            got = dbghelp::SymGetTypeInfo(hProcess, ModBase, index, dbghelp::TI_GET_BASETYPE, &d);
            assert(got);
            switch(d) {
                case 1:
                    return new VoidType();
                case 2:
                    return new CharType();
                case 3:
                    return new CharType();
                case 6: // int
                case 13: // long
                    return new IntegerType(sz, 1);
                case 7: // unsigned int
                case 14: // ulong
                    return new IntegerType(sz, -1);
                case 8:
                    return new FloatType(sz);
                case 10:
                    return new BooleanType();
                default:
                    std::cerr << "unhandled base type " << d << "\n";
                    assert(false);
            }
            break;
        default:
            std::cerr << "unhandled symtag " << d << "\n";
            assert(false);
    }
    return nullptr;
}

int debugRegister(int r) {
    switch(r) {
        case 2:
            return 26;  // edx
        case 4:
            return 25;  // ecx
        case 8:
            return 29;  // ebp
    }
    assert(false);
    return -1;
}

BOOL CALLBACK addSymbol(
        dbghelp::PSYMBOL_INFO pSymInfo,
        ULONG SymbolSize,
        PVOID UserContext
        ) {
    Proc *proc = (Proc*)UserContext;
    const char *name = proc->getName();
    if (pSymInfo->Flags & SYMFLAG_PARAMETER) {
        Type *ty = typeFromDebugInfo(pSymInfo->TypeIndex, pSymInfo->ModBase);
        if (pSymInfo->Flags & SYMFLAG_REGREL) {
            assert(pSymInfo->Register == 8);  // ebp
            proc->getSignature()->addParameter(ty, pSymInfo->Name, Location::memOf(new Binary(opPlus, Location::regOf(28), new Const((int)pSymInfo->Address - 4))));
        } else if (pSymInfo->Flags & SYMFLAG_REGISTER) {
            proc->getSignature()->addParameter(ty, pSymInfo->Name, Location::regOf(debugRegister(pSymInfo->Register)));
        }
    } else if ((pSymInfo->Flags & SYMFLAG_LOCAL) && !proc->isLib()) {
        UserProc *u = (UserProc*)proc;
        assert(pSymInfo->Flags & SYMFLAG_REGREL);
        assert(pSymInfo->Register == 8);
        Exp *memref = Location::memOf(new Binary(opMinus, Location::regOf(28), new Const(-((int)pSymInfo->Address - 4))));
        Type *ty = typeFromDebugInfo(pSymInfo->TypeIndex, pSymInfo->ModBase);
        u->addLocal(ty, pSymInfo->Name, memref);
    }
    return TRUE;
}

#endif

/***************************************************************************//**
 *
 * \brief    Creates a new Proc object, adds it to the list of procs in this Prog object, and adds the address to
 * the list
 * \param name - Name for the proc
 * \param uNative - Native address of the entry point of the proc
 * \param bLib - If true, this will be a libProc; else a UserProc
 * \returns        A pointer to the new Proc object
 ******************************************************************************/
Proc* Prog::newProc (const char* name, ADDRESS uNative, bool bLib /*= false*/) {
    Proc* pProc;
    std::string sname(name);
    if (bLib)
        pProc = new LibProc(this, sname, uNative);
    else
        pProc = new UserProc(this, sname, uNative);
    //TODO: add platform agnostic way of using debug information, should be moved to Loaders, Prog should just collect info from Loader

#if defined(_WIN32) && !defined(__MINGW32__)
    if (isWin32()) {
        // use debugging information
        HANDLE hProcess = GetCurrentProcess();
        dbghelp::SYMBOL_INFO *sym = (dbghelp::SYMBOL_INFO *)malloc(sizeof(dbghelp::SYMBOL_INFO) + 1000);
        sym->SizeOfStruct = sizeof(*sym);
        sym->MaxNameLen = 1000;
        sym->Name[0] = 0;
        BOOL got = dbghelp::SymFromAddr(hProcess, uNative.m_value, 0, sym);
        DWORD retType;
        if (got && *sym->Name && dbghelp::SymGetTypeInfo(hProcess, sym->ModBase, sym->TypeIndex, dbghelp::TI_GET_TYPE, &retType)) {
            DWORD d;
            // get a calling convention
            got = dbghelp::SymGetTypeInfo(hProcess, sym->ModBase, sym->TypeIndex, dbghelp::TI_GET_CALLING_CONVENTION, &d);
            if (got) {
                std::cout << "calling convention: " << d << "\n";
                // TODO: use it
            } else {
                // assume we're stdc calling convention, remove r28, r24 returns
                pProc->setSignature(Signature::instantiate(PLAT_PENTIUM, CONV_C, sname.c_str()));
            }

            // get a return type
            Type *rtype = typeFromDebugInfo(retType, sym->ModBase);
            if (!rtype->isVoid()) {
                pProc->getSignature()->addReturn(rtype, Location::regOf(24));
            }

            // find params and locals
            dbghelp::IMAGEHLP_STACK_FRAME stack;
            stack.InstructionOffset = uNative.m_value;
            dbghelp::SymSetContext(hProcess, &stack, 0);
            dbghelp::SymEnumSymbols(hProcess, 0, nullptr, addSymbol, pProc);

            LOG << "final signature: ";
            pProc->getSignature()->printToLog();
            LOG << "\n";
        }
    }
#endif
    m_procs.push_back(pProc);        // Append this to list of procs
    m_procLabels[uNative] = pProc;
    // alert the watchers of a new proc
    Boomerang::get()->alert_new(pProc);
    return pProc;
}

/***************************************************************************//**
 *
 * \brief Removes the UserProc from this Prog object's list, and deletes as much as possible of the Proc
 * \param proc - pointer to the UserProc object to be removed
 ******************************************************************************/
void Prog::remProc(UserProc* uProc) {
    // Delete the cfg etc.
    uProc->deleteCFG();

    // Replace the entry in the procedure map with -1 as a warning not to decode that address ever again
    m_procLabels[uProc->getNativeAddress()] = (Proc*)-1;

    for (std::list<Proc*>::iterator it = m_procs.begin(); it != m_procs.end(); it++) {
        if (*it == uProc) {
            m_procs.erase(it);
            break;
        }
    }

    // Delete the UserProc object as well
    delete uProc;
}

void Prog::removeProc(const char *name) {
    for (std::list<Proc*>::iterator it = m_procs.begin(); it != m_procs.end(); it++)
        if (std::string(name) == (*it)->getName()) {
            Boomerang::get()->alert_remove(*it);
            m_procs.erase(it);
            break;
        }
}

/***************************************************************************//**
 *
 * \brief    Return the number of real (non deleted) procedures
 * \returns        The number of procedures
 ******************************************************************************/
int Prog::getNumProcs() {
    return m_procs.size();
}
/***************************************************************************//**
 *
 * \brief    Return the number of user (non deleted, non library) procedures
 * \returns  The number of procedures
 ******************************************************************************/
int Prog::getNumUserProcs() {
    int n = 0;
    for (Proc* pProc : m_procs)
        if (!pProc->isLib())
            n++;
    return n;
}

/***************************************************************************//**
 *
 * \brief Return a pointer to the indexed Proc object
 * \param idx - Index of the proc
 * \returns Pointer to the Proc object, or 0 if index invalid
 ******************************************************************************/
Proc* Prog::getProc(int idx) const {
    // Return the indexed procedure. If this is used often, we should use a vector instead of a list
    // If index is invalid, result will be 0
    if ((idx < 0) || (idx >= (int)m_procs.size()))
        return 0;
    std::list<Proc*>::const_iterator it = m_procs.begin();
    std::advance(it,idx);
    return (*it);
}


/***************************************************************************//**
 *
 * \brief    Return a pointer to the associated Proc object, or nullptr if none
 * \note        Could return -1 for a deleted Proc
 * \param uAddr - Native address of the procedure entry point
 * \returns Pointer to the Proc object, or 0 if none, or -1 if deleted
 ******************************************************************************/
Proc* Prog::findProc(ADDRESS uAddr) const {
    PROGMAP::const_iterator it;
    it = m_procLabels.find(uAddr);
    if (it == m_procLabels.end())
        return nullptr;
    return (*it).second;
}
/***************************************************************************//**
 * \brief    Return a pointer to the associated Proc object, or nullptr if none
 * \note        Could return -1 for a deleted Proc
 * \param name - name of the searched-for procedure
 * \returns Pointer to the Proc object, or 0 if none, or -1 if deleted
 ******************************************************************************/
Proc* Prog::findProc(const char *name) const {
    std::list<Proc *>::const_iterator it;
    it = std::find_if(m_procs.begin(),m_procs.end(),
                      [name](Proc *p) -> bool {
                        return !strcmp(p->getName(), name);
                      });
    if(it==m_procs.end())
        return nullptr;
    return *it;
}

//! lookup a library procedure by name; create if does not exist
LibProc *Prog::getLibraryProc(const char *nam) {
    Proc *p = findProc(nam);
    if (p && p->isLib())
        return (LibProc*)p;
    return (LibProc*)newProc(nam, NO_ADDRESS, true);
}
//! Get a library signature for a given name (used when creating a new library proc).
Signature* Prog::getLibSignature(const char *nam) {
    return pFE->getLibSignature(nam);
}

void Prog::rereadLibSignatures() {
    pFE->readLibraryCatalog();
    for (Proc* pProc : m_procs) {
        if (pProc->isLib()) {
            pProc->setSignature(getLibSignature(pProc->getName()));
            for (CallStatement * call_stmt : pProc->getCallers())
                call_stmt->setSigArguments();
            Boomerang::get()->alert_update_signature(pProc);
        }
    }
}
//! Get the front end id used to make this prog
platform Prog::getFrontEndId() {
    return pFE->getFrontEndId();
}

Signature *Prog::getDefaultSignature(const char *name) {
    return pFE->getDefaultSignature(name);
}

std::vector<Exp*> &Prog::getDefaultParams() {
    return pFE->getDefaultParams();
}

std::vector<Exp*> &Prog::getDefaultReturns() {
    return pFE->getDefaultReturns();
}
//! Returns true if this is a win32 program
bool Prog::isWin32() {
    if (!pFE)
      return false;
    return pFE->isWin32();
}
//! Get a global variable if possible, looking up the loader's symbol table if necessary
const char *Prog::getGlobalName(ADDRESS uaddr) {
    // FIXME: inefficient
    for (Global* glob : globals) {
        if (glob->getAddress() == uaddr)
            return glob->getName();
        else if (glob->getAddress() < uaddr &&
                 glob->getAddress() + glob->getType()->getSize() / 8 > uaddr) //TODO: use glob->getBytes() ?
            return glob->getName();
    }
    if (pBF)
        return pBF->SymbolByAddress(uaddr);
    return nullptr;
}
//! Dump the globals to stderr for debugging
void Prog::dumpGlobals() {
    for (Global *glob : globals) {
        glob->print(std::cerr, this);
        std::cerr << "\n";
    }
}
//! Get a named global variable if possible, looking up the loader's symbol table if necessary
ADDRESS Prog::getGlobalAddr(const char *nam) {
    Global *glob=getGlobal(nam);
    if(glob)
        return glob->getAddress();
    return pBF->GetAddressByName(nam);
}

Global* Prog::getGlobal(const char *nam) {
    auto iter = std::find_if(globals.begin(),globals.end(),
                             [nam](Global *g) -> bool { return !strcmp(g->getName(),nam); }
                            );
    if(iter==globals.end())
        return nullptr;
    return *iter;
}
//! Indicate that a given global has been seen used in the program.
bool Prog::globalUsed(ADDRESS uaddr, Type* knownType) {
    for (Global *glob : globals) {
        if (glob->addressWithinGlobal(uaddr) ) {
            if (knownType)
                glob->meetType(knownType);
            return true;
        }
    }

    if (pBF->GetSectionInfoByAddr(uaddr) == nullptr) {
        if (VERBOSE)
            LOG << "refusing to create a global at address that is in no known section of the binary: " << uaddr << "\n";
        return false;
    }

    const char *nam = newGlobalName(uaddr);
    Type *ty;
    if (knownType) {
        ty = knownType;
        if (ty->resolvesToArray() && ty->asArray()->isUnbounded()) {
            Type* baseType = ty->asArray()->getBaseType();
            int baseSize = 0;
            if (baseType)
                baseSize = baseType->getSize() / 8;        // TODO: use baseType->getBytes()
            int sz = pBF->GetSizeByName(nam);
            if (sz && baseSize)
                // Note: since ty is a pointer and has not been cloned, this will also set the type for knownType
                ty->asArray()->setLength(sz / baseSize);
        }
    }
    else
        ty = guessGlobalType(nam, uaddr);

    Global *global = new Global(ty, uaddr, nam);
    globals.insert(global);

    if (VERBOSE) {
        LOG << "globalUsed: name " << nam << ", address " << uaddr;
        if (knownType)
            LOG << ", known type " << ty->getCtype() << "\n";
        else
            LOG << ", guessed type " << ty->getCtype() << "\n";
    }
    return true;
}

Prog::mAddressString &Prog::getSymbols() {
    return pBF->getSymbols();
}
//! Make an array type for the global array at u. Mainly, set the length sensibly
ArrayType* Prog::makeArrayType(ADDRESS u, Type* t) {
    const char* nam = newGlobalName(u);
    assert(pBF);
    int sz = pBF->GetSizeByName(nam);
    if (sz == 0)
        return new ArrayType(t);        // An "unbounded" array
    int n = t->getSize()/8;             // TODO: use baseType->getBytes()
    if (n == 0)
        n = 1;
    return new ArrayType(t, sz/n);
}
//! Guess a global's type based on its name and address
Type *Prog::guessGlobalType(const char *nam, ADDRESS u) {
#if defined(_WIN32) && !defined(__MINGW32__)
    HANDLE hProcess = GetCurrentProcess();
    dbghelp::SYMBOL_INFO *sym = (dbghelp::SYMBOL_INFO *)malloc(sizeof(dbghelp::SYMBOL_INFO) + 1000);
    sym->SizeOfStruct = sizeof(*sym);
    sym->MaxNameLen = 1000;
    sym->Name[0] = 0;
    BOOL got = dbghelp::SymFromAddr(hProcess, u.m_value, 0, sym);
    if (got && *sym->Name && sym->TypeIndex) {
        assert(!strcmp(nam, sym->Name));
        return typeFromDebugInfo(sym->TypeIndex, sym->ModBase);
    }
#endif
    int sz = pBF->GetSizeByName(nam);
    if (sz == 0) {
        // Check if it might be a string
        const char* str = getStringConstant(u);
        if (str)
            // return char* and hope it is dealt with properly
            return new PointerType(new CharType());
    }
    Type *ty;
    switch(sz) {
        case 1: case 2: case 4: case 8:
            ty = new IntegerType(sz*8);
            break;
        default:
            ty = new ArrayType(new CharType(), sz);
    }
    return ty;
}
//! Make up a name for a new global at address \a uaddr (or return an existing name if address already used)
const char *Prog::newGlobalName(ADDRESS uaddr) {
    const char *nam = getGlobalName(uaddr);
    if (nam == nullptr) {
        std::ostringstream os;
        os << "global" << globals.size();
        nam = strdup(os.str().c_str());
        if (VERBOSE)
            LOG << "naming new global: " << nam << " at address " << uaddr << "\n";
    }
    return nam;
}
//! Get the type of a global variable
Type *Prog::getGlobalType(const char* nam) {
    for (Global * gl : globals )
        if (!strcmp(gl->getName(), nam))
            return gl->getType();
    return nullptr;
}
//! Set the type of a global variable
void Prog::setGlobalType(const char* nam, Type* ty) {
    // FIXME: inefficient
    for (Global * gl : globals ) {
        if (strcmp(gl->getName(), nam))
            continue;
        gl->setType(ty);
        return;
    }
}

// get a string constant at a given address if appropriate
// if knownString, it is already known to be a char*
//! get a string constant at a give address if appropriate
const char *Prog::getStringConstant(ADDRESS uaddr, bool knownString /* = false */) {
    const SectionInfo* si = pBF->GetSectionInfoByAddr(uaddr);
    // Too many compilers put constants, including string constants, into read/write sections
    //if (si && si->bReadOnly)
    if (si && !si->isAddressBss(uaddr)) {
        // At this stage, only support ascii, null terminated, non unicode strings.
        // At least 4 of the first 6 chars should be printable ascii
                char* p = (char*)(uaddr + si->uHostAddr - si->uNativeAddr).m_value;
        if (knownString)
            // No need to guess... this is hopefully a known string
            return p;
        int printable = 0;
        char last = 0;
        for (int i=0; i < 6; i++) {
            char c = p[i];
            if (c == 0) break;
            if (c >= ' ' && c < '\x7F') printable++;
            last = c;
        }
        if (printable >= 4)
            return p;
        // Just a hack while type propagations are not yet ready
        if (last == '\n' && printable >= 2)
            return p;
    }
    return nullptr;
}

double Prog::getFloatConstant(ADDRESS uaddr, bool &ok, int bits) {
    ok = true;
    SectionInfo* si = pBF->GetSectionInfoByAddr(uaddr);
    if (si && si->bReadOnly) {
        if (bits == 64) { // TODO: handle 80bit floats ?
            return pBF->readNativeFloat8(uaddr);
        } else {
            assert(bits == 32);
            return pBF->readNativeFloat4(uaddr);
        }
    }
    ok = false;
    return 0.0;
}

/***************************************************************************//**
 *
 * \brief    Return a pointer to the Proc object containing uAddr, or 0 if none
 * \note     Could return nullptr for a deleted Proc
 * \param uAddr - Native address to search for
 * \returns        Pointer to the Proc object, or 0 if none, or -1 if deleted
 ******************************************************************************/
Proc* Prog::findContainingProc(ADDRESS uAddr) const {
    for (Proc* p: m_procs) {
        if (p->getNativeAddress() == uAddr)
            return p;
        if (p->isLib())
            continue;
        UserProc *u = (UserProc *)p;
        if (u->containsAddr(uAddr))
            return p;
    }
    return nullptr;
}

/***************************************************************************//**
 *
 * \brief    Return true if this is a real procedure
 * \param addr   Native address of the procedure entry point
 * \returns        True if a real (non deleted) proc
 ******************************************************************************/
bool Prog::isProcLabel (ADDRESS addr) {
    return m_procLabels[addr] != 0;
}

/***************************************************************************//**
 *
 * \brief Get the name for the progam, without any path at the front
 * \returns A string with the name
 ******************************************************************************/
std::string Prog::getNameNoPath() const {
    unsigned n = m_name.rfind("/");
    if (n == std::string::npos) {
        n = m_name.rfind("\\");
        if (n == std::string::npos)
            return m_name;
    }

    return m_name.substr(n+1);
}

/***************************************************************************//**
 *
 * \brief Get the name for the progam, without any path at the front, and no extension
 * \sa Prog::getNameNoPath
 * \returns A string with the name
 ******************************************************************************/
std::string Prog::getNameNoPathNoExt() const {
    std::string nopath = getNameNoPath();
    unsigned n = nopath.rfind(".");
    if (n == std::string::npos)
        return nopath;
    return nopath.substr(0, n);
}

/***************************************************************************//**
 *
 * \brief    Return a pointer to the first Proc object for this program
 * \note    The \a it parameter must be passed to getNextProc
 * \param    it An uninitialised PROGMAP::const_iterator
 * \returns        A pointer to the first Proc object; could be 0 if none
 ******************************************************************************/
Proc* Prog::getFirstProc(PROGMAP::const_iterator& it) {
    it = m_procLabels.begin();
    while (it != m_procLabels.end() && (it->second == (Proc*) -1))
        it++;
    if (it == m_procLabels.end())
        return 0;
    return it->second;
}

/***************************************************************************//**
 *
 * \brief    Return a pointer to the next Proc object for this program
 * \note       The \a it parameter must be from a previous call to getFirstProc or getNextProc
 * \param    it: A PROGMAP::const_iterator as above
 * \returns        A pointer to the next Proc object; could be 0 if no more
 ******************************************************************************/
Proc* Prog::getNextProc(PROGMAP::const_iterator& it) {
    it++;
    while (it != m_procLabels.end() && (it->second == (Proc*) -1))
        it++;
    if (it == m_procLabels.end())
        return 0;
    return it->second;
}

/***************************************************************************//**
 *
 * \brief    Return a pointer to the first UserProc object for this program
 * \note    The \a it parameter must be passed to getNextUserProc
 * \param    it An uninitialised std::list<Proc*>::iterator
 * \returns A pointer to the first UserProc object; could be 0 if none
 ******************************************************************************/
UserProc* Prog::getFirstUserProc(std::list<Proc*>::iterator& it) {
    it = m_procs.begin();
    while (it != m_procs.end() && (*it)->isLib())
        it++;
    if (it == m_procs.end())
        return 0;
    return (UserProc*)*it;
}

/***************************************************************************//**
 *
 * \brief    Return a pointer to the next UserProc object for this program
 * NOTE:        The it parameter must be from a previous call to
 *                  getFirstUserProc or getNextUserProc
 * PARAMETERS:    it: A std::list<Proc*>::iterator
 * \returns        A pointer to the next UserProc object; could be 0 if no more
 ******************************************************************************/
UserProc* Prog::getNextUserProc(std::list<Proc*>::iterator& it) {
    it++;
    while (it != m_procs.end() && (*it)->isLib())
        it++;
    if (it == m_procs.end())
        return 0;
    return (UserProc*)*it;
}

/***************************************************************************//**
 *
 * \brief    Lookup the given native address in the code section, returning a host pointer corresponding to the same
 *                 address
 * \param uNative: Native address of the candidate string or constant
 * \param last: will be set to one past end of the code section (host)
 * \param delta: will be set to the difference between the host and native addresses
 * \returns        Host pointer if in range; nullptr if not
 *                Also sets 2 reference parameters (see above)
 ******************************************************************************/
const void* Prog::getCodeInfo(ADDRESS uAddr, const char*& last, int& delta) {
    delta=0;
    last=0;
#ifdef _WIN32
    // this is broken obviously
    return nullptr;
#else
    int n = pBF->GetNumSections();
    int i;
    // Search all code and read-only sections
    for (i=0; i < n; i++) {
        SectionInfo* pSect = pBF->GetSectionInfo(i);
        if ((!pSect->bCode) && (!pSect->bReadOnly))
            continue;
        if ((uAddr < pSect->uNativeAddr) || (uAddr >= pSect->uNativeAddr + pSect->uSectionSize))
            continue;            // Try the next section
        delta = (pSect->uHostAddr - pSect->uNativeAddr).m_value;
        last = (const char*) (pSect->uHostAddr + pSect->uSectionSize).m_value;
        const char* p = (const char *) (uAddr + delta).m_value;
        return p;
    }
    return nullptr;
#endif
}

/***************************************************************************//**
 *
 * \brief    Decode from entry point given as an agrument
 * \param a -  Native address of the entry point
 *
 ******************************************************************************/
void Prog::decodeEntryPoint(ADDRESS a) {
    Proc* p = (UserProc*)findProc(a);
    if (p == nullptr || (!p->isLib() && !((UserProc*)p)->isDecoded())) {
        if (a < pBF->getLimitTextLow() || a >= pBF->getLimitTextHigh()) {
            std::cerr << "attempt to decode entrypoint at address outside text area, addr=" << a << "\n";
            if (VERBOSE)
                LOG << "attempt to decode entrypoint at address outside text area, addr=" << a << "\n";
            return;
        }
        pFE->decode(this, a);
        finishDecode();
    }
    if (p == nullptr)
        p = findProc(a);
    assert(p);
    if (!p->isLib())                // -sf procs marked as __nodecode are treated as library procs (?)
        entryProcs.push_back((UserProc*)p);
}
/***************************************************************************//**
 *
 * \brief    Add entry point given as an agrument to the list of entryProcs
 * \param a -  Native address of the entry point
 *
 ******************************************************************************/
void Prog::setEntryPoint(ADDRESS a) {
    Proc* p = (UserProc*)findProc(a);
    if (p != nullptr && !p->isLib())
        entryProcs.push_back((UserProc*)p);
}

void Prog::decodeEverythingUndecoded() {
    for (Proc *pp : m_procs) {
        UserProc *up = (UserProc*) pp;
        if (!pp || pp->isLib())
            continue;
        if (up->isDecoded())
            continue;
        pFE->decode(this, pp->getNativeAddress());
    }
    finishDecode();
}
//! Do the main non-global decompilation steps
void Prog::decompile() {
    assert(m_procs.size());

    if (VERBOSE)
        LOG << (int)m_procs.size() << " procedures\n";

    // Start decompiling each entry point
    for (UserProc *up : entryProcs) {
        std::cerr << "decompiling entry point " << up->getName() << "\n";
        if (VERBOSE)
            LOG << "decompiling entry point " << up->getName() << "\n";
        int indent = 0;
        up->decompile(new ProcList, indent);
    }

    // Just in case there are any Procs not in the call graph.
    std::list<Proc*>::iterator pp;
    if (Boomerang::get()->decodeMain && !Boomerang::get()->noDecodeChildren) {
        bool foundone = true;
        while (foundone) {
            foundone = false;
            for (Proc * pp : m_procs) {
                UserProc* proc = (UserProc*)pp;
                if (proc->isLib()) continue;
                if (proc->isDecompiled()) continue;
                int indent = 0;
                proc->decompile(new ProcList, indent);
                foundone = true;
            }
        }
    }

    // Type analysis, if requested
    if (Boomerang::get()->conTypeAnalysis && Boomerang::get()->dfaTypeAnalysis) {
        std::cerr << "can't use two types of type analysis at once!\n";
        Boomerang::get()->conTypeAnalysis = false;
    }
    globalTypeAnalysis();


    if (!Boomerang::get()->noDecompile) {
        if (!Boomerang::get()->noRemoveReturns) {
            // A final pass to remove returns not used by any caller
            if (VERBOSE)
                LOG << "prog: global removing unused returns\n";
            // Repeat until no change. Note 100% sure if needed.
            while(removeUnusedReturns())
                ;
        }

        // print XML after removing returns
        for (Proc * pp : m_procs) {
            UserProc* proc = (UserProc*)pp;
            if (proc->isLib())
                continue;
            proc->printXML();
        }
    }

    if (VERBOSE)
        LOG << "transforming from SSA\n";

    // Now it is OK to transform out of SSA form
    fromSSAform();

    // removeUnusedLocals(); Note: is now in UserProc::generateCode()
    removeUnusedGlobals();
}
//! As the name suggests, removes globals unused in the decompiled code.
void Prog::removeUnusedGlobals() {
    if (VERBOSE)
        LOG << "removing unused globals\n";

    // seach for used globals
    std::list<Exp*> usedGlobals;
    for ( Proc* pp : m_procs ) {
        if (pp->isLib())
            continue;
        UserProc *u = (UserProc*)pp;
        Exp* search = new Location(opGlobal, new Terminal(opWild), u);
        // Search each statement in u, excepting implicit assignments (their uses don't count, since they don't really
        // exist in the program representation)
        StatementList stmts;
        StatementList::iterator ss;
        u->getStatements(stmts);
        for (Statement* s : stmts) {
            if (s->isImplicit())
                continue;            // Ignore the uses in ImplicitAssigns
            bool found = s->searchAll(search, usedGlobals);
            if (found && DEBUG_UNUSED)
                LOG << " a global is used by stmt " << s->getNumber() << "\n";
        }
    }

    // make a map to find a global by its name (could be a global var too)
    std::map<std::string, Global*> namedGlobals;
    for (Global* g : globals)
        namedGlobals[g->getName()] = g;

    // rebuild the globals vector
    const char* name;
    Global* usedGlobal;

    globals.clear();
    for (Exp *e : usedGlobals) {
        if (DEBUG_UNUSED)
            LOG << " " << e << " is used\n";
        name = ((Const*)e->getSubExp1())->getStr();
        usedGlobal=namedGlobals[name];
        if(usedGlobal) {
            globals.insert(usedGlobal);
        } else {
            LOG << "warning: an expression refers to a nonexistent global\n";
        }
    }
}

/***************************************************************************//**
 *
 * \brief    Remove unused return locations
 *
 * This is the global removing of unused and redundant returns. The initial idea
 * is simple enough: remove some returns according to the formula:
 * returns(p) = modifieds(p) isect union(live at c) for all c calling p.
 * However, removing returns reduces the uses, leading to three effects:
 * 1) The statement that defines the return, if only used by that return, becomes unused
 * 2) if the return is implicitly defined, then the parameters may be reduced, which affects all callers
 * 3) if the return is defined at a call, the location may no longer be live at the call. If not, you need to check
 *    the child, and do the union again (hence needing a list of callers) to find out if this change also affects that
 *    child.
 * \returns true if any change
 *
 ******************************************************************************/
bool Prog::removeUnusedReturns() {
    // For each UserProc. Each proc may process many others, so this may duplicate some work. Really need a worklist of
    // procedures not yet processed.
    // Define a workset for the procedures who have to have their returns checked
    // This will be all user procs, except those undecoded (-sf says just trust the given signature)
    std::set<UserProc*> removeRetSet;
    std::list<Proc*>::iterator pp;
    bool change=false;
    for (Proc * pp : m_procs) {
        UserProc* proc = (UserProc*)pp;
        if (proc->isLib() || !proc->isDecoded())
            continue; // e.g. use -sf file to just prototype the proc
        removeRetSet.insert(proc);
    }
    // The workset is processed in arbitrary order. May be able to do better, but note that sometimes changes propagate
    // down the call tree (no caller uses potential returns for child), and sometimes up the call tree (removal of
    // returns and/or dead code removes parameters, which affects all callers).
    while (removeRetSet.size()) {
        auto it = removeRetSet.begin();        // Pick the first element of the set
        change |= (*it)->removeRedundantReturns(removeRetSet);
        // Note: removing the currently processed item here should prevent unnecessary reprocessing of self recursive
        // procedures
        removeRetSet.erase(it);            // Remove the current element (may no longer be the first)
    }
    return change;
}

// Have to transform out of SSA form after the above final pass
//! Convert from SSA form
void Prog::fromSSAform() {
    for (Proc *pp : m_procs ) {
        UserProc* proc = (UserProc*)pp;
        if (proc->isLib())
            continue;
        if (VERBOSE) {
            LOG << "===== before transformation from SSA form for " << proc->getName() << " =====\n"
                << *proc
                << "===== end before transformation from SSA for " << proc->getName() << " =====\n\n";
            if (Boomerang::get()->dotFile)
                proc->printDFG();
        }
        proc->fromSSAform();
        if (VERBOSE) {
            LOG << "===== after transformation from SSA form for " << proc->getName() << " =====\n"
                << *proc
                << "===== end after transformation from SSA for " << proc->getName() << " =====\n\n";
        }
    }
}
//! Constraint based type analysis
void Prog::conTypeAnalysis() {
    if (VERBOSE || DEBUG_TA)
        LOG << "=== start constraint-based type analysis ===\n";
    // FIXME: This needs to be done bottom of the call-tree first, with repeat until no change for cycles
    // in the call graph
    for ( Proc * pp : m_procs ) {
        UserProc* proc = (UserProc*)pp;
        if (proc->isLib() || !proc->isDecoded() )
            continue;
        proc->conTypeAnalysis();
    }
    if (VERBOSE || DEBUG_TA)
        LOG << "=== end type analysis ===\n";
}

void Prog::globalTypeAnalysis() {
    if (VERBOSE || DEBUG_TA)
        LOG << "### start global data-flow-based type analysis ###\n";
    for ( Proc * pp : m_procs ) {
        UserProc* proc = (UserProc*)pp;
        if (proc->isLib() || !proc->isDecoded())
            continue;
        // FIXME: this just does local TA again. Need to meet types for all parameter/arguments, and return/results!
        // This will require a repeat until no change loop
        std::cout << "global type analysis for " << proc->getName() << "\n";
        proc->typeAnalysis();
    }
    if (VERBOSE || DEBUG_TA)
        LOG << "### end type analysis ###\n";
}

void Prog::rangeAnalysis() {
    for ( Proc * pp : m_procs ) {
        UserProc* proc = (UserProc*)pp;
        if (proc->isLib() || !proc->isDecoded())
            continue;
        proc->rangeAnalysis();
        proc->logSuspectMemoryDefs();
    }
}

void Prog::printCallGraph() {
    std::string fname1 = Boomerang::get()->getOutputPath() + "callgraph.out";
    std::string fname2 = Boomerang::get()->getOutputPath() + "callgraph.dot";
    int fd1 = lockFileWrite(fname1.c_str());
    int fd2 = lockFileWrite(fname2.c_str());
    std::ofstream f1(fname1.c_str());
    std::ofstream f2(fname2.c_str());
    std::set<Proc*> seen;
    std::map<Proc*, int> spaces;
    std::map<Proc*, Proc*> parent;
    std::list<Proc*> procList;
    f2 << "digraph callgraph {\n";
    std::copy(entryProcs.begin(),entryProcs.end(),std::back_inserter(procList));

    spaces[procList.front()] = 0;
    while (procList.size()) {
        Proc *p = procList.front();
        procList.erase(procList.begin());
        if (ADDRESS::host_ptr(p) == NO_ADDRESS)
            continue;
        if (seen.find(p) != seen.end())
            continue;
        seen.insert(p);
        int n = spaces[p];
        for (int i = 0; i < n; i++)
            f1 << "     ";
        f1 << p->getName() << " @ " << std::hex << p->getNativeAddress();
        if (parent.find(p) != parent.end())
            f1 << " [parent=" << parent[p]->getName() << "]";
        f1 << std::endl;
        if (!p->isLib()) {
            n++;
            UserProc *u = (UserProc*)p;
            std::list<Proc*> &calleeList = u->getCallees();
            for (auto it1 = calleeList.rbegin(); it1 != calleeList.rend(); it1++) {
                procList.push_front(*it1);
                spaces[*it1] = n;
                parent[*it1] = p;
                f2 << p->getName() << " -> " << (*it1)->getName() << ";\n";
            }
        }
    }
    f2 << "}\n";
    f1.close();
    f2.close();
    unlockFile(fd1);
    unlockFile(fd2);
}

void printProcsRecursive(Proc* proc, int indent, std::ofstream &f,std::set<Proc*> &seen) {
    bool fisttime=false;
    if (seen.find(proc) == seen.end()) {
        seen.insert(proc);
        fisttime=true;
    }
    for (int i = 0; i < indent; i++)
        f << "     ";

    if(!proc->isLib() && fisttime) { // seen lib proc
        f << proc->getNativeAddress();
        f << " __nodecode __incomplete void " << proc->getName() << "();\n";

        UserProc *u = (UserProc*)proc;
        for (Proc* callee : u->getCallees()) {
            printProcsRecursive(callee,indent+1,f,seen);
        }
        for (int i = 0; i < indent; i++)
            f << "     ";
        f << "// End of " << proc->getName() << "\n";
    } else {
        f << "// " << proc->getName() << "();\n";
    }
}

void Prog::printSymbolsToFile() {
    std::cerr << "entering Prog::printSymbolsToFile\n";
    std::string fname = Boomerang::get()->getOutputPath() + "symbols.h";
    int fd = lockFileWrite(fname.c_str());
    std::ofstream f(fname.c_str());

    /* Print procs */
    f << "/* Functions: */\n";
    std::set<Proc*> seen;
    for ( UserProc * up : entryProcs )
        printProcsRecursive(up, 0, f, seen);

    f << "/* Leftovers: */\n";
    std::list<Proc*>::iterator it; // don't forget the rest
    for ( Proc *pp : m_procs ) {
        if (!pp->isLib() && seen.find(pp) == seen.end()) {
            printProcsRecursive(*it,0,f,seen);
        }
    }

    f.close();
    unlockFile(fd);
    std::cerr << "leaving Prog::printSymbolsToFile\n";
}

void Prog::printCallGraphXML() {
    if (!Boomerang::get()->dumpXML)
        return;
    std::list<Proc*>::iterator it;
    for (it = m_procs.begin(); it != m_procs.end(); it++)
        (*it)->clearVisited();
    std::string fname = Boomerang::get()->getOutputPath() + "callgraph.xml";
    int fd = lockFileWrite(fname.c_str());
    std::ofstream f(fname.c_str());
    f << "<prog name=\"" << getName() << "\">\n";
    f << "     <callgraph>\n";
    std::list<UserProc*>::iterator pp;
    for ( UserProc * up : entryProcs )
        up->printCallGraphXML(f, 2);
    for ( Proc *pp : m_procs ) {
        if (!pp->isVisited() && !pp->isLib()) {
            pp->printCallGraphXML(f, 2);
        }
    }
    f << "     </callgraph>\n";
    f << "</prog>\n";
    f.close();
    unlockFile(fd);
}

void Prog::readSymbolFile(const char *fname) {
    std::ifstream ifs;

    ifs.open(fname);

    if (!ifs.good()) {
        LOG << "can't open `" << fname << "'\n";
        exit(1);
    }

    AnsiCParser *par = new AnsiCParser(ifs, false);
    platform plat = getFrontEndId();
    callconv cc = CONV_C;
    if (isWin32()) cc = CONV_PASCAL;
    par->yyparse(plat, cc);

    for (Symbol* sym : par->symbols) {
        if (sym->sig) {
            Proc* p = newProc(sym->sig->getName(), sym->addr, pBF->IsDynamicLinkedProcPointer(sym->addr) ||
                              // NODECODE isn't really the right modifier; perhaps we should have a LIB modifier,
                              // to specifically specify that this function obeys library calling conventions
                              sym->mods->noDecode);
            if (!sym->mods->incomplete) {
                p->setSignature(sym->sig->clone());
                p->getSignature()->setForced(true);
            }
        } else {
            const char *nam = sym->nam.c_str();
            if (strlen(nam) == 0) {
                nam = newGlobalName(sym->addr);
            }
            Type *ty = sym->ty;
            if (ty == nullptr) {
                ty = guessGlobalType(nam, sym->addr);
            }
            globals.insert(new Global(ty, sym->addr, nam));
        }
    }

    for (SymbolRef* ref : par->refs) {
        pFE->addRefHint(ref->addr, ref->nam.c_str());
    }

    delete par;
    ifs.close();
}

Global::~Global() {
    // Do-nothing d'tor
}
//! Get the initial value as an expression (or nullptr if not initialised)
Exp* Global::getInitialValue(Prog* prog) {
    SectionInfo *si = prog->getSectionInfoByAddr(uaddr);
    if (si && si->isAddressBss(uaddr))
        // This global is in the BSS, so it can't be initialised
        //NOTE: this is not actually correct. at least for typing, BSS data can have a type assigned
        return nullptr;
    if (si == nullptr)
        return nullptr;
    return prog->readNativeAs(uaddr, type);
}

void Global::print(std::ostream& os, Prog* prog) {
    Exp* init = getInitialValue(prog);
    os << type << " " << nam << " at " << std::hex << uaddr << std::dec << " initial value " <<
          (init ? init->prints() : "<none>");
}

Exp *Prog::readNativeAs(ADDRESS uaddr, Type *type) {
    Exp *e = nullptr;
    SectionInfo *si = getSectionInfoByAddr(uaddr);
    if (si == nullptr)
        return nullptr;
    if (type->resolvesToPointer()) {
        ADDRESS init = ADDRESS::g(readNative4(uaddr));
        if ( init.isZero() )
            return new Const(0);
        const char *nam = getGlobalName(init);
        if (nam != nullptr)
            // TODO: typecast?
            return Location::global(nam, nullptr);
        if (type->asPointer()->getPointsTo()->resolvesToChar()) {
            const char *str = getStringConstant(init);
            if (str != nullptr)
                return new Const(str);
        }
    }
    if (type->resolvesToCompound()) {
        CompoundType *c = type->asCompound();
        Exp *n = e = new Terminal(opNil);
        for (unsigned int i = 0; i < c->getNumTypes(); i++) {
            ADDRESS addr = uaddr + c->getOffsetTo(i) / 8;
            Type *t = c->getType(i);
            Exp *v = readNativeAs(addr, t);
            if (v == nullptr) {
                LOG << "unable to read native address " << addr << " as type " << t->getCtype() << "\n";
                v = new Const(-1);
            }
            if (n->isNil()) {
                n = new Binary(opList, v, n);
                e = n;
            } else {
                assert(n->getSubExp2()->getOper() == opNil);
                n->setSubExp2(new Binary(opList, v, n->getSubExp2()));
                n = n->getSubExp2();
            }
        }
        return e;
    }
    if (type->resolvesToArray() && type->asArray()->getBaseType()->resolvesToChar()) {
        const char* str = getStringConstant(uaddr, true);
        if (str) {
            // Make a global string
            return new Const(str);
        }
    }
    if (type->resolvesToArray()) {
        int nelems = -1;
        const char *nam = getGlobalName(uaddr);
        int base_sz = type->asArray()->getBaseType()->getSize() / 8;
        if (nam != nullptr)
            nelems = pBF->GetSizeByName(nam) / base_sz;
        Exp *n = e = new Terminal(opNil);
        for (int i = 0; nelems == -1 || i < nelems; i++) {
            Exp *v = readNativeAs(uaddr + i * base_sz, type->asArray()->getBaseType());
            if (v == nullptr)
                break;
            if (n->isNil()) {
                n = new Binary(opList, v, n);
                e = n;
            } else {
                assert(n->getSubExp2()->getOper() == opNil);
                n->setSubExp2(new Binary(opList, v, n->getSubExp2()));
                n = n->getSubExp2();
            }
            // "null" terminated
            if (nelems == -1 && v->isConst() && ((Const*)v)->getInt() == 0)
                break;
        }
    }
    if (type->resolvesToInteger() || type->resolvesToSize()) {
        int size;
        if (type->resolvesToInteger())
            size = type->asInteger()->getSize();
        else
            size = type->asSize()->getSize();
        switch(size) {
            case 8:
                e = new Const(readNative1(uaddr));
                break;
            case 16:
                // Note: must respect endianness
                e = new Const(readNative2(uaddr));
                break;
            case 32:
                e = new Const(readNative4(uaddr));
                break;
            case 64:
                e = new Const(readNative8(uaddr));
                break;
        }
    }
    if (type->resolvesToFloat()) {
        switch(type->asFloat()->getSize()) {
            case 32:
                e = new Const(readNativeFloat4(uaddr));
                break;
            case 64:
                e = new Const(readNativeFloat8(uaddr));
                break;
        }
    }
    return e;
}

void Global::meetType(Type* ty) {
    bool ch;
    type = type->meetWith(ty, ch);
}
//! Re-decode this proc from scratch
void Prog::reDecode(UserProc* proc) {
    std::ofstream os;
    pFE->processProc(proc->getNativeAddress(), proc, os);
}




#ifdef USING_MEMO
class ClusterMemo : public Memo {
public:
    ClusterMemo(int mId) : Memo(mId) { }

    std::string name;
    std::vector<Cluster*> children;
    Cluster *parent;
};

Memo *Cluster::makeMemo(int mId) {
    ClusterMemo *m = new ClusterMemo(mId);
    m->name = name;
    m->children = children;
    m->parent = parent;
    return m;
}

void Cluster::readMemo(Memo *mm, bool dec) {
    ClusterMemo *m = dynamic_cast<ClusterMemo*>(mm);

    name = m->name;
    children = m->children;
    parent = m->parent;

    for (std::vector<Cluster*>::iterator it = children.begin(); it != children.end(); it++)
        (*it)->restoreMemo(m->mId, dec);
}

class GlobalMemo : public Memo {
public:
    GlobalMemo(int mId) : Memo(mId) { }

    Type *type;
    ADDRESS uaddr;
    std::string nam;
};

Memo *Global::makeMemo(int mId) {
    GlobalMemo *m = new GlobalMemo(mId);
    m->type = type;
    m->uaddr = uaddr;
    m->nam = nam;

    type->takeMemo(mId);
    return m;
}

void Global::readMemo(Memo *mm, bool dec) {
    GlobalMemo *m = dynamic_cast<GlobalMemo*>(mm);

    type = m->type;
    uaddr = m->uaddr;
    nam = m->nam;

    type->restoreMemo(m->mId, dec);
}

class ProgMemo : public Memo {
public:
    ProgMemo(int m) : Memo(m) { }

    std::string m_name, m_path;
    std::list<Proc*> m_procs;
    PROGMAP m_procLabels;
    std::set<Global*> globals;
    DataIntervalMap globalMap;
    int m_iNumberedProc;
    Cluster *m_rootCluster;
};

Memo *Prog::makeMemo(int mId) {
    ProgMemo *m = new ProgMemo(mId);
    m->m_name = m_name;
    m->m_path = m_path;
    m->m_procs = m_procs;
    m->m_procLabels = m_procLabels;
    m->globals = globals;
    m->globalMap = globalMap;
    m->m_iNumberedProc = m_iNumberedProc;
    m->m_rootCluster = m_rootCluster;

    for (std::list<Proc*>::iterator it = m_procs.begin(); it != m_procs.end(); it++)
        (*it)->takeMemo(m->mId);
    m_rootCluster->takeMemo(m->mId);
    for (std::set<Global*>::iterator it = globals.begin(); it != globals.end(); it++)
        (*it)->takeMemo(m->mId);

    return m;
}

void Prog::readMemo(Memo *mm, bool dec) {
    ProgMemo *m = dynamic_cast<ProgMemo*>(mm);
    m_name = m->m_name;
    m_path = m->m_path;
    m_procs = m->m_procs;
    m_procLabels = m->m_procLabels;
    globals = m->globals;
    globalMap = m->globalMap;
    m_iNumberedProc = m->m_iNumberedProc;
    m_rootCluster = m->m_rootCluster;

    for (std::list<Proc*>::iterator it = m_procs.begin(); it != m_procs.end(); it++)
        (*it)->restoreMemo(m->mId, dec);
    m_rootCluster->restoreMemo(m->mId, dec);
    for (std::set<Global*>::iterator it = globals.begin(); it != globals.end(); it++)
        (*it)->restoreMemo(m->mId, dec);
}

/*

        After every undoable operation:

                                                                                 ? ||
                prog->takeMemo();                 1 |.1|

                always deletes any memos before the cursor, adds new memo leaving cursor pointing to new memo


        For first undo:

                                                                                 ? |.4321|
                prog->restoreMemo(inc);             4 |5.4321|

                takes a memo of previous state, always leaves cursor pointing at current state

        For second undo:
                                                                                 4 |5.4321|
                prog->restoreMemo(inc);             3 |54.321|

        To redo:

                                                                                 3    |54.321|
                prog->restoreMemo(dec);             4    |5.4321|

 */

void Memoisable::takeMemo(int mId) {
    if (cur_memo != memos.end() && (*cur_memo)->mId == mId && mId != -1)
        return;

    if (cur_memo != memos.begin()) {
        std::list<Memo*>::iterator it = memos.begin();
        while (it != cur_memo)
            it = memos.erase(it);
    }

    if (mId == -1) {
        if (cur_memo == memos.end())
            mId = 1;
        else
            mId = memos.front()->mId + 1;
    }

    Memo *m = makeMemo(mId);

    memos.push_front(m);
    cur_memo = memos.begin();
}

void Memoisable::restoreMemo(int mId, bool dec) {
    if (memos.begin() == memos.end())
        return;

    if ((*cur_memo)->mId == mId && mId != -1)
        return;

    if (dec) {
        if (cur_memo == memos.begin())
            return;
        cur_memo--;
    } else {
        cur_memo++;
        if (cur_memo == memos.end()) {
            cur_memo--;
            return;
        }
    }

    Memo *m = *cur_memo;
    if (m->mId != mId && mId != -1)
        return;

    readMemo(m, dec);
}


bool Memoisable::canRestore(bool dec) {
    if (memos.begin() == memos.end())
        return false;

    if (dec) {
        if (cur_memo == memos.begin())
            return false;
    } else {
        cur_memo++;
        if (cur_memo == memos.end()) {
            cur_memo--;
            return false;
        }
        cur_memo--;
    }
    return true;
}

void Memoisable::takeMemo() {
    takeMemo(-1);
}

void Memoisable::restoreMemo(bool dec) {
    restoreMemo(-1, dec);
}

#endif        // #ifdef USING_MEMO

void Prog::decodeFragment(UserProc* proc, ADDRESS a) {
    if (a >= pBF->getLimitTextLow() && a < pBF->getLimitTextHigh())
        pFE->decodeFragment(proc, a);
    else {
        std::cerr << "attempt to decode fragment outside text area, addr=" << a << "\n";
        if (VERBOSE)
            LOG << "attempt to decode fragment outside text area, addr=" << a << "\n";
    }
}
//
//
/***************************************************************************//**
 *
 * \brief This does extra processing on a constant.
 *
 * The Exp* \a e is expected to be a Const, and the ADDRESS \a lc is the native
 * location from which the constant was read.
 *
 * \returns processed Exp
 ******************************************************************************/
Exp * Prog::addReloc(Exp *e, ADDRESS lc) {
    assert(e->isConst());
    Const *c = (Const*)e;

    if (!pBF->IsRelocationAt(lc))
        return e;

    // relocations have been applied to the constant, so if there is a
    // relocation for this lc then we should be able to replace the constant
    // with a symbol.
    std::map<ADDRESS, std::string> &symbols = pBF->getSymbols();
    ADDRESS c_addr = ADDRESS::g(c->getInt());
    auto found_at = symbols.find(c_addr);
    if (found_at != symbols.end()) { //TODO: use getAddr instead of getInt
        const char *n = found_at->second.c_str();
        unsigned int sz = pBF->GetSizeByName(n);
        if (getGlobal((char*)n) == nullptr) {
            Global *global = new Global(new SizeType(sz*8), c_addr, n);
            globals.insert(global);
        }
        e = new Unary(opAddrOf, Location::global(n, nullptr));
    } else {
        const char *str = getStringConstant(c_addr);
        if (str)
            e = new Const(str);
        else {
            // check for accesses into the middle of symbols
            for (auto it : symbols) {
                if (it.first < c_addr && it.first + pBF->GetSizeByName(it.second.c_str()) > c_addr) {
                    int off = (c->getAddr() - it.first).m_value;
                    e = new Binary(opPlus,
                                   new Unary(opAddrOf, Location::global(it.second.c_str(), nullptr)),
                                   new Const(off));
                    break;
                }
            }
        }
    }
    return e;
}

