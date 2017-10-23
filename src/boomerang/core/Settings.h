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


#include <QString>
#include <QDir>
#include <QSettings>

/**
 * Settings that affect decompilation and output behavior.
 */
class Settings
{
public:
    Settings();

public:
    /// Get the path where the boomerang executable is run from.
    QDir getWorkingDirectory() const { return m_workingDirectory; }
    void setWorkingDirectory(const QString& directoryPath) { m_workingDirectory = directoryPath; }

    /// Get the path of the data directory where plugins, ssl files etc. are stored.
    QDir getDataDirectory() const { return QDir(m_settings.value("DataDirectory").toString()); }
    void setDataDirectory(const QString& directoryPath) { m_settings.setValue("DataDirectory", directoryPath); }

    /// Get the path where the decompiled files should be put
    QDir getOutputDirectory() { return QDir(m_settings.value("OutputDirectory").toString()); }
    void setOutputDirectory(const QString& directoryPath);

public:
    // Command line flags
    bool vFlag               = false;
    bool debugSwitch         = false;
    bool debugLiveness       = false;
    bool debugTA             = false;
    bool debugDecoder        = false;
    bool debugProof          = false;
    bool debugUnused         = false;
    bool debugRangeAnalysis  = false;
    bool printRtl            = false;
    bool noBranchSimplify    = false;
    bool noRemoveNull        = false;
    bool noLocals            = false;
    bool noRemoveLabels      = false;
    bool noDataflow          = false;
    bool noDecompile         = false;
    bool stopBeforeDecompile = false;
    bool traceDecoder        = false;

    /// The file in which the dotty graph is saved
    QString dotFile;
    int numToPropagate     = -1;
    bool noPromote         = false;
    bool propOnlyToAll     = false;
    bool debugGen          = false;
    int maxMemDepth        = 99;
    bool noParameterNames  = false;
    bool stopAtDebugPoints = false;

    /// When true, attempt to decode main, all children, and all procs.
    /// \a decodeMain is set when there are no -e or -E switches given
    bool decodeMain          = true;
    bool printAST            = false;
    bool dumpXML             = false;
    bool noRemoveReturns     = false;
    bool decodeThruIndCall   = false;
    bool noDecodeChildren    = false;
    bool loadBeforeDecompile = false;
    bool saveBeforeDecompile = false;
    bool noProve             = false;
    bool noChangeSignatures  = false;
    bool conTypeAnalysis     = false;
    bool dfaTypeAnalysis     = true;
    int propMaxDepth         = 3; ///< Max depth of expression that'll be propagated to more than one dest
    bool generateCallGraph   = false;
    bool generateSymbols     = false;
    bool noGlobals           = false;
    bool assumeABI           = false; ///< Assume ABI compliance
    bool experimental        = false; ///< Activate experimental code. Caution!

    QString replayFile;               ///< file with commands to execute in interactive mode

private:
    QDir m_workingDirectory;       ///< Directory where Boomerang is run from
    QSettings m_settings;
};
