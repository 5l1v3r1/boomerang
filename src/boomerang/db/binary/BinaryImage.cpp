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
    reset();
}


void BinaryImage::reset()
{
    m_sectionMap.clear();
    m_sections.clear();
}


Byte BinaryImage::readNative1(Address addr) const
{
    const BinarySection *si = getSectionByAddr(addr);

    if (si == nullptr || si->getHostAddr() == HostAddress::INVALID) {
        LOG_WARN("Invalid read at address %1: Address is not mapped to a section", addr);
        return 0xFF;
    }
    else if (addr + 1 > si->getSourceAddr() + si->getSize()) {
        LOG_WARN("Invalid read at address %1: Read extends past section boundary", addr);
        return 0xFF;
    }

    HostAddress host = si->getHostAddr() - si->getSourceAddr() + addr;
    return *reinterpret_cast<Byte *>(host.value());
}


SWord BinaryImage::readNative2(Address addr) const
{
    const BinarySection *si = getSectionByAddr(addr);

    if (si == nullptr || si->getHostAddr() == HostAddress::INVALID) {
        LOG_WARN("Invalid read at address %1: Address is not mapped to a section", addr.toString());
        return 0x0000;
    }
    else if (addr + 2 > si->getSourceAddr() + si->getSize()) {
        LOG_WARN("Invalid read at address %1: Read extends past section boundary", addr);
        return 0x0000;
    }

    HostAddress host = si->getHostAddr() - si->getSourceAddr() + addr;
    return Util::readWord(reinterpret_cast<const Byte *>(host.value()), si->getEndian());
}


DWord BinaryImage::readNative4(Address addr) const
{
    const BinarySection *si = getSectionByAddr(addr);

    if (si == nullptr || si->getHostAddr() == HostAddress::INVALID) {
        LOG_WARN("Invalid read at address %1: Address is not mapped to a section", addr.toString());
        return 0x00000000;
    }
    else if (addr + 4 > si->getSourceAddr() + si->getSize()) {
        LOG_WARN("Invalid read at address %1: Read extends past section boundary", addr);
        return 0x00000000;
    }

    HostAddress host = si->getHostAddr() - si->getSourceAddr() + addr;
    return Util::readDWord(reinterpret_cast<const Byte *>(host.value()), si->getEndian());
}


QWord BinaryImage::readNative8(Address addr) const
{
    const BinarySection *si = getSectionByAddr(addr);

    if (si == nullptr || si->getHostAddr() == HostAddress::INVALID) {
        LOG_WARN("Invalid read at address %1: Address is not mapped to a section", addr.toString());
        return 0x0000000000000000;
    }
    else if (addr + 8 > si->getSourceAddr() + si->getSize()) {
        LOG_WARN("Invalid read at address %1: Read extends past section boundary", addr);
        return 0x0000000000000000;
    }

    HostAddress host = si->getHostAddr() - si->getSourceAddr() + addr;
    return Util::readQWord(reinterpret_cast<const Byte *>(host.value()), si->getEndian());
}


float BinaryImage::readNativeFloat4(Address addr) const
{
    const BinarySection *si = getSectionByAddr(addr);

    if (si == nullptr || si->getHostAddr() == HostAddress::INVALID) {
        LOG_WARN("Invalid read at address %1: Address is not mapped to a section", addr.toString());
        return 0.0f;
    }
    else if (addr + 4 > si->getSourceAddr() + si->getSize()) {
        LOG_WARN("Invalid read at address %1: Read extends past section boundary", addr);
        return 0.0f;
    }

    DWord raw = readNative4(addr);

    return *reinterpret_cast<float *>(&raw); // Note: cast, not convert
}


double BinaryImage::readNativeFloat8(Address addr) const
{
    const BinarySection *si = getSectionByAddr(addr);

    if (si == nullptr || si->getHostAddr() == HostAddress::INVALID) {
        LOG_WARN("Invalid read at address %1: Address is not mapped to a section", addr.toString());
        return 0.0;
    }
    else if (addr + 8 > si->getSourceAddr() + si->getSize()) {
        LOG_WARN("Invalid read at address %1: Read extends past section boundary", addr);
        return 0.0;
    }

    QWord raw = readNative8(addr);
    return *reinterpret_cast<double *>(&raw);
}


