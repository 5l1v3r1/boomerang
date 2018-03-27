#pragma region License
/*
 * This file is part of the Boomerang Decompiler.
 *
 * See the file "LICENSE.TERMS" for information on usage and
 * redistribution of this file, and for a DISCLAIMER OF ALL
 * WARRANTIES.
 */
#pragma endregion License
#include "PalmBinaryLoader.h"


#include "palmsystraps.h"

#include "boomerang/db/binary/BinaryImage.h"
#include "boomerang/db/binary/BinarySection.h"
#include "boomerang/db/binary/BinarySymbolTable.h"
#include "boomerang/util/Log.h"

#include <cassert>
#include <cstring>
#include <cstdlib>


// Macro to convert a pointer to a Big Endian integer into a host integer
#define UINT4(p)        Util::readDWord(p, true)
#define UINT4ADDR(p)    Util::readDWord(reinterpret_cast<const void *>((p).value()), true)

enum PRCAttr : SWord
{
    PRCAttr_PRC               =    0x1,
    PRCAttr_ReadOnly          =    0x2,
    PRCAttr_AppInfoDirty      =    0x4,
    PRCAttr_NeedsBackup       =    0x8,
    PRCAttr_ReplaceOK         =   0x10,
    PRCAttr_ResetAfterInstall =   0x20,
    PRCAttr_NoCopy            =   0x40,
    PRCAttr_FileStreamDB      =   0x80,
    PRCAttr_Hidden            =  0x100,
    PRCAttr_Launchable        =  0x200,
    PRCAttr_Open              = 0x8000
};

#pragma pack(push, 1)

struct PRCHeader
{
    char name[0x20];  ///< name of app, in MacRoman encoding, padded with 0
    SWord attributes; ///< PRCAttr
    SWord version;            ///< Usually 1
    DWord creationDate;       ///< seconds since midnight, January 1, 1904
    DWord modificationDate;   ///< seconds since midnight, January 1, 1904
    DWord backupDate;         ///< seconds since midnight, January 1, 1904
    DWord modificationNumber; ///< usually 0
    DWord appInfoOffset;      ///< offset from start of file to start of appInfo field; usually 0
    DWord sortInfoOffset;     ///< offset from start of file to start of sortInfo field; usually 0
    char type[4];             ///< for a PRC file, the four-character constant "appl" or "panl"
    DWord creator;            ///< a four-character constant unique to this application
    DWord uniqueIDSeed;       ///< usually 0
};
static_assert(sizeof(PRCHeader) == 72, "PRCHeader size does not match");


struct PRCRecordList
{
    DWord nextRecordListOffset; ///< offset from this record list to the next record list; usually zero, indicating no further record lists
    SWord resourceCount;        ///< the number of resources
};
static_assert(sizeof(PRCRecordList) == 6, "PRCRecordList size does not match");


struct PRCResource
{
    DWord type; ///< the resource type, a four-character constant
    SWord id;   ///< the resource id
    DWord dataOffset; ///< offset from start of file to start of resource data; end of resource data is indicated by next record or end of file
};
static_assert(sizeof(PRCResource) == 10, "PRCRecordList size does not match");


#pragma pack(pop)



PalmBinaryLoader::PalmBinaryLoader()
    : m_image(nullptr)
    , m_data(nullptr)
{
}


PalmBinaryLoader::~PalmBinaryLoader()
{
    m_image = nullptr;
    delete[] m_data;
}


void PalmBinaryLoader::initialize(BinaryImage *image, BinarySymbolTable *symbols)
{
    m_binaryImage = image;
    m_symbols     = symbols;
}


struct SectionProperties
{
    QString     name;
    Address     from, to;
    HostAddress hostAddr;
};


