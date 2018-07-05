#pragma region License
/*
 * This file is part of the Boomerang Decompiler.
 *
 * See the file "LICENSE.TERMS" for information on usage and
 * redistribution of this file, and for a DISCLAIMER OF ALL
 * WARRANTIES.
 */
#pragma endregion License
#include "Boomerang.h"


#include "boomerang/codegen/CCodeGenerator.h"
#include "boomerang/core/Project.h"
#include "boomerang/core/Watcher.h"
#include "boomerang/db/CFGCompressor.h"
#include "boomerang/db/binary/BinaryImage.h"
#include "boomerang/db/binary/BinarySymbolTable.h"
#include "boomerang/db/Prog.h"
#include "boomerang/db/signature/Signature.h"
#include "boomerang/db/proc/UserProc.h"
#include "boomerang/frontend/Frontend.h"
#include "boomerang/util/Log.h"
#include "boomerang/util/CFGDotWriter.h"

#include <ctime>


static Boomerang *g_boomerang;


Boomerang::Boomerang()
    : m_settings(new Settings)
{
}


void Boomerang::miniDebugger(UserProc *proc, const char *description)
{
    QTextStream q_cout(stdout);
    QTextStream q_cin(stdin);

    q_cout << "decompiling " << proc->getName() << ": " << description << "\n";
    QString stopAt;
    static std::set<Statement *> watches;

    if (stopAt.isEmpty() || !proc->getName().compare(stopAt)) {
        // This is a mini command line debugger.  Feel free to expand it.
        for (auto const& watche : watches) {
            (watche)->print(q_cout);
            q_cout << "\n";
        }

        q_cout << " <press enter to continue> \n";
        QString line;

        while (1) {
            line.clear();
            q_cin >> line;

            if (line.startsWith("print")) {
                proc->print(q_cout);
            }
            else if (line.startsWith("fprint")) {
                QFile tgt("out.proc");

                if (tgt.open(QFile::WriteOnly)) {
                    QTextStream of(&tgt);
                    proc->print(of);
                }
            }
            else if (line.startsWith("run ")) {
                QStringList parts = line.trimmed().split(" ", QString::SkipEmptyParts);

                if (parts.size() > 1) {
                    stopAt = parts[1];
                }

                break;
            }
            else if (line.startsWith("watch ")) {
                QStringList parts = line.trimmed().split(" ", QString::SkipEmptyParts);

                if (parts.size() > 1) {
                    int           n = parts[1].toInt();
                    StatementList stmts;
                    proc->getStatements(stmts);
                    StatementList::iterator it;

                    for (it = stmts.begin(); it != stmts.end(); ++it) {
                        if ((*it)->getNumber() == n) {
                            watches.insert(*it);
                            q_cout << "watching " << *it << "\n";
                        }
                    }
                }
            }
            else {
                break;
            }
        }
    }
}


Boomerang *Boomerang::get()
{
    if (!g_boomerang) {
        g_boomerang = new Boomerang();
    }

    return g_boomerang;
}


void Boomerang::destroy()
{
    delete g_boomerang;
    g_boomerang = nullptr;
}


void Boomerang::alertDecompileDebugPoint(UserProc *p, const char *description)
{
    if (m_currentProject->getSettings()->stopAtDebugPoints) {
        miniDebugger(p, description);
    }

    for (IWatcher *elem : m_watchers) {
        elem->onDecompileDebugPoint(p, description);
    }
}


const char *Boomerang::getVersionStr()
{
    return BOOMERANG_VERSION;
}


void Boomerang::addWatcher(IWatcher* watcher)
{
    m_watchers.insert(watcher);
}


void Boomerang::alertFunctionCreated(Function* function)
{
    for (IWatcher *it : m_watchers) {
        it->onFunctionCreated(function);
    }
}


void Boomerang::alertFunctionRemoved(Function* function)
{
    for (IWatcher *it : m_watchers) {
        it->onFunctionRemoved(function);
    }
}


void Boomerang::alertSignatureUpdated(Function* function)
{
    for (IWatcher *it : m_watchers) {
        it->onSignatureUpdated(function);
    }
}


void Boomerang::alertInstructionDecoded(Address pc, int numBytes)
{
    for (IWatcher *it : m_watchers) {
        it->onInstructionDecoded(pc, numBytes);
    }
}


void Boomerang::alertBadDecode(Address pc)
{
    for (IWatcher *it : m_watchers) {
        it->onBadDecode(pc);
    }
}


void Boomerang::alertFunctionDecoded(Function* p, Address pc, Address last, int numBytes)
{
    for (IWatcher *it : m_watchers) {
        it->onFunctionDecoded(p, pc, last, numBytes);
    }
}


void Boomerang::alertStartDecode(Address start, int numBytes)
{
    for (IWatcher *it : m_watchers) {
        it->onStartDecode(start, numBytes);
    }
}


void Boomerang::alertEndDecode()
{
    for (IWatcher *it : m_watchers) {
        it->onEndDecode();
    }
}


void Boomerang::alertStartDecompile(UserProc* proc)
{
    for (IWatcher *it : m_watchers) {
        it->onStartDecompile(proc);
    }
}


void Boomerang::alertProcStatusChanged(UserProc* proc)
{
    for (IWatcher *it : m_watchers) {
        it->onProcStatusChange(proc);
    }
}


void Boomerang::alertEndDecompile(UserProc* proc)
{
    for (IWatcher *it : m_watchers) {
        it->onEndDecompile(proc);
    }
}


void Boomerang::alertDiscovered(Function* function)
{
    for (IWatcher *it : m_watchers) {
        it->onFunctionDiscovered(function);
    }
}


void Boomerang::alertDecompiling(UserProc* proc)
{
    for (IWatcher *it : m_watchers) {
        it->onDecompileInProgress(proc);
    }
}

void Boomerang::alertDecompilationEnd()
{
    for (IWatcher *w : m_watchers) {
        w->onDecompilationEnd();
    }
}
