/*
 * Copyright (C) 2000-2001, The University of Queensland
 *
 * See the file "LICENSE.TERMS" for information on usage and
 * redistribution of this file, and for a DISCLAIMER OF ALL
 * WARRANTIES.
 *
 */

/***************************************************************************/ /**
 * \file    HpSomBinaryLoader.cpp
 * \brief   This file contains the implementation of the class
 *          HpSomBinaryLoader, for decoding PA/RISC SOM executable files.
 *          Derived from class IFileLoader.
 ******************************************************************************/

#include "HpSomBinaryLoader.h"

#include "boomerang/include/IBoomerang.h"
#include "boomerang/db/IBinaryImage.h"
#include "boomerang/db/IBinarySymbols.h"
#include "boomerang/db/IBinarySection.h"

#include <QString>
#include <cstddef>
#include <cassert>
#include <cstring>
#include <QFile>
#include <QBuffer>

// Macro to convert a pointer to a Big Endian integer into a host integer
#define UC(p)           ((unsigned char *)p)
#define UINT4(p)        ((UC(p)[0] << 24) | (UC(p)[1] << 16) | (UC(p)[2] << 8) | UC(p)[3])
#define UINT4ADDR(p)    (Address::n((UC(p)[0] << 24) + (UC(p)[1] << 16) + (UC(p)[2] << 8) + UC(p)[3]))


HpSomBinaryLoader::HpSomBinaryLoader()
	: m_loadedImage(0)
{
}


HpSomBinaryLoader::~HpSomBinaryLoader()
{
	delete [] m_loadedImage;
}


void HpSomBinaryLoader::initialize(IBinaryImage *image, IBinarySymbolTable *symbols)
{
	m_image   = image;
	m_symbols = symbols;
}


static int Read2(short *ps)
{
	unsigned char *p = (unsigned char *)ps;
	// Little endian
	int n = (int(p[0]) << 8) | p[1];

	return n;
}


int Read4(int *pi)
{
	short *p = (short *)pi;
	int   n1 = Read2(p);
	int   n2 = Read2(p + 1);
	int   n  = (int)((n1 << 16) | n2);

	return n;
}


// Functions to recognise various instruction patterns
// Note: these are not presently used. May be needed again if it turns out
// that addresses in the PLT do not always point to the BOR (Bind On Reference,
// a kind of stub)
#if 0
bool isLDW(unsigned instr, int& offset, unsigned dest)
{
	if (((instr >> 26) == 0x12) &&              // Opcode
		(instr & 1) &&                          // Offset is neg
		(((instr >> 21) & 0x1f) == 27) &&       // register b
		(((instr >> 16) & 0x1f) == dest)) {     // register t
		offset = ((((int)instr << 31) >> 18) |
				  ((instr & 0x3ffe) >> 1));
		return true;
	}
	else {
		return false;
	}
}


bool isLDSID(unsigned instr)
{
	// Looking for LDSID       (%s0,%r21),%r1
	return(instr == 0x02a010a1);
}


bool isMSTP(unsigned instr)
{
	// Looking for MTSP        %r1,%s0
	return(instr == 0x00011820);
}


bool isBE(unsigned instr)
{
	// Looking for BE          0(%s0,%r21)
	return(instr == 0xe2a00000);
}


bool isSTW(unsigned instr)
{
	// Looking for STW         %r2,-24(%s0,%r30)
	return(instr == 0x6bc23fd1);
}