bool PalmBinaryLoader::loadFromMemory(QByteArray& img)
{
    const int size = img.size();
    m_image = reinterpret_cast<uint8_t *>(img.data());

    if (static_cast<unsigned long>(size) < sizeof(PRCHeader) + sizeof(PRCRecordList)) {
        LOG_ERROR("This is not a standard .prc file");
        return false;
    }

    PRCHeader *prcHeader = reinterpret_cast<PRCHeader *>(img.data());

    // Check type at offset 0x3C; should be "appl" (or "palm"; ugh!)
    if ((strncmp(prcHeader->type, "appl", 4) != 0) &&
        (strncmp(prcHeader->type, "panl", 4) != 0) &&
        (strncmp(prcHeader->type, "libr", 4) != 0)) {
            LOG_ERROR("This is not a standard .prc file");
            return false;
    }

    addTrapSymbols();

    // Get the number of resource headers (one section per resource)
    PRCRecordList *records = reinterpret_cast<PRCRecordList *>(m_image + sizeof(PRCHeader));
    if (records->nextRecordListOffset != 0) {
        LOG_ERROR("Reading PRC files with multiple record lists is not supported!");
        return false;
    }

    const SWord numSections = Util::readWord(&records->resourceCount, true);

    // Iterate through the resource headers (generating section info structs)
    PRCResource *resource = reinterpret_cast<PRCResource *>(m_image + sizeof(PRCHeader) + sizeof(PRCRecordList));

    std::vector<SectionProperties> sectionProperties;

    for (unsigned i = 0; i < numSections; i++) {
        char buf[5];
        strncpy(buf, reinterpret_cast<char *>(&resource[i].type), 4);
        buf[4] = 0;

        SWord id = Util::readWord(&resource[i].id, true);
        QString name = QString("%1%2").arg(buf).arg(id);
        DWord dataOffset = Util::readDWord(&resource[i].dataOffset, true);

        Address startAddr(dataOffset);

        if (i > 0) {
            sectionProperties[i-1].to = startAddr;
        }

        sectionProperties.push_back({ name, startAddr, Address::INVALID, HostAddress(m_image + dataOffset) });
    }

    // last section extends until eof
    sectionProperties[numSections-1].to = Address(size);

    for (SectionProperties props : sectionProperties) {
        assert(props.to != Address::INVALID);
        BinarySection *sect = m_binaryImage->createSection(props.name, props.from, props.to);

        if (sect) {
            // Decide if code or data; note that code0 is a special case (not code)
            sect->setHostAddr(props.hostAddr);
            sect->setCode((props.name != "code0") && (props.name.startsWith("code")));
            sect->setData(props.name.startsWith("data"));
            sect->setEndian(0);                          // little endian
            sect->setEntrySize(1);                       // No info available
            sect->addDefinedArea(props.from, props.to); // no BSS
        }
    }

    // Create a separate, uncompressed, initialised data section
    BinarySection *dataSection = m_binaryImage->getSectionByName("data0");

    if (dataSection == nullptr) {
        LOG_ERROR("No data section found!");
        return false;
    }

    const BinarySection *code0Section = m_binaryImage->getSectionByName("code0");

    if (code0Section == nullptr) {
        LOG_ERROR("No code 0 section found!");
        return false;
    }

    // When the info is all boiled down, the two things we need from the
    // code 0 section are at offset 0, the size of data above a5, and at
    // offset 4, the size below. Save the size below as a member variable
    m_sizeBelowA5 = UINT4ADDR(code0Section->getHostAddr() + 4);

    // Total size is this plus the amount above (>=) a5
    unsigned sizeData = m_sizeBelowA5 + UINT4ADDR(code0Section->getHostAddr());

    // Allocate a new data section
    m_data = new unsigned char[sizeData];

    if (m_data == nullptr) {
        LOG_FATAL("Could not allocate %1 bytes for data section", sizeData);
    }

    // Uncompress the data. Skip first long (offset of CODE1 "xrefs")
    Byte *p = reinterpret_cast<Byte *>((dataSection->getHostAddr() + 4).value());
    int start = static_cast<int>(UINT4(p));
    p += 4;
    unsigned char *q   = (m_data + m_sizeBelowA5 + start);
    bool          done = false;

    while (!done && (p < reinterpret_cast<unsigned char *>((dataSection->getHostAddr() + dataSection->getSize()).value()))) {
        unsigned char rle = *p++;

        if (rle == 0) {
            done = true;
            break;
        }
        else if (rle == 1) {
            // 0x01 b_0 b_1
            // => 0x00 0x00 0x00 0x00 0xFF 0xFF b_0 b_1
            *q++ = 0x00;
            *q++ = 0x00;
            *q++ = 0x00;
            *q++ = 0x00;
            *q++ = 0xFF;
            *q++ = 0xFF;
            *q++ = *p++;
            *q++ = *p++;
        }
        else if (rle == 2) {
            // 0x02 b_0 b_1 b_2
            // => 0x00 0x00 0x00 0x00 0xFF b_0 b_1 b_2
            *q++ = 0x00;
            *q++ = 0x00;
            *q++ = 0x00;
            *q++ = 0x00;
            *q++ = 0xFF;
            *q++ = *p++;
            *q++ = *p++;
            *q++ = *p++;
        }
        else if (rle == 3) {
            // 0x03 b_0 b_1 b_2
            // => 0xA9 0xF0 0x00 0x00 b_0 b_1 0x00 b_2
            *q++ = 0xA9;
            *q++ = 0xF0;
            *q++ = 0x00;
            *q++ = 0x00;
            *q++ = *p++;
            *q++ = *p++;
            *q++ = 0x00;
            *q++ = *p++;
        }
        else if (rle == 4) {
            // 0x04 b_0 b_1 b_2 b_3
            // => 0xA9 axF0 0x00 b_0 b_1 b_3 0x00 b_3
            *q++ = 0xA9;
            *q++ = 0xF0;
            *q++ = 0x00;
            *q++ = *p++;
            *q++ = *p++;
            *q++ = *p++;
            *q++ = 0x00;
            *q++ = *p++;
        }
        else if (rle < 0x10) {
            // 5-0xF are invalid.
            assert(false);
        }
        else if (rle >= 0x80) {
            // n+1 bytes of literal data
            for (int k = 0; k <= (rle - 0x80); k++) {
                *q++ = *p++;
            }
        }
        else if (rle >= 40) {
            // n+1 repetitions of 0
            for (int k = 0; k <= (rle - 0x40); k++) {
                *q++ = 0x00;
            }
        }
        else if (rle >= 20) {
            // n+2 repetitions of b
            unsigned char b = *p++;

            for (int k = 0; k < (rle - 0x20 + 2); k++) {
                *q++ = b;
            }
        }
        else {
            // 0x10: n+1 repetitions of 0xFF
            for (int k = 0; k <= (rle - 0x10); k++) {
                *q++ = 0xFF;
            }
        }
    }

    if (!done) {
        LOG_WARN("Compressed data section premature end");
    }

    LOG_VERBOSE("Used %1 bytes of %2 in decompressing data section",
                p - reinterpret_cast<unsigned char *>(dataSection->getHostAddr().value()), dataSection->getSize());

    // Replace the data pointer and size with the uncompressed versions

    dataSection->setHostAddr(HostAddress(m_data));
    dataSection->resize(sizeData);

    m_symbols->createSymbol(getMainEntryPoint(), "PilotMain")->setAttribute("EntryPoint", true);
    return true;
}


