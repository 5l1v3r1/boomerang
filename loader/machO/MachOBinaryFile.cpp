/*
 * Copyright (C) 2000, The University of Queensland
 * Copyright (C) 2001, Sun Microsystems, Inc
 * Copyright (C) 2002, Trent Waddington
 *
 * See the file "LICENSE.TERMS" for information on usage and
 * redistribution of this file, and for a DISCLAIMER OF ALL
 * WARRANTIES.
 *
 */

/** \file MachOBinaryFile.cpp
 * \brief This file contains the implementation of the class MachOBinaryFile.
 *
 *    This file implements the class MachOBinaryFile, derived from class
 *    BinaryFile. See MachOBinaryFile.h and BinaryFile.h for details.
 */

#include "MachOBinaryFile.h"

#include "BinaryFile.h"
#include "boomerang.h"
#include "IBinaryImage.h"
#include "config.h"
#include "nlist.h"
#include "macho-apple.h"

#include "objc/objc-class.h"
#include "objc/objc-runtime.h"

#include <cstdarg>
#include <cassert>
#include <cstring>
#include <cstdlib>

namespace {

struct SectionParam {
    QString Name;
    ADDRESS from;
    size_t Size;
    ADDRESS ImageAddress;
    bool Bss,Code,Data,ReadOnly;
};

}

//#define DEBUG_MACHO_LOADER
//#define DEBUG_MACHO_LOADER_OBJC

MachOBinaryFile::MachOBinaryFile() {
    machine = MACHINE_PPC;
    swap_bytes = false;
    Image = Boomerang::get()->getImage();
}

MachOBinaryFile::~MachOBinaryFile() {
}

bool MachOBinaryFile::Open(const char *sName) {
    Q_UNUSED(sName);
    // return Load(sName) != 0;
    return false;
}

void MachOBinaryFile::Close() { UnLoad(); }

std::list<SectionInfo *> &MachOBinaryFile::GetEntryPoints(const char *pEntry) {
    Q_UNUSED(pEntry);
    fprintf(stderr, "really don't know how to implement GetEntryPoints\n");
    exit(0);
    static std::list<SectionInfo *> l;
    return l;
}

ADDRESS MachOBinaryFile::GetEntryPoint() { return entrypoint; }

ADDRESS MachOBinaryFile::GetMainEntryPoint() {
    ADDRESS aMain = GetAddressByName("main", true);
    if (aMain != NO_ADDRESS)
        return aMain;
    aMain = GetAddressByName("_main", true);
    if (aMain != NO_ADDRESS)
        return aMain;

    return NO_ADDRESS;
}

#define BE4(x) ((magic[(x)] << 24) | (magic[(x)+1] << 16) | (magic[(x)+2] << 8) | (magic[(x)+3]))

