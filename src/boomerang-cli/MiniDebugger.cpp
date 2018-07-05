#pragma region License
/*
 * This file is part of the Boomerang Decompiler.
 *
 * See the file "LICENSE.TERMS" for information on usage and
 * redistribution of this file, and for a DISCLAIMER OF ALL
 * WARRANTIES.
 */
#pragma endregion License
#include "MiniDebugger.h"


#include "boomerang/core/Boomerang.h"
#include "boomerang/db/statements/Statement.h"
#include "boomerang/db/proc/UserProc.h"


void MiniDebugger::onDecompileDebugPoint(UserProc *proc, const char *description)
{
    if (SETTING(stopAtDebugPoints)) {
        miniDebugger(proc, description);
    }
}


void MiniDebugger::miniDebugger(UserProc *proc, const char *description)
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
