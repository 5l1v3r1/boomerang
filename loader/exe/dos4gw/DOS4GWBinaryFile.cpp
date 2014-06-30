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

/**
 * \file DOS4GWBinaryFile.cpp
 * Desc: This file contains the implementation of the class DOS4GWBinaryFile.
 */

/* DOS4GW binary file format.
 *    This file implements the class DOS4GWBinaryFile, derived from class
 *    BinaryFile. See DOS4GWBinaryFile.h and BinaryFile.h for details.
 * 24 Jan 05 - Trent: created.
 */

#include "DOS4GWBinaryFile.h"
#include "BinaryFile.h"
#include "IBoomerang.h"
#include "IBinaryImage.h"
#include "IBinarySymbols.h"

#include "config.h"

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
extern "C" {
int microX86Dis(void *p); // From microX86dis.c
}

DOS4GWBinaryFile::DOS4GWBinaryFile()
{
}

DOS4GWBinaryFile::~DOS4GWBinaryFile() {
}
void DOS4GWBinaryFile::initialize(IBoomerang *sys) {
    Image = sys->getImage();
    Symbols = sys->getSymbols();
}

void DOS4GWBinaryFile::Close() { UnLoad(); }

ADDRESS DOS4GWBinaryFile::GetEntryPoint() {
    return ADDRESS::g((LMMH(m_pLXObjects[LMMH(m_pLXHeader->eipobjectnum)].RelocBaseAddr) + LMMH(m_pLXHeader->eip)));
}

ADDRESS DOS4GWBinaryFile::GetMainEntryPoint() {
    const IBinarySymbol *sym = Symbols->find("main");
    if (sym)
        return sym->getLocation();
    sym = Symbols->find("__CMain");
    if (sym)
        return sym->getLocation();

    // Search with this crude pattern: call, sub ebp, ebp, call __Cmain in the first 0x300 bytes
    // Start at program entry point
    unsigned p = LMMH(m_pLXHeader->eip);
    unsigned lim = p + 0x300;
    unsigned char op1, op2;
    ADDRESS addr;
    // unsigned lastOrdCall = 0; //TODO: identify the point of setting this variable
    bool gotSubEbp = false;   // True if see sub ebp, ebp
    bool lastWasCall = false; // True if the last instruction was a call

    IBinarySection *si = Image->GetSectionInfoByName("seg0"); // Assume the first section is text
    if (si == nullptr)
        si = Image->GetSectionInfoByName(".text");
    if (si == nullptr)
        si = Image->GetSectionInfoByName("CODE");
    assert(si);
    ADDRESS nativeOrigin = si->sourceAddr();
    unsigned textSize = si->size();
    if (textSize < 0x300)
        lim = p + textSize;

    while (p < lim) {
        op1 = *(unsigned char *)(p + base);
        op2 = *(unsigned char *)(p + base + 1);
        // std::cerr << std::hex << "At " << p << ", ops " << (unsigned)op1 << ", " << (unsigned)op2 << std::dec <<
        // "\n";
        switch (op1) {
        case 0xE8: {
            // An ordinary call
            if (gotSubEbp) {
                // This is the call we want. Get the offset from the call instruction
                addr = nativeOrigin + p + 5 + LMMH(*(p + base + 1));
                // std::cerr << "__CMain at " << std::hex << addr << "\n";
                return addr;
            }
            // lastOrdCall = p;
            lastWasCall = true;
            break;
        }
        case 0x2B: // 0x2B 0xED is sub ebp,ebp
            if (op2 == 0xED && lastWasCall)
                gotSubEbp = true;
            lastWasCall = false;
            break;
        default:
            gotSubEbp = false;
            lastWasCall = false;
            break;
        case 0xEB:           // Short relative jump
            if (op2 >= 0x80) // Branch backwards?
                break;       // Yes, just ignore it
            // Otherwise, actually follow the branch. May have to modify this some time...
            p += op2 + 2; // +2 for the instruction itself, and op2 for the displacement
            continue;     // Don't break, we have the new "pc" set already
        }
        int size = microX86Dis(p + base);
        if (size == 0x40) {
            fprintf(stderr, "Warning! Microdisassembler out of step at offset 0x%x\n", p);
            size = 1;
        }
        p += size;
    }
    return NO_ADDRESS;
}