bool isStub(ADDRESS hostAddr, int& offset)
{
	// Looking for this pattern:
	// 2600: 4b753f91  LDW         -56(%s0,%r27),%r21
	// 2604: 4b733f99  LDW         -52(%s0,%r27),%r19
	// 2608: 02a010a1  LDSID       (%s0,%r21),%r1
	// 260c: 00011820  MTSP        %r1,%s0
	// 2610: e2a00000  BE          0(%s0,%r21)
	// 2614: 6bc23fd1  STW         %r2,-24(%s0,%r30)
	// Where the only things that vary are the first two offsets (here -56 and
	// -52)
	unsigned instr;
	int      offset1, offset2;

	instr     = *((unsigned *)hostAddr);
	hostAddr += 4;

	if (!isLDW(instr, offset1, 21)) {
		return false;
	}

	instr     = *((unsigned *)hostAddr);
	hostAddr += 4;

	if (!isLDW(instr, offset2, 19)) {
		return false;
	}

	instr     = *((unsigned *)hostAddr);
	hostAddr += 4;

	if (!isLDSID(instr)) {
		return false;
	}

	instr     = *((unsigned *)hostAddr);
	hostAddr += 4;

	if (!isMSTP(instr)) {
		return false;
	}

	instr     = *((unsigned *)hostAddr);
	hostAddr += 4;

	if (!isBE(instr)) {
		return false;
	}

	instr = *((unsigned *)hostAddr);

	if (!isSTW(instr)) {
		return false;
	}

	if ((offset2 - offset1) != 4) {
		return false;
	}

	offset = offset1;
	return true;
}


#endif

// Read the main symbol table, if any
void HpSomBinaryLoader::processSymbols()
{
	// Find the main symbol table, if it exists

	unsigned numSym = UINT4(m_loadedImage + 0x60);

	   Address symPtr  = Address::host_ptr(m_loadedImage) + UINT4(m_loadedImage + 0x5C);
	char    *pNames = (char *)(m_loadedImage + (int)UINT4(m_loadedImage + 0x6C));

#define SYMSIZE    20 // 5 4-byte words per symbol entry
#define SYMBOLNM(idx)     (UINT4((symPtr + idx * SYMSIZE + 4).value()))
#define SYMBOLAUX(idx)    (UINT4((symPtr + idx * SYMSIZE + 8).value()))
#define SYMBOLVAL(idx)    (UINT4((symPtr + idx * SYMSIZE + 16).value()))
#define SYMBOLTY(idx)     ((UINT4((symPtr + idx * SYMSIZE).value()) >> 24) & 0x3f)

	for (unsigned u = 0; u < numSym; u++) {
		// cout << "Symbol " << pNames+SYMBOLNM(u) << ", type " << SYMBOLTY(u) << ", value " << hex << SYMBOLVAL(u)
		// << ", aux " << SYMBOLAUX(u) << endl;
		unsigned symbol_type = SYMBOLTY(u);
		      Address  value       = Address::n(SYMBOLVAL(u));
		char     *pSymName   = pNames + SYMBOLNM(u);

		// Only interested in type 3 (code), 8 (stub), and 12 (millicode)
		if ((symbol_type != 3) && (symbol_type != 8) && (symbol_type != 12)) {
			// 2 - initialized data
			// 4 - primary entry point
			// 5 - secondary entrypoint
			// 6- any antry code
			// 7 - uninitialized data blocks
			// 9 - MODULE name of source module
			if (m_symbols->find(value)) {
				continue;
			}

			if (!m_symbols->find(pSymName)) {
				m_symbols->create(value, pSymName);
			}

			continue;
		}

		//          if ((symbol_type == 10) || (symbol_type == 11))
		// These are extension entries; not interested
		//              continue;
		// Ignore symbols starting with one $; for example, there are many
		// $CODE$ (but we want to see helper functions like $$remU)
		if ((pSymName[0] == '$') && (pSymName[1] != '$')) {
			continue;
		}

		//          if ((symbol_type >= 3) && (symbol_type <= 8))
		// Addresses of code; remove the privilege bits
		value = Address(value.value() & 3);

		// HP's symbol table is crazy. It seems that imports like printf have entries of type 3 with the wrong
		// value. So we have to check whether the symbol has already been entered (assume first one is correct).
		if ((m_symbols->find(value) == nullptr) && (m_symbols->find(pSymName) == nullptr)) {
			m_symbols->create(value, pSymName);
		}

		// cout << "Symbol " << pNames+SYMBOLNM(u) << ", type " << SYMBOLTY(u) << ", value " << hex << value << ",
		// aux " << SYMBOLAUX(u) << endl;  // HACK!
	}
}


