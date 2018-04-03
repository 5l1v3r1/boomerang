#pragma region License
/*
 * This file is part of the Boomerang Decompiler.
 *
 * See the file "LICENSE.TERMS" for information on usage and
 * redistribution of this file, and for a DISCLAIMER OF ALL
 * WARRANTIES.
 */
#pragma endregion License


#include "boomerang/core/Watcher.h"
#include "boomerang/core/Project.h"

#include <QObject>
#include <QTableWidget>
#include <QString>


class Module;
class IFrontEnd;
class Prog;
class BinaryFile;


/**
 * Interface between libboomerang and the GUI.
 */
class Decompiler : public QObject, public IWatcher
{
    Q_OBJECT

public:
    Decompiler();
    ~Decompiler();

    /// IWatcher interface
public:
    virtual void onDecompileDebugPoint(UserProc *proc, const char *description) override;
    virtual void onFunctionDiscovered(Function *parent, Function *function) override;
    virtual void onDecompileInProgress(UserProc *function) override;
    virtual void onFunctionCreated(Function *function) override;
    virtual void onFunctionRemoved(Function *function) override;
    virtual void onSignatureUpdated(Function *function) override;

signals: // Decompiler -> ui
    void loadingStarted();
    void decodingStarted();
    void decompilingStarted();
    void generatingCodeStarted();

    void loadCompleted();
    void decodeCompleted();
    void decompileCompleted();
    void generateCodeCompleted();

    void procDiscovered(const QString& caller, const QString& procName);
    void procDecompileStarted(const QString& procName);

    void userProcCreated(const QString& name, Address entryAddr);
    void libProcCreated(const QString& name, const QString& params);
    void userProcRemoved(const QString& name, Address entryAddr);
    void libProcRemoved(const QString& name);
    void moduleCreated(const QString& name);
    void functionAddedToModule(const QString& functionName, const QString& moduleName);
    void entryPointAdded(Address entryAddr, const QString& name);
    void sectionAdded(const QString& sectionName, Address start, Address end);

    void machineTypeChanged(const QString& machine);

    void debugPointHit(const QString& name, const QString& description);

public slots: // ui -> Decompiler
    void loadInputFile(const QString& inputFile, const QString& outputPath);
    void decode();
    void decompile();
    void generateCode();

    void stopWaiting();
    void rereadLibSignatures();

    void addEntryPoint(Address entryAddr, const QString& name);
    void removeEntryPoint(Address entryAddr);

    // todo: provide thread-safe access mechanism
public:
    bool getRTLForProc(const QString& name, QString& rtl);
    QString getSigFilePath(const QString& name);
    QString getClusterFile(const QString& name);
    void renameProc(const QString& oldName, const QString& newName);
    void getCompoundMembers(const QString& name, QTableWidget *tbl);

    void setDebugEnabled(bool debug) { m_debugging = debug; }
    Project *getProject() { return &m_project; }

private:
    /// After code generation, update the list of modules
    void moduleAndChildrenUpdated(Module *root);

protected:
    bool m_debugging = false;
    bool m_waiting = false;

    Project m_project;

    std::vector<Address> m_userEntrypoints;
};