bool DOS4GWBinaryFile::RealLoad(const QString &sName) {
    m_pFileName = sName;
    FILE *fp = fopen(qPrintable(sName), "rb");

    DWord lxoffLE, lxoff;
    fseek(fp, 0x3c, SEEK_SET);
    fread(&lxoffLE, 4, 1, fp); // Note: peoffLE will be in Little Endian
    lxoff = LMMH(lxoffLE);

    fseek(fp, lxoff, SEEK_SET);
    m_pLXHeader = new LXHeader;

    fread(m_pLXHeader, sizeof(LXHeader), 1, fp);

    if (m_pLXHeader->sigLo != 'L' || (m_pLXHeader->sigHi != 'X' && m_pLXHeader->sigHi != 'E')) {
        fprintf(stderr, "error loading file %s, bad LE/LX magic\n", qPrintable(sName));
        return false;
    }

    fseek(fp, lxoff + LMMH(m_pLXHeader->objtbloffset), SEEK_SET);
    m_pLXObjects = new LXObject[LMMH(m_pLXHeader->numobjsinmodule)];
    fread(m_pLXObjects, sizeof(LXObject), LMMH(m_pLXHeader->numobjsinmodule), fp);

// at this point we're supposed to read in the page table and fuss around with it
// but I'm just going to assume the file is flat.
#if 0
    unsigned npagetblentries = 0;
    m_cbImage = 0;
    for (unsigned n = 0; n < LMMH(m_pLXHeader->numobjsinmodule); n++) {
        if (LMMH(m_pLXObjects[n].PageTblIdx) + LMMH(m_pLXObjects[n].NumPageTblEntries) - 1 > npagetblentries)
            npagetblentries = LMMH(m_pLXObjects[n].PageTblIdx) + LMMH(m_pLXObjects[n].NumPageTblEntries) - 1;
        if (LMMH(m_pLXObjects[n].ObjectFlags) & 0x40)
            if (LMMH(m_pLXObjects[n].RelocBaseAddr) + LMMH(m_pLXObjects[n].VirtualSize) > m_cbImage)
                m_cbImage = LMMH(m_pLXObjects[n].RelocBaseAddr) + LMMH(m_pLXObjects[n].VirtualSize);
    }
    m_cbImage -= LMMH(m_pLXObjects[0].RelocBaseAddr);

    fseek(fp, lxoff + LMMH(m_pLXHeader->objpagetbloffset), SEEK_SET);
    m_pLXPages = new LXPage[npagetblentries];
    fread(m_pLXPages, sizeof(LXPage), npagetblentries, fp);
#endif

    unsigned npages = 0;
    m_cbImage = 0;
    for (unsigned n = 0; n < LMMH(m_pLXHeader->numobjsinmodule); n++)
        if (LMMH(m_pLXObjects[n].ObjectFlags) & 0x40) {
            if (LMMH(m_pLXObjects[n].PageTblIdx) + LMMH(m_pLXObjects[n].NumPageTblEntries) - 1 > npages)
                npages = LMMH(m_pLXObjects[n].PageTblIdx) + LMMH(m_pLXObjects[n].NumPageTblEntries) - 1;
            m_cbImage = LMMH(m_pLXObjects[n].RelocBaseAddr) + LMMH(m_pLXObjects[n].VirtualSize);
        }

    m_cbImage -= LMMH(m_pLXObjects[0].RelocBaseAddr);

    base = (char *)malloc(m_cbImage);

    uint32_t numSections = LMMH(m_pLXHeader->numobjsinmodule);
    std::vector<SectionParam> params;
    for (unsigned n = 0; n < numSections; n++) {
        if (LMMH(m_pLXObjects[n].ObjectFlags) & 0x40) {

            printf("vsize %x reloc %x flags %x page %i npage %i\n", LMMH(m_pLXObjects[n].VirtualSize),
                   LMMH(m_pLXObjects[n].RelocBaseAddr), LMMH(m_pLXObjects[n].ObjectFlags),
                   LMMH(m_pLXObjects[n].PageTblIdx), LMMH(m_pLXObjects[n].NumPageTblEntries));
            SectionParam sect;
            sect.Name = QString("seg%i").arg(n); // no section names in LX
            sect.from = LMMH(m_pLXObjects[n].RelocBaseAddr);
            sect.ImageAddress = ADDRESS::host_ptr(base + (sect.from - params.front().from).m_value);
            sect.Size = LMMH(m_pLXObjects[n].VirtualSize);
            DWord Flags = LMMH(m_pLXObjects[n].ObjectFlags);
            sect.Bss = 0; // TODO
            sect.Code = Flags & 0x4 ? 1 : 0;
            sect.Data = Flags & 0x4 ? 0 : 1;
            sect.ReadOnly = Flags & 0x1 ? 0 : 1;
            fseek(fp,
                  m_pLXHeader->datapagesoffset + (LMMH(m_pLXObjects[n].PageTblIdx) - 1) * LMMH(m_pLXHeader->pagesize),
                  SEEK_SET);
            char *p = base + LMMH(m_pLXObjects[n].RelocBaseAddr) - LMMH(m_pLXObjects[0].RelocBaseAddr);
            fread(p, LMMH(m_pLXObjects[n].NumPageTblEntries), LMMH(m_pLXHeader->pagesize), fp);
        }
    }
    for(SectionParam par : params) {
        IBinarySection *sect = Image->createSection(par.Name,par.from,par.from+par.Size);
        if(sect) {
            sect->setBss(par.Bss)
                    .setCode(par.Code)
                    .setData(par.Data)
                    .setReadOnly(par.ReadOnly)
                    .setHostAddr(par.ImageAddress);
        }
    }
    // TODO: decode entry tables

    // fixups
    fseek(fp, LMMH(m_pLXHeader->fixuppagetbloffset) + lxoff, SEEK_SET);
    unsigned int *fixuppagetbl = new unsigned int[npages + 1];
    fread(fixuppagetbl, sizeof(unsigned int), npages + 1, fp);

    // for (unsigned n = 0; n < npages; n++)
    //    printf("offset for page %i: %x\n", n + 1, fixuppagetbl[n]);
    // printf("offset to end of fixup rec: %x\n", fixuppagetbl[npages]);

    fseek(fp, LMMH(m_pLXHeader->fixuprecordtbloffset) + lxoff, SEEK_SET);
    LXFixup fixup;
    unsigned srcpage = 0;
    do {
        fread(&fixup, sizeof(fixup), 1, fp);
        if (fixup.src != 7 || (fixup.flags & ~0x50)) {
            fprintf(stderr, "unknown fixup type %02x %02x\n", fixup.src, fixup.flags);
            return false;
        }
        // printf("srcpage = %i srcoff = %x object = %02x trgoff = %x\n", srcpage + 1, fixup.srcoff, fixup.object,
        // fixup.trgoff);
        unsigned long src = srcpage * LMMH(m_pLXHeader->pagesize) + (short)LMMHw(fixup.srcoff);
        unsigned short object = 0;
        if (fixup.flags & 0x40)
            fread(&object, 2, 1, fp);
        else
            fread(&object, 1, 1, fp);
        unsigned int trgoff = 0;
        if (fixup.flags & 0x10)
            fread(&trgoff, 4, 1, fp);
        else
            fread(&trgoff, 2, 1, fp);
        unsigned long target = LMMH(m_pLXObjects[object - 1].RelocBaseAddr) + LMMHw(trgoff);
        //        printf("relocate dword at %x to point to %x\n", src, target);
        *(unsigned int *)(base + src) = target;

        while (ftell(fp) - (LMMH(m_pLXHeader->fixuprecordtbloffset) + lxoff) >= LMMH(fixuppagetbl[srcpage + 1]))
            srcpage++;
    } while (srcpage < npages);

    fclose(fp);
    return true;
}