bool HpSomBinaryLoader::loadFromMemory(QByteArray& imgdata)
{
	QBuffer fp(&imgdata);

	fp.open(QBuffer::ReadOnly);

	if (!fp.open(QFile::ReadOnly)) {
		fprintf(stderr, "Could not open binary file \n");
		return false;
	}

	long size = fp.size();

	// Allocate a buffer for the image
	m_loadedImage = new unsigned char[size];

	if (m_loadedImage == 0) {
		fprintf(stderr, "Could not allocate %ld bytes for image\n", size);
		return false;
	}

	memset(m_loadedImage, 0, size);


	if (fp.read((char *)m_loadedImage, size) != (unsigned)size) {
		fprintf(stderr, "Error reading binary file\n");
		return false;
	}

	// Check type at offset 0x0; should be 0x0210 or 0x20B then
	// 0107, 0108, or 010B
	unsigned magic     = UINT4(m_loadedImage);
	unsigned system_id = magic >> 16;
	unsigned a_magic   = magic & 0xFFFF;

	if (((system_id != 0x210) && (system_id != 0x20B)) ||
		((a_magic != 0x107) && (a_magic != 0x108) && (a_magic != 0x10B))) {
		fprintf(stderr, "File is not a standard PA/RISC executable file, with system ID %X and magic number %X\n",
				system_id, a_magic);
		return false;
	}

	// Find the array of aux headers
	unsigned *auxHeaders = (unsigned *)intptr_t(UINT4(m_loadedImage + 0x1c));

	if (auxHeaders == 0) {
		fprintf(stderr, "Error: auxilliary header array is not present\n");
		return false;
	}

	// Get the size of the aux headers
	unsigned sizeAux = UINT4(m_loadedImage + 0x20);
	// Search through the auxillary headers. There should be one of type 4
	// ("Exec Auxilliary Header")
	bool     found   = false;
	unsigned *maxAux = auxHeaders + sizeAux;

	while (auxHeaders < maxAux) {
		if ((UINT4(m_loadedImage + Address::value_type(auxHeaders)) & 0xFFFF) == 0x0004) {
			found = true;
			break;
		}

		// Skip this one; length is at the second word. Rightshift by 2 for
		// sizeof(unsigned).
		auxHeaders += (UINT4((UC(auxHeaders + 1)))) >> 2;
	}

	if (!found) {
		fprintf(stderr, "Error: Exec auxilliary header not found\n");
		return false;
	}

	// There will be just three sections:
	// one for text, one for initialised data, one for BSS

	// Find the DL Table, if it exists
	// The DL table (Dynamic Link info?) is supposed to be at the start of
	// the $TEXT$ space, but the only way I can presently find that is to
	// assume that the first subspace entry points to it
	char         *subspace_location     = (char *)m_loadedImage + UINT4(m_loadedImage + 0x34);
	Address      first_subspace_fileloc = Address::g(UINT4(subspace_location + 8));
	char         *DLTable     = (char *)m_loadedImage + first_subspace_fileloc.value();
	char         *pDlStrings  = DLTable + UINT4(DLTable + 0x28);
	unsigned     numImports   = UINT4(DLTable + 0x14); // Number of import strings
	import_entry *import_list = (import_entry *)(DLTable + UINT4(DLTable + 0x10));
	unsigned     numExports   = UINT4(DLTable + 0x24); // Number of export strings
	export_entry *export_list = (export_entry *)(DLTable + UINT4(DLTable + 0x20));

	// A convenient macro for accessing the fields (0-11) of the auxilliary header
	// Fields 0, 1 are the header (flags, aux header type, and size)
#define AUXHDR(idx)    (UINT4(m_image + Address::value_type(auxHeaders + idx)))

	// Section 0: text (code)
	IBinarySection *text = m_image->createSection("$TEXT$", Address::n(AUXHDR(3)), Address::n(AUXHDR(3) + AUXHDR(2)));
	assert(text);
	text->setHostAddr(Address::host_ptr(m_loadedImage) + AUXHDR(4))
	   .setEntrySize(1)
	   .setCode(true)
	   .setData(false)
	   .setBss(false)
	   .setReadOnly(true)
	   .setEndian(0)
	   .addDefinedArea(Address::n(AUXHDR(3)), Address::n(AUXHDR(3) + AUXHDR(2)));

	// Section 1: initialised data
	IBinarySection *data = m_image->createSection("$DATA$", Address::n(AUXHDR(6)), Address::n(AUXHDR(6) + AUXHDR(5)));
	assert(data);
	data->setHostAddr(Address::host_ptr(m_loadedImage) + AUXHDR(7))
	   .setEntrySize(1)
	   .setCode(false)
	   .setData(true)
	   .setBss(false)
	   .setReadOnly(false)
	   .setEndian(0)
	   .addDefinedArea(Address::n(AUXHDR(6)), Address::n(AUXHDR(6) + AUXHDR(5)));
	// Section 2: BSS
	// For now, assume that BSS starts at the end of the initialised data
	IBinarySection *bss = m_image->createSection("$BSS$", Address::n(AUXHDR(6) + AUXHDR(5)),
												              Address::n(AUXHDR(6) + AUXHDR(5) + AUXHDR(8)));
	assert(bss);
	bss->setHostAddr(Address::n(0))
	   .setEntrySize(1)
	   .setCode(false)
	   .setData(false)
	   .setBss(true)
	   .setReadOnly(false)
	   .setEndian(0);

	// Work through the imports, and find those for which there are stubs using that import entry.
	// Add the addresses of any such stubs.
	ptrdiff_t deltaText = (text->getHostAddr() - text->getSourceAddr()).value();
	ptrdiff_t deltaData = (data->getHostAddr() - data->getSourceAddr()).value();
	// The "end of data" where r27 points is not necessarily the same as
	// the end of the $DATA$ space. So we have to call getSubSpaceInfo
	std::pair<Address, int> pr = getSubspaceInfo("$GLOBAL$");

	//  ADDRESS endData = pr.first + pr.second;
	pr = getSubspaceInfo("$PLT$");
	//  int minPLT = pr.first - endData;
	//  int maxPLT = minPLT + pr.second;
	   Address pltStart = pr.first;
	// cout << "Offset limits are " << dec << minPLT << " and " << maxPLT << endl;
	// Note: DLT entries come before PLT entries in the import array, but
	// the $DLT$ subsection is not necessarilly just before the $PLT$
	// subsection in memory.
	int numDLT = UINT4(DLTable + 0x40);

	// This code was for pattern patching the BOR (Bind On Reference, or library call stub) routines. It appears to be
	// unnecessary, since as they appear in the file, the PLT entries point to the BORs
#if 0
	ADDRESS startText = m_pSections[1].uHostAddr;
	ADDRESS endText   = startText + m_pSections[1].uSectionSize - 0x10;
	ADDRESS host;

	for (host = startText; host != endText; host += 4) {
		// Test this location for a BOR (library stub)
		int offset;

		if (isStub(host, offset)) {
			cout << "Found a stub with offset " << dec << offset << endl;

			if ((offset >= minPLT) && (offset < maxPLT)) {
				// This stub corresponds with an import entry
				u = (offset - minPLT) / sizeof(plt_record);
				// Add an offset for the DLT entries
				u += numDLT;
				symbols[import_list[u].name + pDlStrings] = host - deltaText;
				cout << "Added sym " << (import_list[u].name + pDlStrings) << ", value " << hex << (host - deltaText) << endl;
			}
		}
	}
#endif

	// For each PLT import table entry, add a symbol
	// u runs through import table; v through $PLT$ subspace
	// There should be a one to one correspondance between (DLT + PLT) entries and import table entries.
	// The DLT entries always come first in the import table
	unsigned   u = (unsigned)numDLT, v = 0;
	plt_record *PLTs = (plt_record *)(pltStart + deltaData).value();

	for ( ; u < numImports; u++, v++) {
		// TODO: this is a mess, needs someone who actually knows how the SOM's PLT contents are structured
		continue;
		// TODO: add some type info to the imported symbols
		// Add it to the set of imports; needed by IsDynamicLinkedProc()
		m_symbols->create(Address::n(UINT4(&PLTs[v].value)), pDlStrings + UINT4(&import_list[u].name))
		   .setAttr("Imported", true).setAttr("Function", true);
	}

	// Work through the exports, and find main. This isn't main itself,
	// but in fact a call to main.
	for (u = 0; u < numExports; u++) {
		// cout << "Exporting " << (pDlStrings+UINT4(&export_list[u].name)) << " value " << hex <<
		// UINT4(&export_list[u].value) << endl;
		if (strncmp(pDlStrings + UINT4(&export_list[u].name), "main", 4) == 0) {
			// Enter the symbol "_callmain" for this address
			m_symbols->create(UINT4ADDR(&export_list[u].value), "_callmain");
			// Found call to main. Extract the offset. See assemble_17
			// in pa-risc 1.1 manual page 5-9
			// +--------+--------+--------+----+------------+-+-+
			// | 3A (6) |  t (5) | w1 (5) |0(3)|   w2 (11)  |n|w|  BL
			// +--------+--------+--------+----+------------+-+-+
			//  31    26|25    21|20    16|1513|12         2|1|0
			// +----------------------+--------+-----+----------+
			// |wwww...              w| w1 (5) |w2lsb| w2 msb's | offset
			// +----------------------+--------+-----+----------+
			//  31                  16|15    11| 10  |9        0

			unsigned bincall = *(unsigned *)(UINT4(&export_list[u].value) + deltaText);
			int      offset  = ((((bincall & 1) << 31) >> 15) | // w
								((bincall & 0x1f0000) >> 5) |   // w1
								((bincall & 4) << 8) |          // w2@10
								((bincall & 0x1ff8) >> 3));     // w2@0..9
			// Address of main is st + 8 + offset << 2
			m_symbols->create(UINT4ADDR(&export_list[u].value) + 8 + (offset << 2), "main")
			   .setAttr("Export", true);
			break;
		}
	}

	processSymbols();
	m_symbols->find("main")->setAttr("EntryPoint", true);
	return true;
}


