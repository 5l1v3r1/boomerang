#pragma region License
/*
 * This file is part of the Boomerang Decompiler.
 *
 * See the file "LICENSE.TERMS" for information on usage and
 * redistribution of this file, and for a DISCLAIMER OF ALL
 * WARRANTIES.
 */
#pragma endregion License
#pragma once


#include <map>

#include "boomerang/type/type/Type.h"
#include "boomerang/type/DataIntervalMap.h"
#include "boomerang/db/binary/BinarySymbolTable.h"
#include "boomerang/db/binary/BinaryFile.h"
#include "boomerang/db/Module.h"
#include "boomerang/util/Util.h"
#include "boomerang/frontend/Frontend.h"


class RTLInstDict;
class Function;
class UserProc;
class LibProc;
class Signature;
class Statement;
class Module;
class BinarySection;
class ICodeGenerator;
class Global;
class BinarySymbol;
class BinaryFile;
class Project;


class Prog
{
public:
    /// The type for the list of functions.
    typedef std::list<std::unique_ptr<Module>>  ModuleList;
    typedef std::map<Address, BinarySymbol *>   AddressToSymbolMap;

public:
    Prog(const QString& name, Project *project);
    Prog(const Prog& other) = delete;
    Prog(Prog&& other) = default;

    ~Prog();

    Prog& operator=(const Prog& other) = delete;
    Prog& operator=(Prog&& other) = default;

public:
    /// Change the FrontEnd. Takes ownership of the pointer.
    void setFrontEnd(IFrontEnd *fe);
    IFrontEnd *getFrontEnd() const { return m_defaultFrontend; }

    Project *getProject() { return m_project; }
    const Project *getProject() const { return m_project; }

    /// Assign a new name to this program
    void setName(const QString& name);
    QString getName() const { return m_name; }

    BinaryFile *getBinaryFile() { return m_binaryFile; }
    const BinaryFile *getBinaryFile() const { return m_binaryFile; }

    /**
     * Creates a new empty module.
     * \param name   The name of the new module.
     * \param parent The parent of the new module.
     * \param modFactory Determines the type of Module to be created.
     * \returns the new module, or nullptr if there already exists a module with the same name and parent.
     */
    Module *createModule(const QString& name, Module *parent = nullptr, const ModuleFactory& modFactory = DefaultModFactory());

    /**
     * Create or retrieve existing module
     * \param frontend for the module, if nullptr set it to program's default frontend.
     * \param fact abstract factory object that creates Module instance
     * \param name retrieve/create module with this name.
     */
    Module *getOrInsertModule(const QString& name, const ModuleFactory& fact = DefaultModFactory(), IFrontEnd *frontend = nullptr);

    Module *getRootModule() { return m_rootModule; }
    Module *getRootModule() const { return m_rootModule; }

    Module *findModule(const QString& name);
    const Module *findModule(const QString& name) const;

    bool isModuleUsed(Module *module) const;

    const ModuleList& getModuleList() const { return m_moduleList; }

    /// Add an entry procedure at the specified address.
    /// This will fail if \p entryAddr is already the entry address of a LibProc.
    /// \returns the new or exising entry procedure, or nullptr on failure.
    Function *addEntryPoint(Address entryAddr);

    /**
     * Create a new unnamed function at address \p addr.
     * Call this method when a function is discovered (usually by
     * decoding a call instruction). That way, it is given a name
     * that can be displayed in the dot file, etc. If we assign it
     * a number now, then it will retain this number always.
     *
     * \param entryAddr Address of the entry point of the function
     * \returns Pointer to the Function, or nullptr if this is a deleted
     * (not to be decoded) address
     */
    Function *getOrCreateFunction(Address entryAddr);

    /// lookup a library procedure by name; create if does not exist
    LibProc *getOrCreateLibraryProc(const QString& name);

    /// \returns the function with entry address \p entryAddr,
    /// or nullptr if no such function exists.
    Function *getFunctionByAddr(Address entryAddr) const;

    /// \returns the function with name \p name,
    /// or nullptr if no such function exists.
    Function *getFunctionByName(const QString& name) const;

    /// Removes the function with name \p name.
    /// If there is no such function, nothing happens.
    /// \returns true if function was found and removed.
    bool removeFunction(const QString& name);

    /// \param userOnly If true, only count user functions, not lbrary functions.
    /// \returns the number of functions in this program.
    int getNumFunctions(bool userOnly = true) const;

    /// Check the wellformedness of all the procedures/cfgs in this program
    bool isWellFormed() const;