bool MachOBinaryFile::RealLoad(const QString &sName) {
    m_pFileName = sName;
    FILE *fp = fopen(qPrintable(sName), "rb");
    unsigned int imgoffs = 0;

    unsigned char magic[12 * 4];
    fread(magic, sizeof(magic), 1, fp);

    if (magic[0] == 0xca && magic[1] == 0xfe && magic[2] == 0xba && magic[3] == 0xbe) {
        int nimages = BE4(4);
#ifdef DEBUG_MACHO_LOADER
        printf("binary is universal with %d images\n", nimages);
#endif
        for (int i = 0; i < nimages; i++) {
            int fbh = 8 + i * 5 * 4;
            unsigned int cputype = BE4(fbh);
            unsigned int offset = BE4(fbh + 8);
#ifdef DEBUG_MACHO_LOADER
            unsigned int cpusubtype = BE4(fbh + 4);
            unsigned int size = BE4(fbh + 12);
            unsigned int pad = BE4(fbh + 16);
            printf("cputype: %08x\n", cputype);
            printf("cpusubtype: %08x\n", cpusubtype);
            printf("offset: %08x\n", offset);
            printf("size: %08x\n", size);
            printf("pad: %08x\n", pad);
#endif

            if (cputype == 0x7) // i386
                imgoffs = offset;
        }
    }

    fseek(fp, imgoffs, SEEK_SET);
    header = new struct mach_header;
    fread(header, sizeof(*header), 1, fp);

    if ((header->magic != MH_MAGIC) && (_BMMH(header->magic) != MH_MAGIC)) {
        fclose(fp);
        fprintf(stderr, "error loading file %s, bad Mach-O magic\n", qPrintable(sName));
        return false;
    }

    // check for swapped bytes
    swap_bytes = (_BMMH(header->magic) == MH_MAGIC);

    // Determine CPU type
    if (BMMH(header->cputype) == 0x07)
        machine = MACHINE_PENTIUM;
    else
        machine = MACHINE_PPC;

    sections.clear();
    std::vector<struct segment_command> segments;
    std::vector<struct nlist> symbols;
    // uint32_t startundef, nundef;
    // uint32_t  startlocal, nlocal,ndef, startdef;
    std::vector<struct section> stubs_sects;
    char *strtbl = nullptr;
    unsigned *indirectsymtbl = nullptr;
    ADDRESS objc_symbols = NO_ADDRESS, objc_modules = NO_ADDRESS, objc_strings = NO_ADDRESS, objc_refs = NO_ADDRESS;
    unsigned objc_modules_size = 0;

    fseek(fp, imgoffs + sizeof(*header), SEEK_SET);
    for (unsigned i = 0; i < BMMH(header->ncmds); i++) {
        struct load_command cmd;
        long pos = ftell(fp);
        fread(&cmd, 1, sizeof(struct load_command), fp);
        fseek(fp, pos, SEEK_SET);
        switch (BMMH(cmd.cmd)) {
        case LC_SEGMENT: {
            struct segment_command seg;
            fread(&seg, 1, sizeof(seg), fp);
            segments.push_back(seg);
#ifdef DEBUG_MACHO_LOADER
            fprintf(stdout, "seg addr %x size %i fileoff %x filesize %i flags %x\n", BMMH(seg.vmaddr), BMMH(seg.vmsize),
                    BMMH(seg.fileoff), BMMH(seg.filesize), BMMH(seg.flags));
#endif
            for (unsigned n = 0; n < BMMH(seg.nsects); n++) {
                struct section sect;
                fread(&sect, 1, sizeof(sect), fp);
                sections.push_back(sect);
#ifdef DEBUG_MACHO_LOADER
                fprintf(stdout, "    sectname %s segname %s addr %x size %i flags %x\n", sect.sectname, sect.segname,
                        BMMH(sect.addr), BMMH(sect.size), BMMH(sect.flags));
#endif
                if ((BMMH(sect.flags) & SECTION_TYPE) == S_SYMBOL_STUBS) {
                    stubs_sects.push_back(sect);
#ifdef DEBUG_MACHO_LOADER
                    fprintf(stdout, "        symbol stubs section, start index %i, stub size %i\n",
                            BMMH(sect.reserved1), BMMH(sect.reserved2));
#endif
                }
                if (!strcmp(sect.sectname, SECT_OBJC_SYMBOLS)) {
                    assert(objc_symbols == NO_ADDRESS);
                    objc_symbols = BMMH(sect.addr);
                }
                if (!strcmp(sect.sectname, SECT_OBJC_MODULES)) {
                    assert(objc_modules == NO_ADDRESS);
                    objc_modules = BMMH(sect.addr);
                    objc_modules_size = BMMH(sect.size);
                }
                if (!strcmp(sect.sectname, SECT_OBJC_STRINGS)) {
                    assert(objc_strings == NO_ADDRESS);
                    objc_strings = BMMH(sect.addr);
                }
                if (!strcmp(sect.sectname, SECT_OBJC_REFS)) {
                    assert(objc_refs == NO_ADDRESS);
                    objc_refs = BMMH(sect.addr);
                }
            }
        } break;
        case LC_SYMTAB: {
            struct symtab_command syms;
            fread(&syms, 1, sizeof(syms), fp);
            fseek(fp, imgoffs + BMMH(syms.stroff), SEEK_SET);
            strtbl = new char[BMMH(syms.strsize)];
            fread(strtbl, 1, BMMH(syms.strsize), fp);
            fseek(fp, imgoffs + BMMH(syms.symoff), SEEK_SET);
            for (unsigned n = 0; n < BMMH(syms.nsyms); n++) {
                struct nlist sym;
                fread(&sym, 1, sizeof(sym), fp);
                symbols.push_back(sym);
#ifdef DEBUG_MACHO_LOADER
// fprintf(stdout, "got sym %s flags %x value %x\n", strtbl + BMMH(sym.n_un.n_strx), sym.n_type, BMMH(sym.n_value));
#endif
            }
#ifdef DEBUG_MACHO_LOADER
            fprintf(stdout, "symtab contains %i symbols\n", BMMH(syms.nsyms));
#endif
        } break;
        case LC_DYSYMTAB: {
            struct dysymtab_command syms;
            fread(&syms, 1, sizeof(syms), fp);
#ifdef DEBUG_MACHO_LOADER
            fprintf(stdout, "dysymtab local %i %i defext %i %i undef %i %i\n", BMMH(syms.ilocalsym),
                    BMMH(syms.nlocalsym), BMMH(syms.iextdefsym), BMMH(syms.nextdefsym), BMMH(syms.iundefsym),
                    BMMH(syms.nundefsym));
#endif
// TODO: find uses for values below
// startlocal = BMMH(syms.ilocalsym);
// nlocal = BMMH(syms.nlocalsym);
// startdef = BMMH(syms.iextdefsym);
// ndef = BMMH(syms.nextdefsym);
// startundef = BMMH(syms.iundefsym);
// nundef = BMMH(syms.nundefsym);

#ifdef DEBUG_MACHO_LOADER
            fprintf(stdout, "dysymtab has %i indirect symbols: ", BMMH(syms.nindirectsyms));
#endif
            indirectsymtbl = new unsigned[BMMH(syms.nindirectsyms)];
            fseek(fp, imgoffs + BMMH(syms.indirectsymoff), SEEK_SET);
            fread(indirectsymtbl, 1, BMMH(syms.nindirectsyms) * sizeof(unsigned), fp);
#ifdef DEBUG_MACHO_LOADER
            for (unsigned j = 0; j < BMMH(syms.nindirectsyms); j++) {
                fprintf(stdout, "%i ", BMMH(indirectsymtbl[j]));
            }
            fprintf(stdout, "\n");
#endif
        } break;
        default:
#ifdef DEBUG_MACHO_LOADER
            fprintf(stderr, "not handled load command %x\n", BMMH(cmd.cmd));
#endif
            // yep, there's lots of em
            break;
        }

        fseek(fp, pos + BMMH(cmd.cmdsize), SEEK_SET);
    }

    struct segment_command *lowest = &segments[0], *highest = &segments[0];
    for (unsigned i = 1; i < segments.size(); i++) {
        if (BMMH(segments[i].vmaddr) < BMMH(lowest->vmaddr))
            lowest = &segments[i];
        if (BMMH(segments[i].vmaddr) > BMMH(highest->vmaddr))
            highest = &segments[i];
    }

    loaded_addr = BMMH(lowest->vmaddr);
    loaded_size = BMMH(highest->vmaddr) - BMMH(lowest->vmaddr) + BMMH(highest->vmsize);

    base = (char *)malloc(loaded_size);

    if (!base) {
        fclose(fp);
        fprintf(stderr, "Cannot allocate memory for copy of image\n");
        return false;
    }

    for (unsigned i = 0; i < segments.size(); i++) {
        fseek(fp, imgoffs + BMMH(segments[i].fileoff), SEEK_SET);
        ADDRESS a = ADDRESS::g(BMMH(segments[i].vmaddr));
        unsigned sz = BMMH(segments[i].vmsize);
        unsigned fsz = BMMH(segments[i].filesize);
        memset(base + a.m_value - loaded_addr.m_value, 0, sz);
        fread(base + a.m_value - loaded_addr.m_value, 1, fsz, fp);
#ifdef DEBUG_MACHO_LOADER
        fprintf(stderr, "loaded segment %x %i in mem %i in file\n", a, sz, fsz);
#endif
        QString name = QByteArray(segments[i].segname,17);
        SectionInfo *sect = Image->createSection(name,ADDRESS::n(BMMH(segments[i].vmaddr)),
                                          ADDRESS::n(BMMH(segments[i].vmaddr)+BMMH(segments[i].vmsize)));
        assert(sect);
        sect->uHostAddr = ADDRESS::value_type(base) + BMMH(segments[i].vmaddr) - loaded_addr.m_value;
        assert((sect->uHostAddr + sect->uSectionSize) <= ADDRESS::host_ptr(base + loaded_size));

        unsigned long l = BMMH(segments[i].initprot);
        sect->bBss = false; // TODO
        sect->Endiannes = (machine == MACHINE_PPC) ? 1 : 0;
        sect->bCode = l & VM_PROT_EXECUTE ? 1 : 0;
        sect->bData = l & VM_PROT_READ ? 1 : 0;
        sect->bReadOnly = ~(l & VM_PROT_WRITE) ? 0 : 1;
#ifdef DEBUG_MACHO_LOADER
        fprintf(stderr, "loaded segment %x %i in mem %i in file code=%i data=%i readonly=%i\n", a, sz, fsz,
                sect.bCode, sect.bData, sect.bReadOnly);
#endif
    }

    // process stubs_sects
    for (unsigned j = 0; j < stubs_sects.size(); j++) {
        for (unsigned i = 0; i < BMMH(stubs_sects[j].size) / BMMH(stubs_sects[j].reserved2); i++) {
            unsigned startidx = BMMH(stubs_sects[j].reserved1);
            unsigned symbol = BMMH(indirectsymtbl[startidx + i]);
            ADDRESS addr = ADDRESS::g(BMMH(stubs_sects[j].addr) + i * BMMH(stubs_sects[j].reserved2));
#ifdef DEBUG_MACHO_LOADER
            fprintf(stdout, "stub for %s at %x\n", strtbl + BMMH(symbols[symbol].n_un.n_strx), addr);
#endif
            char *name = strtbl + BMMH(symbols[symbol].n_un.n_strx);
            if (*name == '_') // we want printf not _printf
                name++;
            m_SymA[addr] = name;
            dlprocs[addr] = name;
        }
    }

    // process the remaining symbols
    for (unsigned i = 0; i < symbols.size(); i++) {
        char *name = strtbl + BMMH(symbols[i].n_un.n_strx);
        if (BMMH(symbols[i].n_un.n_strx) != 0 && BMMH(symbols[i].n_value) != 0 && *name != 0) {

#ifdef DEBUG_MACHO_LOADER
            fprintf(stdout, "symbol %s at %x type %x\n", name, BMMH(symbols[i].n_value),
                    BMMH(symbols[i].n_type) & N_TYPE);
#endif
            if (*name == '_') // we want main not _main
                name++;
            m_SymA[ADDRESS::g(BMMH(symbols[i].n_value))] = name;
        }
    }

    // process objective-c section
    if (objc_modules != NO_ADDRESS) {
#ifdef DEBUG_MACHO_LOADER_OBJC
        fprintf(stdout, "processing objective-c section\n");
#endif
        for (unsigned i = 0; i < objc_modules_size;) {
            struct objc_module *module =
                (struct objc_module *)(ADDRESS::host_ptr(base) + objc_modules - loaded_addr + i).m_value;
            char *name = (char *)(intptr_t(base) + BMMH(module->name) - loaded_addr.m_value);
            Symtab symtab = (Symtab)(ADDRESS::host_ptr(base) + BMMH(module->symtab) - loaded_addr).m_value;
#ifdef DEBUG_MACHO_LOADER_OBJC
            fprintf(stdout, "module %s (%i classes)\n", name, BMMHW(symtab->cls_def_cnt));
#endif
            ObjcModule *m = &modules[name];
            m->name = name;
            for (unsigned j = 0; j < BMMHW(symtab->cls_def_cnt); j++) {
                struct objc_class *def = (struct objc_class *)(base + BMMH(symtab->defs[j]) - loaded_addr.m_value);
                char *name = (char *)(ADDRESS::value_type(base) + BMMH(def->name) - loaded_addr.m_value);
#ifdef DEBUG_MACHO_LOADER_OBJC
                fprintf(stdout, "  class %s\n", name);
#endif
                ObjcClass *cl = &m->classes[name];
                cl->name = name;
                struct objc_ivar_list *ivars = (struct objc_ivar_list *)(base + BMMH(def->ivars) - loaded_addr.m_value);
                for (unsigned k = 0; k < static_cast<unsigned int>(BMMH(ivars->ivar_count)); k++) {
                    struct objc_ivar *ivar = &ivars->ivar_list[k];
                    char *name = (char *)(ADDRESS::value_type(base) + BMMH(ivar->ivar_name) - loaded_addr.m_value);
                    char *types = (char *)(ADDRESS::value_type(base) + BMMH(ivar->ivar_type) - loaded_addr.m_value);
#ifdef DEBUG_MACHO_LOADER_OBJC
                    fprintf(stdout, "    ivar %s %s %x\n", name, types, BMMH(ivar->ivar_offset));
#endif
                    ObjcIvar *iv = &cl->ivars[name];
                    iv->name = name;
                    iv->type = types;
                    iv->offset = BMMH(ivar->ivar_offset);
                }
                // this is weird, why is it defined as a ** in the struct but used as a * in otool?
                struct objc_method_list *methods =
                    (struct objc_method_list *)(intptr_t(base) + BMMH(def->methodLists) - loaded_addr.m_value);
                for (unsigned k = 0; k < static_cast<unsigned int>(BMMH(methods->method_count)); k++) {
                    struct objc_method *method = &methods->method_list[k];
                    char *name = (char *)(intptr_t(base) + BMMH(method->method_name) - loaded_addr.m_value);
                    char *types = (char *)(intptr_t(base) + BMMH(method->method_types) - loaded_addr.m_value);
#ifdef DEBUG_MACHO_LOADER_OBJC
                    fprintf(stdout, "    method %s %s %x\n", name, types, BMMH((void *)method->method_imp));
#endif
                    ObjcMethod *me = &cl->methods[name];
                    me->name = name;
                    me->types = types;
                    me->addr = ADDRESS::g(BMMH(method->method_imp));
                }
            }
            i += BMMH(module->size);
        }
    }

    // Give the entry point a symbol
    // ADDRESS entry = GetMainEntryPoint();
    entrypoint = GetMainEntryPoint();

    fclose(fp);
    return true;
}