int HpSomBinaryLoader::canLoad(QIODevice& dev) const
{
	unsigned char buf[64];

	dev.read((char *)buf, sizeof(buf));

	if ((buf[0] == 0x02) && (buf[2] == 0x01) && ((buf[1] == 0x10) || (buf[1] == 0x0B)) &&
		((buf[3] == 0x07) || (buf[3] == 0x08) || (buf[4] == 0x0B))) {
		/* HP Som binary */
		return 5;
	}

	return 0;
}


void HpSomBinaryLoader::unload()
{
	if (m_loadedImage) {
		delete[] m_loadedImage;
		m_loadedImage = 0;
	}
}


Address HpSomBinaryLoader::getEntryPoint()
{
	assert(0); /* FIXME: Someone who understands this file please implement */
	return Address::g(0L);
}


void HpSomBinaryLoader::close()
{
	// Not implemented yet
}


bool HpSomBinaryLoader::postLoad(void *handle)
{
	Q_UNUSED(handle);
	// Not needed: for archives only
	return false;
}


LoadFmt HpSomBinaryLoader::getFormat() const
{
	return LoadFmt::PAR;
}


Machine HpSomBinaryLoader::getMachine() const
{
	return Machine::HPRISC;
}


bool HpSomBinaryLoader::isLibrary() const
{
	int type = UINT4(m_loadedImage) & 0xFFFF;

	return(type == 0x0104 || type == 0x010D || type == 0x010E || type == 0x0619);
}