    /// Returns true if this is a win32 program
    bool isWin32() const;


    QString getRegName(int idx) const { return m_defaultFrontend->getRegName(idx); }
    int getRegSize(int idx) const { return m_defaultFrontend->getRegSize(idx); }

    /// Get the front end id used to make this prog
    Platform getFrontEndId() const;

    /// Get a code for the machine e.g. MACHINE_SPARC
    Machine getMachine() const;

    std::shared_ptr<Signature> getDefaultSignature(const char *name) const;


    /// get a string constant at a given address if appropriate
    /// if knownString, it is already known to be a char*
    /// get a string constant at a give address if appropriate
    const char *getStringConstant(Address addr, bool knownString = false) const;
    bool getFloatConstant(Address addr, double& value, int bits = 64) const;

    /// Get a symbol from an address
    QString getSymbolNameByAddr(Address dest) const;

    const BinarySection *getSectionByAddr(Address addr) const;
    Address getLimitTextLow() const;
    Address getLimitTextHigh() const;

    bool isReadOnly(Address a) const;
    bool isInStringsSection(Address a) const;
    bool isDynamicallyLinkedProcPointer(Address dest) const;
    const QString& getDynamicProcName(Address addr) const;

    /// \returns the default module for a symbol with name \p name.
    Module *getModuleForSymbol(const QString& symbolName);

    // Read 1, 2, 4, or 8 bytes given a native address
    int readNative1(Address a) const;
    int readNative2(Address a) const;
    int readNative4(Address a) const;
    SharedExp readNativeAs(Address uaddr, SharedType type) const;

    void readSymbolFile(const QString& fname);

    /**
     * This does extra processing on a constant. The expression \p e
     * is expected to be a Const, and the Address \p location
     * is the native location from which the constant was read.
     * \returns processed Exp
     */
    SharedExp addReloc(SharedExp e, Address location);

    void updateLibrarySignatures();


    // Decompilation related

    /// Decode from entry point given as an agrument
    void decodeEntryPoint(Address entryAddr);

    /// Decode a procedure fragment of \p proc starting at address \p addr.
    void decodeFragment(UserProc *proc, Address addr);

    /// Re-decode this proc from scratch
    bool reDecode(UserProc *proc);

    /// last fixes after decoding everything
    void finishDecode();

    const std::list<UserProc *>& getEntryProcs() const { return m_entryProcs; }

    // globals
    std::set<std::shared_ptr<Global>>& getGlobals() { return m_globals; }
    const std::set<std::shared_ptr<Global>>& getGlobals() const { return m_globals; }

    /// Get a global variable if possible, looking up the loader's symbol table if necessary
    QString getGlobalName(Address addr) const;

    /// Get a named global variable if possible, looking up the loader's symbol table if necessary
    Address getGlobalAddr(const QString& name) const;
    Global *getGlobal(const QString& name) const;

    /// Indicate that a given global has been seen used in the program.
    /// \returns true on success, false on failure (e.g. existing incompatible type already present)
    bool markGlobalUsed(Address uaddr, SharedType knownType = nullptr);

    /// Make an array type for the global array starting at \p startAddr.
    /// Mainly, set the length sensibly
    std::shared_ptr<ArrayType> makeArrayType(Address startAddr, SharedType baseType);

    /// Guess a global's type based on its name and address
    SharedType guessGlobalType(const QString& name, Address addr) const;

    /// Make up a name for a new global at address \a uaddr
    /// (or return an existing name if address already used)
    QString newGlobalName(Address uaddr);

    /// Get the type of a global variable
    SharedType getGlobalType(const QString& name) const;

    /// Set the type of a global variable
    void setGlobalType(const QString& name, SharedType ty);

private:
    QString m_name;                         ///< name of the program
    Project *m_project = nullptr;
    BinaryFile *m_binaryFile = nullptr;
    IFrontEnd *m_defaultFrontend = nullptr; ///< Pointer to the FrontEnd object for the project
    Module *m_rootModule = nullptr;         ///< Root of the module tree
    ModuleList m_moduleList;                ///< The Modules that make up this program

    /// list of UserProcs for entry point(s)
    std::list<UserProc *> m_entryProcs;

    // FIXME: is a set of Globals the most appropriate data structure? Surely not.
    std::set<std::shared_ptr<Global>> m_globals; ///< globals to print at code generation time
    DataIntervalMap m_globalMap;  ///< Map from address to DataInterval (has size, name, type)
};
