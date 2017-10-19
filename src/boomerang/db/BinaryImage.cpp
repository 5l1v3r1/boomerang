#pragma region License
/*
 * This file is part of the Boomerang Decompiler.
 *
 * See the file "LICENSE.TERMS" for information on usage and
 * redistribution of this file, and for a DISCLAIMER OF ALL
 * WARRANTIES.
 */
#pragma endregion License
#include "BinaryImage.h"


#include "boomerang/util/Types.h"
#include "boomerang/util/Log.h"

#include <algorithm>


BinaryImage::BinaryImage()
{
}


BinaryImage::~BinaryImage()
{
}


void BinaryImage::reset()
{
    m_sectionMap.clear();

    for (IBinarySection *section : m_sections) {
        delete section;
    }

    m_sections.clear();
}


Byte BinaryImage::readNative1(Address addr)
{
    const IBinarySection *si = getSectionByAddr(addr);

    if (si == nullptr) {
        LOG_WARN("Target Memory access in unmapped section at address %1", addr.toString());
        return 0xFF;
    }

    HostAddress host = si->getHostAddr() - si->getSourceAddr() + addr;
    return *(Byte *)host.value();
}


SWord BinaryImage::readNative2(Address nat)
{
    const IBinarySection *si = getSectionByAddr(nat);

    if (si == nullptr) {
        return 0;
    }

    HostAddress host = si->getHostAddr() - si->getSourceAddr() + nat;
    return Util::readWord((const void *)host.value(), si->getEndian());
}


DWord BinaryImage::readNative4(Address addr)
{
    const IBinarySection *si = getSectionByAddr(addr);

    if (si == nullptr) {
        return 0;
    }

    HostAddress host = si->getHostAddr() - si->getSourceAddr() + addr;
    return Util::readDWord((const void *)host.value(), si->getEndian());
}


QWord BinaryImage::readNative8(Address addr)
{
    const IBinarySection *si = getSectionByAddr(addr);

    if (si == nullptr) {
        return 0;
    }

    HostAddress host = si->getHostAddr() - si->getSourceAddr() + addr;
    return Util::readQWord((const void *)host.value(), si->getEndian());
}


float BinaryImage::readNativeFloat4(Address nat)
{
    DWord raw = readNative4(nat);

    return *(float *)&raw; // Note: cast, not convert
}


double BinaryImage::readNativeFloat8(Address nat)
{
    const IBinarySection *si = getSectionByAddr(nat);

    if (si == nullptr) {
        return 0;
    }

    QWord raw = readNative8(nat);
    return *(double *)&raw;
}


void BinaryImage::writeNative4(Address addr, uint32_t value)
{
    const IBinarySection *si = getSectionByAddr(addr);

    if (si == nullptr) {
        LOG_WARN("Ignoring write at address %1: Address is outside any known section");
        return;
    }

    HostAddress host = si->getHostAddr() - si->getSourceAddr() + addr;

    Util::writeDWord((void *)host.value(), value, si->getEndian());
}


void BinaryImage::updateTextLimits()
{
    m_limitTextLow  = Address::INVALID;
    m_limitTextHigh = Address::ZERO;
    m_textDelta     = 0;

    for (IBinarySection *pSect : m_sections) {
        if (!pSect->isCode()) {
            continue;
        }

        // The .plt section is an anomaly. It's code, but we never want to
        // decode it, and in Sparc ELF files, it's actually in the data
        // section (so it can be modified). For now, we make this ugly
        // exception
        if (".plt" == pSect->getName()) {
            continue;
        }

        if (pSect->getSourceAddr() < m_limitTextLow) {
            m_limitTextLow = pSect->getSourceAddr();
        }

        Address hiAddress = pSect->getSourceAddr() + pSect->getSize();

        if (hiAddress > m_limitTextHigh) {
            m_limitTextHigh = hiAddress;
        }

        ptrdiff_t host_native_diff = (pSect->getHostAddr() - pSect->getSourceAddr()).value();

        if (m_textDelta == 0) {
            m_textDelta = host_native_diff;
        }
        else if (m_textDelta != host_native_diff) {
            fprintf(stderr, "warning: textDelta different for section %s (ignoring).\n", qPrintable(pSect->getName()));
        }
    }
}


const IBinarySection *BinaryImage::getSectionByAddr(Address uEntry) const
{
    auto iter = m_sectionMap.find(uEntry);

    return (iter != m_sectionMap.end()) ? iter->second : nullptr;
}


int BinaryImage::getSectionIndex(const QString& sName)
{
    for (size_t i = 0; i < m_sections.size(); i++) {
        if (m_sections[i]->getName() == sName) {
            return i;
        }
    }

    return -1;
}


IBinarySection *BinaryImage::getSectionByName(const QString& sName)
{
    int i = getSectionIndex(sName);

    if (i == -1) {
        return nullptr;
    }

    return m_sections[i];
}


bool BinaryImage::isReadOnly(Address addr)
{
    const SectionInfo *p = static_cast<const SectionInfo *>(getSectionByAddr(addr));

    if (!p) {
        return false;
    }

    if (p->isReadOnly()) {
        return true;
    }

    QVariant v = p->attributeInRange("ReadOnly", addr, addr + 1);
    return !v.isNull();
}


Address BinaryImage::getLimitTextLow() const
{
    return m_sectionMap.begin()->first.lower();
}


Address BinaryImage::getLimitTextHigh() const
{
    return m_sectionMap.rbegin()->first.upper();
}


IBinarySection *BinaryImage::createSection(const QString& name, Address from, Address to)
{
    assert(from <= to);

    if (from == to) {
        to += 1; // open interval, so -> [from,to+1) is right
    }

#if DEBUG
    // see https://stackoverflow.com/questions/25501044/gcc-ld-overlapping-sections-tbss-init-array-in-statically-linked-elf-bin
    // Basically, the .tbss section is of type SHT_NOBITS, so there is no data associated to the section.
    // It can therefore overlap other sections containing data.
    // This is a quirk of ELF programs linked statically with glibc
    if (name != ".tbss") {
        SectionRangeMap::iterator itFrom, itTo;
        std::tie(itFrom, itTo) = m_sectionMap.equalRange(from, to);

        for (SectionRangeMap::iterator clash_with = itFrom; clash_with != itTo; clash_with++) {
            if ((*clash_with->second).getName() != ".tbss") {
                LOG_WARN("Segment %1 would intersect existing segment %2", name, (*clash_with->second).getName());
                return nullptr;
            }
        }
    }
#endif

    SectionInfo *sect = new SectionInfo(from, (to - from).value(), name);
    m_sections.push_back(sect);

    m_sectionMap.insert(from, to, sect);
    return sect;
}
