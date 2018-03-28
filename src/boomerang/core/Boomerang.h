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


#include "boomerang/core/Watcher.h"
#include "boomerang/util/Log.h"
#include "boomerang/core/Settings.h"

#include <QDir>
#include <QTextStream>
#include <string>
#include <set>
#include <vector>
#include <map>


class QString;
class SeparateLogger;
class Log;
class Prog;
class Function;
class UserProc;
class ICodeGenerator;
class ObjcModule;
class Project;

/**
 * Controls the loading, decoding, decompilation and code generation for a program.
 * This is the main class of the decompiler.
 */
class Boomerang
{
private:
    /**
     * Initializes the Boomerang object.
     * The default settings are:
     * - All options disabled
     * - Infinite propagations
     * - A maximum memory depth of 99
     * - The path to the executable is "./"
     * - The output directory is "./output/"
     * - Main log stream is output on stderr
     */
    Boomerang();
    Boomerang(const Boomerang& other) = delete;
    Boomerang(Boomerang&& other) = default;

    virtual ~Boomerang() = default;

    Boomerang& operator=(const Boomerang& other) = delete;
    Boomerang& operator=(Boomerang&& other) = default;

public:
    /// \returns The global boomerang object. It will be created if it does not already exist.
    static Boomerang *get();
    static void destroy();

    /// \returns the library version string
    static const char *getVersionStr();

    Settings *getSettings() { return m_settings.get(); }
    const Settings *getSettings() const { return m_settings.get(); }

public:
    /// Add a Watcher to the set of Watchers for this Boomerang object.
    void addWatcher(IWatcher *watcher);

    /// Alert the watchers that decompilation has completed.
    void alertDecompileComplete();

    /// Alert the watchers we have found a new Proc.
    void alertNew(Function *p);

    /// Alert the watchers we have removed a %Proc.
    void alertRemove(Function *p);

    /// Alert the watchers we have updated this Procs signature
    void alertUpdateSignature(Function *p);

    /// Alert the watchers we are currently decoding \p numBytes bytes at address \p pc.
    void alertDecode(Address pc, int numBytes);

    /// Alert the watchers of a bad decode of an instruction at \a pc.
    void alertBadDecode(Address pc);

    /// Alert the watchers we have succesfully decoded this function
    void alertDecode(Function *p, Address pc, Address last, int numBytes);

    /// Alert the watchers we have loaded the Proc.
    void alertLoad(Function *p);

    /// Alert the watchers we are starting to decode.
    void alertStartDecode(Address start, int numBytes);

    /// Alert the watchers we finished decoding.
    void alertEndDecode();
    void alertStartDecompile(UserProc *p);
    void alertProcStatusChange(UserProc *p);
    void alertDecompileSSADepth(UserProc *p, int depth);
    void alertDecompileBeforePropagate(UserProc *p, int depth);
    void alertDecompileAfterPropagate(UserProc *p, int depth);
    void alertDecompileAfterRemoveStmts(UserProc *p, int depth);
    void alertEndDecompile(UserProc *p);
    void alertDiscovered(Function *_parent, Function *p);
    void alertDecompiling(UserProc *p);
    void alertDecompileDebugPoint(UserProc *p, const char *description);

public:
    std::unique_ptr<Settings> m_settings;
    std::unique_ptr<Project> m_currentProject;

    std::set<IWatcher *> m_watchers;        ///< The watchers which are interested in this decompilation.

private:
    /// This is a mini command line debugger.  Feel free to expand it.
    void miniDebugger(UserProc *p, const char *description);
};

/**
 * Global settings
 */

#define SETTING(var)    (Boomerang::get()->getSettings()->var)

#define DEBUG_TA                (SETTING(debugTA))
#define DEBUG_PROOF             (SETTING(debugProof))
#define DEBUG_UNUSED            (SETTING(debugUnused))
#define DEBUG_SWITCH            (SETTING(debugSwitch))
#define DEBUG_GEN               (SETTING(debugGen))
#define DEBUG_DECODER           (SETTING(debugDecoder))
#define DFA_TYPE_ANALYSIS       (SETTING(dfaTypeAnalysis))
#define EXPERIMENTAL            (SETTING(experimental))