bool BinaryImage::writeNative4(Address addr, uint32_t value)
{
    const BinarySection *si = getSectionByAddr(addr);

    if (si == nullptr || si->getHostAddr() == HostAddress::INVALID) {
        LOG_WARN("Ignoring write at address %1: Address is outside any writable section");
        return false;
    }
    else if (addr + 4 > si->getSourceAddr() + si->getSize()) {
        LOG_WARN("Invalid write at address %1: Write extends past section boundary", addr);
        return 0.0f;
    }

    HostAddress host = si->getHostAddr() - si->getSourceAddr() + addr;

    Util::writeDWord(reinterpret_cast<void *>(host.value()), value, si->getEndian());
    return true;
}


void BinaryImage::updateTextLimits()
{
    m_limitTextLow  = Address::INVALID;
    m_limitTextHigh = Address::INVALID;
    m_textDelta     = 0;

    for (BinarySection *section : m_sections) {
        if (!section->isCode()) {
            continue;
        }

        // The .plt section is an anomaly. It's code, but we never want to
        // decode it, and in Sparc ELF files, it's actually in the data
        // section (so it can be modified). For now, we make this ugly
        // exception
        if (".plt" == section->getName()) {
            continue;
        }

        if (m_limitTextLow == Address::INVALID || section->getSourceAddr() < m_limitTextLow) {
            m_limitTextLow = section->getSourceAddr();
        }

        const Address highAddress = section->getSourceAddr() + section->getSize();

        if (m_limitTextHigh == Address::INVALID || highAddress > m_limitTextHigh) {
            m_limitTextHigh = highAddress;
        }

        const ptrdiff_t hostNativeDiff = (section->getHostAddr() - section->getSourceAddr()).value();

        if (m_textDelta == 0) {
            m_textDelta = hostNativeDiff;
        }
        else if (m_textDelta != hostNativeDiff) {
            LOG_WARN("TextDelta different for section %1 (ignoring).", section->getName());
        }
    }
}


bool BinaryImage::isReadOnly(Address addr) const
{
    const BinarySection *section = getSectionByAddr(addr);

    if (!section) {
        return false;
    }

    if (section->isReadOnly()) {
        return true;
    }

    QVariant v = section->attributeInRange("ReadOnly", addr, addr + 1);
    return !v.isNull();
}


Address BinaryImage::getLimitTextLow() const
{
    return m_limitTextLow;
}


Address BinaryImage::getLimitTextHigh() const
{
    return m_limitTextHigh;
}


BinarySection *BinaryImage::createSection(const QString& name, Address from, Address to)
{
    if (from == Address::INVALID || to == Address::INVALID || to < from) {
        LOG_ERROR("Could not create section '%1' with invalid extent [%2, %3)", name, from, to);
        return nullptr;
    }
    else if (getSectionByName(name) != nullptr) {
        LOG_ERROR("Could not create section '%1': A section with the same name already exists", name);
        return nullptr;
    }

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

        for (SectionRangeMap::iterator clash_with = itFrom; clash_with != itTo; ++clash_with) {
            if ((*clash_with->second).getName() != ".tbss") {
                LOG_WARN("Segment %1 would intersect existing segment %2", name, (*clash_with->second).getName());
                return nullptr;
            }
        }
    }
#endif

    BinarySection *sect = new BinarySection(from, (to - from).value(), name);
    m_sections.push_back(sect);

    m_sectionMap.insert(from, to, std::unique_ptr<BinarySection>(sect));
    return sect;
}


BinarySection *BinaryImage::createSection(const QString& name, Interval<Address> extent)
{
    return createSection(name, extent.lower(), extent.upper());
}


BinarySection *BinaryImage::getSectionByIndex(int idx)
{
    return Util::inRange(idx, 0, getNumSections()) ? m_sections[idx] : nullptr;
}


const BinarySection *BinaryImage::getSectionByIndex(int idx) const
{
    return Util::inRange(idx, 0, getNumSections()) ? m_sections[idx] : nullptr;
}


BinarySection *BinaryImage::getSectionByName(const QString& sectionName)
{
    for (BinarySection *section : m_sections) {
        if (section->getName() == sectionName) {
            return section;
        }
    }

    return nullptr;
}


const BinarySection *BinaryImage::getSectionByName(const QString& sectionName) const
{
    for (const BinarySection *section : m_sections) {
        if (section->getName() == sectionName) {
            return section;
        }
    }

    return nullptr;
}


BinarySection *BinaryImage::getSectionByAddr(Address addr)
{
    auto iter = m_sectionMap.find(addr);
    return (iter != m_sectionMap.end()) ? iter->second.get() : nullptr;
}


const BinarySection *BinaryImage::getSectionByAddr(Address addr) const
{
    auto iter = m_sectionMap.find(addr);
    return (iter != m_sectionMap.end()) ? iter->second.get() : nullptr;
}


