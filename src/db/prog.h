#pragma once

/*
 * Copyright (C) 1998-2001, The University of Queensland
 * Copyright (C) 2001, Sun Microsystems, Inc
 * Copyright (C) 2002, Trent Waddington
 *
 * See the file "LICENSE.TERMS" for information on usage and
 * redistribution of this file, and for a DISCLAIMER OF ALL
 * WARRANTIES.
 *
 */

/*========================================================================*//**
* \file        prog.h
* OVERVIEW:    interface for the program object.
******************************************************************************/

#include <map>

#include "core/BinaryFileFactory.h"
#include "include/frontend.h"
#include "type/type.h"
#include "db/module.h"
#include "util/Util.h"
#include "loader/IBinaryFile.h"

// TODO: refactor Prog Global handling into separate class
class RTLInstDict;
class Function;
class UserProc;
class LibProc;
class Signature;
class Instruction;
class InstructionSet;
class Module;
class XMLProgParser;
class IBinarySection;

struct BinarySymbol;

class HLLCode;


class Global : public Printable
{
protected:
	friend class XMLProgParser;

private:
	SharedType m_type;
	ADDRESS m_addr;
	QString m_name;
	Prog *m_parent;

public:
	Global(SharedType type, ADDRESS addr, const QString& name, Prog *p)
		: m_type(type)
		, m_addr(addr)
		, m_name(name)
		, m_parent(p) {}

	virtual ~Global() {}

	SharedType getType() const { return m_type; }
	void setType(SharedType ty) { m_type = ty; }
	void meetType(SharedType ty);

	ADDRESS getAddress()     const { return m_addr; }
	const QString& getName() const { return m_name; }

	/// return true if @p address is contained within this global.
	bool containsAddress(ADDRESS addr) const
	{
		// TODO: use getType()->getBytes()
		if (addr == m_addr) {
			return true;
		}

		return (addr > m_addr) && (addr <= (m_addr + getType()->getBytes()));
	}

	/// Get the initial value as an expression (or nullptr if not initialised)
	SharedExp getInitialValue(const Prog *prog) const;
	QString toString() const override;

protected:
	Global()
		: m_type(nullptr)
		, m_addr(ADDRESS::g(0L)) {}
};


class Prog : public QObject
{
	Q_OBJECT

	friend class XMLProgParser;

private:
	class IBinaryImage *m_image;
	SymTab *m_binarySymbols;

public:
	/// The type for the list of functions.
	typedef std::list<Module *>          ModuleList;
	typedef ModuleList::iterator         iterator;
	typedef ModuleList::const_iterator   const_iterator;

private:
	ModuleList m_moduleList;  ///< The Modules that make up this program

public:
	typedef std::map<ADDRESS, BinarySymbol *> AddressToSymbolMap;

	Prog(const QString& name);
	virtual ~Prog();

	void setFrontEnd(FrontEnd *_pFE);

	FrontEnd *getFrontEnd() const { return m_defaultFrontend; }

	/// Assign a name to this program
	void setName(const char *name);

	/***************************************************************************/ /**
	 * \note     Formally Frontend::newProc
	 * \brief    Call this function when a procedure is discovered (usually by
	 *           decoding a call instruction). That way, it is given a name
	 *           that can be displayed in the dot file, etc. If we assign it
	 *           a number now, then it will retain this number always
	 * \param uAddr - Native address of the procedure entry point
	 * \returns       Pointer to the Proc object, or 0 if this is a deleted (not to
	 *                be decoded) address
	 ******************************************************************************/
	Function *setNewProc(ADDRESS uNative);

	void removeProc(const QString& name);

	QString getName() const { return m_name; } ///< Get the name of this program

	QString getPath() const { return m_path; }
	QString getPathAndName() const { return(m_path + m_name); }

	/***************************************************************************/ /**
	 *
	 * \brief    Return the number of user (non deleted, non library) procedures
	 * \returns  The number of procedures
	 ******************************************************************************/
	int getNumProcs(bool user_only = true) const;

	Function *findProc(ADDRESS uAddr) const;
	Function *findProc(const QString& name) const;

	/***************************************************************************/ /**
	 * \brief    Return a pointer to the Proc object containing uAddr, or 0 if none
	 * \note     Could return nullptr for a deleted Proc
	 * \param uAddr - Native address to search for
	 * \returns       Pointer to the Proc object, or 0 if none, or -1 if deleted
	 ******************************************************************************/
	Function *findContainingProc(ADDRESS uAddr) const;

	/***************************************************************************/ /**
	 * \brief    Return true if this is a real procedure
	 * \param addr   Native address of the procedure entry point
	 * \returns      True if a real (non deleted) proc
	 ******************************************************************************/
	bool isProcLabel(ADDRESS addr) const;