// Clean up and unload the binary image
void MachOBinaryFile::UnLoad() {}

bool MachOBinaryFile::PostLoad(void *handle) {
    Q_UNUSED(handle);
    return false;
}

QString MachOBinaryFile::symbolByAddress(ADDRESS dwAddr) {
    tMapAddrToString::iterator it = m_SymA.find(dwAddr);
    if (it == m_SymA.end())
        return 0;
    return it->second;
}

ADDRESS MachOBinaryFile::GetAddressByName(const QString &pName, bool bNoTypeOK /* = false */) {
    Q_UNUSED(bNoTypeOK);
    // This is "looking up the wrong way" and hopefully is uncommon
    // Use linear search
    tMapAddrToString::iterator it = m_SymA.begin();
    while (it != m_SymA.end()) {
        // std::cerr << "Symbol: " << it->second.c_str() << " at 0x" << std::hex << it->first << "\n";
        if (it->second == pName)
            return it->first;
        it++;
    }
    return NO_ADDRESS;
}

void MachOBinaryFile::AddSymbol(ADDRESS uNative, const QString &pName) { m_SymA[uNative] = pName; }

int MachOBinaryFile::GetSizeByName(const QString &pName, bool bTypeOK) {
    if (GetAddressByName(pName, bTypeOK) != NO_ADDRESS) {
        return 4; // TODO: HACK
    }
    return 0;
}