#define TESTMAGIC4(buf, off, a, b, c, d)    (buf[off] == a && buf[off + 1] == b && buf[off + 2] == c && buf[off + 3] == d)
int PalmBinaryLoader::canLoad(QIODevice& dev) const
{
    unsigned char buf[64];

    dev.read(reinterpret_cast<char *>(buf), sizeof(buf));

    if (TESTMAGIC4(buf, 0x3C, 'a', 'p', 'p', 'l') || TESTMAGIC4(buf, 0x3C, 'p', 'a', 'n', 'l')) {
        /* PRC Palm-pilot binary */
        return 8;
    }

    return 0;
}


void PalmBinaryLoader::unload()
{
}


Address PalmBinaryLoader::getEntryPoint()
{
    assert(0); /* FIXME: Need to be implemented */
    return Address::INVALID;
}


void PalmBinaryLoader::close()
{
    // Not implemented yet
}


LoadFmt PalmBinaryLoader::getFormat() const
{
    return LoadFmt::PALM;
}


Machine PalmBinaryLoader::getMachine() const
{
    return Machine::PALM;
}


bool PalmBinaryLoader::isLibrary() const
{
    return(strncmp(reinterpret_cast<char *>(m_image + 0x3C), "libr", 4) == 0);
}


void PalmBinaryLoader::addTrapSymbols()
{
    for (uint32_t loc = 0xAAAAA000; loc <= 0xAAAAAFFF; ++loc) {
        // This is the convention used to indicate an A-line system call
        unsigned offset = loc & 0xFFF;

        if (offset < numTrapStrings) {
            m_symbols->createSymbol(Address(loc), trapNames[offset]);
        }
    }
}


// Specific to BinaryFile objects that implement a "global pointer"
// Gets a pair of unsigned integers representing the address of %agp,
// and the value for GLOBALOFFSET. For Palm, the latter is the amount of
// space allocated below %a5, i.e. the difference between %a5 and %agp
// (%agp points to the bottom of the global data area).
std::pair<Address, unsigned> PalmBinaryLoader::getGlobalPointerInfo()
{
    Address              agp = Address::ZERO;
    const BinarySection *ps = m_binaryImage->getSectionByName("data0");

    if (ps) {
        agp = ps->getSourceAddr();
    }

    std::pair<Address, unsigned> ret(agp, m_sizeBelowA5);
    return ret;
}


int PalmBinaryLoader::getAppID() const
{
    // The answer is in the header. Return 0 if file not loaded
    if (m_image == nullptr) {
        return 0;
    }

    const PRCHeader *prcHeader = reinterpret_cast<PRCHeader *>(m_image);
    return Util::readDWord(&prcHeader->creator, true);
}


#define WILD    0x4AFC

// Patterns for Code Warrior
static SWord CWFirstJump[] =
{
    0x0000, 0x0001,         // ? All Pilot programs seem to start with this
    0x487a, 0x0004,         // pea 4(pc)
    0x0697, WILD, WILD,     // addil #number, (a7)
    0x4e75                  // rts
};

static SWord CWCallMain[] =
{
    0x487a, 0x000e,          // pea 14(pc)
    0x487a, 0x0004,          // pea 4(pc)
    0x0697, WILD, WILD,      // addil #number, (a7)
    0x4e75                   // rts
};