	/***************************************************************************/ /**
	 * \brief Get the name for the progam, without any path at the front
	 * \returns A string with the name
	 ******************************************************************************/
	QString getNameNoPath() const;

	/***************************************************************************/ /**
	 * \brief Get the name for the progam, without any path at the front, and no extension
	 * \sa Prog::getNameNoPath
	 * \returns A string with the name
	 ******************************************************************************/
	QString getNameNoPathNoExt() const;
	UserProc *getFirstUserProc(std::list<Function *>::iterator& it) const;
	UserProc *getNextUserProc(std::list<Function *>::iterator& it) const;

	void clear();

	/***************************************************************************/ /**
	 * \brief    Lookup the given native address in the code section,
	 *           returning a host pointer corresponding to the same address
	 *
	 * \param uAddr Native address of the candidate string or constant
	 * \param last  will be set to one past end of the code section (host)
	 * \param delta will be set to the difference between the host and native addresses
	 * \returns     Host pointer if in range; nullptr if not
	 *              Also sets 2 reference parameters (see above)
	 ******************************************************************************/
	const void *getCodeInfo(ADDRESS uAddr, const char *& last, int& delta) const;

	QString getRegName(int idx) const { return m_defaultFrontend->getRegName(idx); }
	int getRegSize(int idx) const { return m_defaultFrontend->getRegSize(idx); }

	/***************************************************************************/ /**
	 * \brief    Decode from entry point given as an agrument
	 * \param a -  Native address of the entry point
	 ******************************************************************************/
	void decodeEntryPoint(ADDRESS a);

	/***************************************************************************/ /**
	 * \brief    Add entry point given as an agrument to the list of entryProcs
	 * \param a -  Native address of the entry point
	 ******************************************************************************/
	void setEntryPoint(ADDRESS a);
	void decodeEverythingUndecoded();
	void decodeFragment(UserProc *proc, ADDRESS a);

	/// Re-decode this proc from scratch
	void reDecode(UserProc *proc);

	/// Well form all the procedures/cfgs in this program
	bool wellForm() const;

	/// last fixes after decoding everything
	void finishDecode();

	/// Do the main non-global decompilation steps
	void decompile();

	/// As the name suggests, removes globals unused in the decompiled code.
	void removeUnusedGlobals();
	void removeRestoreStmts(InstructionSet& rs);
	void globalTypeAnalysis();

	/***************************************************************************/ /**
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
	 ******************************************************************************/
	bool removeUnusedReturns();

	/// Have to transform out of SSA form after the above final pass
	/// Convert from SSA form
	void fromSSAform();

	/// Constraint based type analysis
	void conTypeAnalysis();
	void dfaTypeAnalysis();
	void rangeAnalysis();

	/// Generate dotty file
	void generateDotFile() const;
	void generateCode(QTextStream& os) const;
	void generateCode(Module *cluster = nullptr, UserProc *proc = nullptr, bool intermixRTL = false) const;
	void generateRTL(Module *cluster = nullptr, UserProc *proc = nullptr) const;
	void print(QTextStream& out) const;

	LibProc *getLibraryProc(const QString& nam) const;
	Signature *getLibSignature(const QString& name) const;
	Instruction *getStmtAtLex(Module *cluster, unsigned int begin, unsigned int end) const;
	Platform getFrontEndId() const;

	std::shared_ptr<Signature> getDefaultSignature(const char *name) const;

	std::vector<SharedExp>& getDefaultParams();

	std::vector<SharedExp>& getDefaultReturns();

	/// Returns true if this is a win32 program
	bool isWin32() const;

	/// Get a global variable if possible, looking up the loader's symbol table if necessary
	QString getGlobalName(ADDRESS uaddr) const;

	/// Get a named global variable if possible, looking up the loader's symbol table if necessary
	ADDRESS getGlobalAddr(const QString& nam) const;
	Global *getGlobal(const QString& nam) const;

	/// Make up a name for a new global at address \a uaddr
	/// (or return an existing name if address already used)
	QString newGlobalName(ADDRESS uaddr);

	/// Guess a global's type based on its name and address
	SharedType guessGlobalType(const QString& nam, ADDRESS u) const;

	/// Make an array type for the global array at u. Mainly, set the length sensibly
	std::shared_ptr<ArrayType> makeArrayType(ADDRESS u, SharedType t);

	/// Indicate that a given global has been seen used in the program.
	bool markGlobalUsed(ADDRESS uaddr, SharedType knownType = nullptr);