ADDRESS *MachOBinaryFile::GetImportStubs(int &numImports) {
    numImports = 0;
    return nullptr;
}

QString MachOBinaryFile::getFilenameSymbolFor(const char *) { return ""; }

bool MachOBinaryFile::DisplayDetails(const char *fileName, FILE *f
                                     /* = stdout */) {
    Q_UNUSED(fileName);
    Q_UNUSED(f);
    return false;
}

int MachOBinaryFile::machORead2(short *ps) const {
    unsigned char *p = (unsigned char *)ps;
    int n;
    if (machine == MACHINE_PPC)
        n = (int)(p[1] + (p[0] << 8));
    else
        n = (int)(p[0] + (p[1] << 8));
    return n;
}

int MachOBinaryFile::machORead4(int *pi) const {
    short *p = (short *)pi;
    int n1 = machORead2(p);
    int n2 = machORead2(p + 1);
    int n;
    if (machine == MACHINE_PPC)
        n = (int)(n2 | (n1 << 16));
    else
        n = (int)(n1 | (n2 << 16));
    return n;
}

// unsigned int MachOBinaryFile::BMMH(long int & x) {
//    if (swap_bytes) return _BMMH(x); else return x;
//}

int32_t MachOBinaryFile::BMMH(int32_t x) {
    if (swap_bytes)
        return _BMMH(x);
    else
        return x;
}