// Clean up and unload the binary image
void DOS4GWBinaryFile::UnLoad() {}

bool DOS4GWBinaryFile::PostLoad(void *handle) {
    Q_UNUSED(handle);
    return false;
}

bool DOS4GWBinaryFile::DisplayDetails(const char *fileName, FILE *f
                                      /* = stdout */) {
    Q_UNUSED(fileName);
    Q_UNUSED(f);
    return false;
}

int DOS4GWBinaryFile::dos4gwRead2(short *ps) const {
    unsigned char *p = (unsigned char *)ps;
    // Little endian
    int n = (int)(p[0] + (p[1] << 8));
    return n;
}

int DOS4GWBinaryFile::dos4gwRead4(int *pi) const {
    short *p = (short *)pi;
    int n1 = dos4gwRead2(p);
    int n2 = dos4gwRead2(p + 1);
    int n = (int)(n1 | (n2 << 16));
    return n;
}

LOAD_FMT DOS4GWBinaryFile::GetFormat() const { return LOADFMT_LX; }

MACHINE DOS4GWBinaryFile::getMachine() const { return MACHINE_PENTIUM; }

ADDRESS DOS4GWBinaryFile::getImageBase() { return ADDRESS::g(m_pLXObjects[0].RelocBaseAddr); }

size_t DOS4GWBinaryFile::getImageSize() {
    return 0; // TODO
}

QStringList DOS4GWBinaryFile::getDependencyList() { return QStringList(); /* FIXME */ }

DWord DOS4GWBinaryFile::getDelta() {
    // Stupid function anyway: delta depends on section
    // This should work for the header only
    //    return (DWord)base - LMMH(m_pPEHeader->Imagebase);
    return intptr_t(base) - m_pLXObjects[0].RelocBaseAddr;
}