	/// Get the type of a global variable
	SharedType getGlobalType(const QString& nam) const;

	/// Set the type of a global variable
	void setGlobalType(const QString& name, SharedType ty);

	/// Dump the globals to stderr for debugging
	void dumpGlobals() const;

	/// get a string constant at a given address if appropriate
	/// if knownString, it is already known to be a char*
	/// get a string constant at a give address if appropriate
	const char *getStringConstant(ADDRESS uaddr, bool knownString = false) const;
	double getFloatConstant(ADDRESS uaddr, bool& ok, int bits = 64) const;

	// Hacks for Mike
	/// Get a code for the machine e.g. MACHINE_SPARC
	Machine getMachine() const;

	/// Get a symbol from an address
	QString getSymbolByAddress(ADDRESS dest) const;

	const IBinarySection *getSectionInfoByAddr(ADDRESS a) const;
	ADDRESS getLimitTextLow() const;
	ADDRESS getLimitTextHigh() const;

	bool isReadOnly(ADDRESS a) const;
	bool isStringConstant(ADDRESS a) const;
	bool isCFStringConstant(ADDRESS a) const;

	// Read 1, 2, 4, or 8 bytes given a native address
	int readNative1(ADDRESS a) const;
	int readNative2(ADDRESS a) const;
	int readNative4(ADDRESS a) const;
	SharedExp readNativeAs(ADDRESS uaddr, SharedType type) const;

	bool isDynamicLinkedProcPointer(ADDRESS dest) const;
	const QString& getDynamicProcName(ADDRESS uNative) const;

	bool processProc(ADDRESS addr, UserProc *proc) // Decode a proc
	{
		QTextStream os(stderr);                    // rtl output target

		return m_defaultFrontend->processProc(addr, proc, os);
	}

	void readSymbolFile(const QString& fname);

	size_t getImageSize()  const { return m_loaderIface->getImageSize(); }
	ADDRESS getImageBase() const { return m_loaderIface->getImageBase(); }
	void printSymbolsToFile() const;
	void printCallGraph() const;
	void printCallGraphXML() const;

	Module *getRootCluster() const { return m_rootCluster; }
	Module *findModule(const QString& name) const;
	Module *getDefaultModule(const QString& name);
	bool isModuleUsed(Module *c) const;

	/// Add the given RTL to the front end's map from address to aldready-decoded-RTL
	void addDecodedRtl(ADDRESS a, RTL *rtl) { m_defaultFrontend->addDecodedRtl(a, rtl); }

	/***************************************************************************/ /**
	 * \brief This does extra processing on a constant.
	 * The Exp* \a e is expected to be a Const, and the ADDRESS \a lc is the native
	 * location from which the constant was read.
	 * \returns processed Exp
	 ******************************************************************************/
	SharedExp addReloc(SharedExp e, ADDRESS lc);


	/// Create or retrieve existing module
	/// \param frontend for the module, if nullptr set it to program's default frontend.
	/// \param fact abstract factory object that creates Module instance
	/// \param name retrieve/create module with this name.
	Module *getOrInsertModule(const QString& name, const ModuleFactory& fact = DefaultModFactory(), FrontEnd *frontend = nullptr);

	const ModuleList& getModuleList() const { return m_moduleList; }
	ModuleList& getModuleList()       { return m_moduleList; }

	iterator begin()       { return m_moduleList.begin(); }
	const_iterator begin() const { return m_moduleList.begin(); }
	iterator end()         { return m_moduleList.end(); }
	const_iterator end()   const { return m_moduleList.end(); }

	size_t size()  const { return m_moduleList.size(); }
	bool empty() const { return m_moduleList.empty(); }

	void generateDataSectionCode(QString section_name, ADDRESS section_start, uint32_t size, HLLCode *code) const;

signals:
	void rereadLibSignatures();

public:
	// Public booleans that are set if and when a register jump or call is
	// found, respectively
	bool bRegisterJump;
	bool bRegisterCall;

protected:
	// list of UserProcs for entry point(s)
	std::list<UserProc *> m_entryProcs;

	IFileLoader *m_loaderIface = nullptr;
	FrontEnd *m_defaultFrontend; ///< Pointer to the FrontEnd object for the project

	/* Persistent state */
	QString m_name;               // name of the program
	QString m_path;               // its full path
	// FIXME: is a set of Globals the most appropriate data structure? Surely not.
	std::set<Global *> m_globals; ///< globals to print at code generation time
	DataIntervalMap m_globalMap;  ///< Map from address to DataInterval (has size, name, type)
	int m_iNumberedProc;          ///< Next numbered proc will use this
	Module *m_rootCluster;        ///< Root of the cluster tree
};