uint32_t MachOBinaryFile::BMMH(uint32_t x) {
    if (swap_bytes)
        return _BMMH(x);
    else
        return x;
}

unsigned short MachOBinaryFile::BMMHW(unsigned short x) {
    if (swap_bytes)
        return _BMMHW(x);
    else
        return x;
}
bool MachOBinaryFile::isReadOnly(ADDRESS uEntry) {
    uint32_t entry_val(uEntry.m_value);
    for (size_t i = 0; i < sections.size(); i++) {
        if (entry_val >= BMMH(sections[i].addr) && entry_val < BMMH(sections[i].addr) + BMMH(sections[i].size)) {
            return (BMMH(sections[i].flags) & VM_PROT_WRITE) ? 0 : 1;
        }
    }
    return Image->isReadOnly(uEntry);
}

// constant.. hmm, seems __cstring is writable, what's with that?
bool MachOBinaryFile::isStringConstant(ADDRESS uEntry) {
    uint32_t entry_val(uEntry.m_value);
    for (size_t i = 0; i < sections.size(); i++) {
        if (entry_val >= BMMH(sections[i].addr) && entry_val < BMMH(sections[i].addr) + BMMH(sections[i].size)) {
            // printf("%08x is in %s\n", uEntry, sections[i].sectname);
            if (!strcmp(sections[i].sectname, "__cstring"))
                return true;
        }
    }
    return false; // BinaryFile::isStringConstant(uEntry)
}