Address HpSomBinaryLoader::getImageBase()
{
	return Address::g(0L);                                       /* FIXME */
}


size_t HpSomBinaryLoader::getImageSize()
{
	return UINT4(m_loadedImage + 0x24);
}


std::pair<Address, int> HpSomBinaryLoader::getSubspaceInfo(const char *ssname)
{
	std::pair<Address, int> ret(Address::g(0L), 0);
	// Get the start and length of the subspace with the given name
	subspace_dictionary_record *subSpaces = (subspace_dictionary_record *)(m_loadedImage + UINT4(m_loadedImage + 0x34));
	unsigned   numSubSpaces  = UINT4(m_loadedImage + 0x38);
	const char *spaceStrings = (const char *)(m_loadedImage + UINT4(m_loadedImage + 0x44));

	for (unsigned u = 0; u < numSubSpaces; u++) {
		const char *thisName    = (const char *)(spaceStrings + UINT4(&subSpaces[u].name));
		unsigned   thisNameSize = UINT4(spaceStrings + UINT4(&subSpaces[u].name) - 4);

		// cout << "Subspace " << thisName << " starts " << hex << subSpaces[u].subspace_start << " length " <<
		// subSpaces[u].subspace_length << endl;
		if ((thisNameSize == strlen(ssname)) && ((strcmp(thisName, ssname) == 0))) {
			ret.first  = Address(UINT4(&subSpaces[u].subspace_start));
			ret.second = UINT4(&subSpaces[u].subspace_length);
			return ret;
		}
	}

	// Failed. Return the zeroes
	return ret;
}