// patterns for GCC
static SWord GccCallMain[] =
{
    0x3F04,                                       // movew d4, -(a7)
    0x6100, WILD,                                 // bsr xxxx
    0x3F04,                                       // movew d4, -(a7)
    0x2F05,                                       // movel d5, -(a7)
    0x3F06,                                       // movew d6, -(a7)
    0x6100, WILD
};                                                // bsr PilotMain


/**
 * Find a byte pattern corresponding to \p patt;
 * \p pat may include a wildcard (16 bit WILD, 0x4AFC)
 *
 * \param start    pointer to code to start searching
 * \param size     max number of SWords to search
 * \param patt     pattern to look for
 * \param pattSize size of the pattern (in SWords)
 *
 * \returns pointer to start of match if found, or nullptr if not found.
 */
const SWord *findPattern(const SWord *start, int size, const SWord *patt, int pattSize)
{
    if (pattSize <= 0 || pattSize > size) {
        return nullptr; // no pattern to find
    }

    int startOffset = 0;

    while (startOffset + pattSize <= size) {
        bool allMatched = true;
        for (int i = 0; i < pattSize; i++) {
            if (patt[i] == WILD) {
                continue;
            }

            const SWord curr = Util::readWord(start + startOffset + i, true);
            if (patt[i] != curr) {
                // Mismatch, try next pattern
                allMatched = false;
                break;
            }
        }

        if (allMatched) {
            return start + startOffset;
        }

        startOffset++;
    }

    // Each start position failed
    return nullptr;
}


// Find the native address for the start of the main entry function.
// For Palm binaries, this is PilotMain.
Address PalmBinaryLoader::getMainEntryPoint()
{
    BinarySection *psect = m_binaryImage->getSectionByName("code1");

    if (psect == nullptr) {
        return Address::ZERO; // Failed
    }

    // Return the start of the code1 section
    SWord *startCode = reinterpret_cast<SWord *>(psect->getHostAddr().value());
    int      delta   = (psect->getHostAddr() - psect->getSourceAddr()).value();

    // First try the CW first jump pattern
    const SWord *res = findPattern(startCode, sizeof(CWFirstJump) / sizeof(SWord), CWFirstJump, sizeof(CWFirstJump) / sizeof(SWord));

    if (res) {
        // We have the code warrior first jump. Get the addil operand
        const int addilOp    = static_cast<int>(Util::readDWord((startCode + 5), true));
        SWord   *startupCode = reinterpret_cast<SWord *>((HostAddress(startCode) + 10 + addilOp).value());

        // Now check the next 60 SWords for the call to PilotMain
        res = findPattern(startupCode, 60, CWCallMain, sizeof(CWCallMain) / sizeof(SWord));

        if (res) {
            // Get the addil operand
            const int _addilOp = Util::readDWord((res + 5), true);

            // That operand plus the address of that operand is PilotMain
            Address offset_loc = Address(reinterpret_cast<const Byte *>(res) - reinterpret_cast<const Byte *>(startCode) + 5);
            return offset_loc + _addilOp; // ADDRESS::host_ptr(res) + 10 + addilOp - delta;
        }
        else {
            fprintf(stderr, "Could not find call to PilotMain in CW app\n");
            return Address::ZERO;
        }
    }

    // Check for gcc call to main
    res = findPattern(startCode, 75, GccCallMain, sizeof(GccCallMain) / sizeof(SWord));

    if (res) {
        // Get the operand to the bsr
        SWord bsrOp = res[7];
        return Address((HostAddress(res) - delta).value() + 14 + bsrOp);
    }

    fprintf(stderr, "Cannot find call to PilotMain\n");
    return Address::ZERO;
}


void PalmBinaryLoader::generateBinFiles(const QString& path) const
{
    for (const BinarySection *si : *m_binaryImage) {
        const BinarySection& psect(*si);

        if (psect.getName().startsWith("code") || psect.getName().startsWith("data")) {
            continue;
        }

        // Save this section in a file
        // First construct the file name
        int     sect_num = psect.getName().mid(4).toInt();
        QString name     = QString("%1%2.bin").arg(psect.getName().left(4)).arg(sect_num, 4, 16, QChar('0'));
        QString fullName(path);
        fullName += name;
        // Create the file
        FILE *f = fopen(qPrintable(fullName), "w");

        if (f == nullptr) {
            fprintf(stderr, "Could not open %s for writing binary file\n", qPrintable(fullName));
            return;
        }

        fwrite(psect.getHostAddr(), psect.getSize(), 1, f);
        fclose(f);
    }
}


BOOMERANG_LOADER_PLUGIN(PalmBinaryLoader,
                        "Palm OS binary file loader", BOOMERANG_VERSION, "Boomerang developers")