bool MachOBinaryFile::isCFStringConstant(ADDRESS uEntry) {
    uint32_t entry_val(uEntry.m_value);
    for (size_t i = 0; i < sections.size(); i++) {
        if (entry_val >= BMMH(sections[i].addr) && entry_val < BMMH(sections[i].addr) + BMMH(sections[i].size)) {
            // printf("%08x is in %s\n", uEntry, sections[i].sectname);
            if (!strcmp(sections[i].sectname, "__cfstring"))
                return true;
        }
    }
    return false; // BinaryFile::isCFStringConstant(uEntry)
}

const QString &MachOBinaryFile::GetDynamicProcName(ADDRESS uNative) { return dlprocs[uNative]; }

LOAD_FMT MachOBinaryFile::GetFormat() const { return LOADFMT_MACHO; }

MACHINE MachOBinaryFile::getMachine() const { return machine; }

bool MachOBinaryFile::isLibrary() const { return false; }

ADDRESS MachOBinaryFile::getImageBase() { return loaded_addr; }

size_t MachOBinaryFile::getImageSize() { return loaded_size; }

QStringList MachOBinaryFile::getDependencyList() { return QStringList(); /* FIXME */ }

DWord MachOBinaryFile::getDelta() {
    // Stupid function anyway: delta depends on section
    // This should work for the header only
    //    return (DWord)base - LMMH(m_pPEHeader->Imagebase);
    return (ADDRESS::host_ptr(base) - loaded_addr).m_value;
}