// Specific to BinaryFile objects that implement a "global pointer"
// Gets a pair of unsigned integers representing the address of %agp
// (first) and the value for GLOBALOFFSET (unused for ra-risc)
// The understanding at present is that the global data pointer (%r27 for
// pa-risc) points just past the end of the $GLOBAL$ subspace.
std::pair<Address, unsigned> HpSomBinaryLoader::getGlobalPointerInfo()
{
	std::pair<Address, unsigned> ret(Address::g(0L), 0);
	// Search the subspace names for "$GLOBAL$
	std::pair<Address, int> info = getSubspaceInfo("$GLOBAL$");
	// We want the end of the $GLOBAL$ section, which is the sum of the start
	// address and the size
	ret.first = info.first + info.second;
	return ret;
}


/***************************************************************************/ /**
 *
 * \brief  Get a map from ADDRESS to const char*. This map contains the
 *         native addresses and symbolic names of global data items
 *         (if any) which are shared with dynamically linked libraries.
 *         Example: __iob (basis for stdout). The ADDRESS is the native
 *         address of a pointer to the real dynamic data object.
 * \note        The caller should delete the returned map.
 * \returns     Pointer to a new map with the info
 ******************************************************************************/
std::map<Address, const char *> *HpSomBinaryLoader::getDynamicGlobalMap()
{
	// Find the DL Table, if it exists
	// The DL table (Dynamic Link info) is supposed to be at the start of
	// the $TEXT$ space, but the only way I can presently find that is to
	// assume that the first subspace entry points to it
	const char *subspace_location     = (char *)m_loadedImage + UINT4(m_loadedImage + 0x34);
	Address    first_subspace_fileloc = Address::g(UINT4(subspace_location + 8));
	const char *DLTable = (char *)m_loadedImage + first_subspace_fileloc.value();

	unsigned numDLT = UINT4(DLTable + 0x40);
	// Offset 0x38 in the DL table has the offset relative to $DATA$ (section 2)
	unsigned *p = (unsigned *)(UINT4(DLTable + 0x38) + m_image->getSectionInfo(1)->getHostAddr().value());

	// The DLT is paralelled by the first <numDLT> entries in the import table;
	// the import table has the symbolic names
	const import_entry *import_list = (import_entry *)(DLTable + UINT4(DLTable + 0x10));
	// Those names are in the DLT string table
	const char *pDlStrings = DLTable + UINT4(DLTable + 0x28);

	std::map<Address, const char *> *ret = new std::map<Address, const char *>;

	for (unsigned u = 0; u < numDLT; u++) {
		// ? Sometimes the names are just -1
		if (import_list[u].name == -1) {
			continue;
		}

		const char *str = pDlStrings + import_list[u].name;
		(*ret)[Address::g(*p++)] = str;
	}

	return ret;
}


Address HpSomBinaryLoader::getMainEntryPoint()
{
	auto sym = m_symbols->find("main");

	return sym ? sym->getLocation() : NO_ADDRESS;
}


DEFINE_PLUGIN(PluginType::Loader, IFileLoader, HpSomBinaryLoader,
			  "HpSom binary file loader", "0.4.0", "Boomerang developers")
