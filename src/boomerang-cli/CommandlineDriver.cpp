#pragma region License
/*
 * This file is part of the Boomerang Decompiler.
 *
 * See the file "LICENSE.TERMS" for information on usage and
 * redistribution of this file, and for a DISCLAIMER OF ALL
 * WARRANTIES.
 */
#pragma endregion License
#include "CommandlineDriver.h"


#include "boomerang/core/Boomerang.h"
#include "boomerang/db/Prog.h"
#include "boomerang/util/Log.h"
#include "boomerang/util/CFGDotWriter.h"

#include <QCoreApplication>
#include <iostream>


DecompilationThread::DecompilationThread(IProject *project)
    : m_project(project)
{
}


CommandlineDriver::CommandlineDriver(QObject *_parent)
    : QObject(_parent)
    , m_console(&m_project)
    , m_thread(&m_project)
    , m_kill_timer(this)
{
    this->connect(&m_kill_timer, &QTimer::timeout, this, &CommandlineDriver::onCompilationTimeout);
    QCoreApplication::instance()->connect(&m_thread, &DecompilationThread::finished,
                                          []() {
        QCoreApplication::instance()->quit();
    });
}


/**
 * Prints help about the command line switches.
 */
static void help()
{
    std::cout <<
        "Symbols\n"
        "  -s <addr> <name> : Define a symbol\n"
        "  -sf <filename>   : Read a symbol/signature file\n"
        "Decoding/decompilation options\n"
        "  -e <addr>        : Decode the procedure beginning at addr, and callees\n"
        "  -E <addr>        : Decode the procedure at addr, no callees\n"
        "                     Use -e and -E repeatedly for multiple entry points\n"
        "  -ic              : Decode through type 0 Indirect Calls\n"
        "  -S <min>         : Stop decompilation after specified number of minutes\n"
        "  -t               : Trace (print address of) every instruction decoded\n"
        "  -Tc              : Use old constraint-based type analysis\n"
        "  -Td              : Use data-flow-based type analysis\n"
        "  -a               : Assume ABI compliance\n"
        "Output\n"
        "  -v               : Verbose\n"
        "  -h               : This help\n"
        "  -o <output path> : Where to generate output (defaults to ./output/)\n"
        "  -r               : Print RTL for each proc to log before code generation\n"
        "  -gd <dot file>   : Generate a dotty graph of the program's CFG and DFG\n"
        "  -gc              : Generate a call graph to callgraph.dot\n"
        "  -gs              : Generate a symbol file (symbols.h)\n"
        "  -iw              : Write indirect call report to output/indirect.txt\n"
        "Misc.\n"
        "  -i [<file>]      : Interactive mode; execute commands from <file>, if present\n"
        "  -k               : Same as -i, deprecated\n"
        "  -P <path>        : Path to Boomerang files, defaults to where you run\n"
        "                     Boomerang from\n"
        "  -X               : activate eXperimental code; errors likely\n"
        "  --               : No effect (used for testing)\n"
        "Debug\n"
        "  -dc              : Debug switch (Case) analysis\n"
        "  -dd              : Debug decoder to stdout\n"
        "  -dg              : Debug code Generation\n"
        "  -dl              : Debug liveness (from SSA) code\n"
        "  -dp              : Debug proof engine\n"
        "  -ds              : Stop at debug points for keypress\n"
        "  -dt              : Debug type analysis\n"
        "  -du              : Debug removal of unused statements etc\n"
        "Restrictions\n"
        "  -nb              : No simplifications for branches\n"
        "  -nc              : No decode children in the call graph (callees)\n"
        "  -nd              : No (reduced) dataflow analysis\n"
        "  -nD              : No decompilation (at all!)\n"
        "  -nl              : No creation of local variables\n"
        "  -ng              : No replacement of expressions with Globals\n"
        "  -nn              : No removal of nullptr and unused statements\n"
        "  -np              : No replacement of expressions with Parameter names\n"
        "  -nP              : No promotion of signatures (other than main/WinMain/\n"
        "                     DriverMain)\n"
        "  -nr              : No removal of unneeded labels\n"
        "  -nR              : No removal of unused Returns\n"
        "  -l <depth>       : Limit multi-propagations to expressions with depth <depth>\n"
        "  -p <num>         : Only do num propagations\n"
        "  -m <num>         : Max memory depth\n";
}


/**
 * Prints a short usage statement.
 */
static void usage()
{
    std::cout <<
        "Usage: boomerang [ switches ] <program>\n"
        "boomerang -h for switch help\n";
}


int CommandlineDriver::applyCommandline(const QStringList& args)
{
    bool interactiveMode = false;

    if (args.size() < 2) {
        usage();
        return 1;
    }

    if ((args.size() == 2) && (args[1].compare("-h") == 0)) {
        help();
        return 1;
    }

    Boomerang& boom(*Boomerang::get());

    for (int i = 1; i < args.size(); ++i) {
        QString arg = args[i];

        if (arg[0] != '-') {
            if (i == args.size() - 1) {
                break;
            }

            // every argument but last must begin with '-'
            usage();
            return 1;
        }

        switch (arg[1].toLatin1())
        {
        case 'E':
            SETTING(decodeChildren) = false;
        // Fall through

        case 'e':
            {
                Address addr;
                SETTING(decodeMain) = false;

                if (++i == args.size()) {
                    usage();
                    return 1;
                }

                bool converted = false;
                addr = Address(args[i].toLongLong(&converted, 0));

                if (!converted) {
                    LOG_FATAL("Bad address: %1", args[i]);
                }

                Boomerang::get()->getSettings()->m_entryPoints.push_back(addr);
            }
            break;

        case 'h':
            help();
            break;

        case 'v':
            SETTING(verboseOutput) = true;
            break;

        case 'X':
            SETTING(experimental) = true;
            LOG_WARN("Activating experimental code!");
            break;

        case 'r':
            SETTING(printRTLs) = true;
            break;

        case 't':
            SETTING(traceDecoder) = true;
            break;

        case 'T':
            if (arg[2] == 'c') {
                LOG_WARN("Constraint-based type analysis is no longer supported. "
                        "Falling back to Data-Flow based type analysis.");
                SETTING(dfaTypeAnalysis) = true;
            }
            else if (arg[2] == 'd') {
                SETTING(dfaTypeAnalysis) = true;
            }

            break;

        case 'g':

            if (arg[2] == 'd') {
                SETTING(dotFile) = args[++i];
            }
            else if (arg[2] == 'c') {
                SETTING(generateCallGraph) = true;
            }
            else if (arg[2] == 's') {
                SETTING(generateSymbols)     = true;
                SETTING(stopBeforeDecompile) = true;
            }

            break;

        case 'o':
            {
                QString o_path = args[++i];

                if (!o_path.endsWith('/') && !o_path.endsWith('\\')) {
                    o_path += '/'; // Maintain the convention of a trailing slash
                }

                boom.getSettings()->setOutputDirectory(o_path);
                break;
            }

        case '-':
            break; // No effect: ignored

        case 'i':

            if (arg[2] == 'c') {
                SETTING(decodeThruIndCall) = true; // -ic;
                break;
            }
            else if (arg.size() > 2) {
                // unknown command
                break;
            }

        /* fallthrough */

        case 'k':
            {
                interactiveMode = true;

                if ((i + 1 < args.size()) && !args[i + 1].startsWith("-")) {
                    SETTING(replayFile) = args[++i];
                }
            }
            break;

        case 'P':
            {
                QDir wd(args[++i] + "/");

                if (!wd.exists()) {
                    LOG_WARN("Working directory '%1' does not exist!", wd.path());
                }
                else {
                    LOG_MSG("Working directory now '%1'", wd.path());
                }

                boom.getSettings()->setWorkingDirectory(wd.path());
                boom.getSettings()->setDataDirectory(wd.path() + "/../share/boomerang/");
                boom.getSettings()->setPluginDirectory(wd.path() + "/../lib/boomerang/plugins/");
                boom.getSettings()->setOutputDirectory(wd.path() + "/./output/");
            }
            break;

        case 'n':
            switch (arg[2].toLatin1())
            {
            case 'b':
                SETTING(branchSimplify) = false;
                break;

            case 'c':
                SETTING(decodeChildren) = false;
                break;

            case 'd':
                SETTING(useDataflow) = false;
                break;

            case 'D':
                SETTING(decompile) = false;
                break;

            case 'l':
                SETTING(useLocals) = false;
                break;

            case 'n':
                SETTING(removeNull) = false;
                break;

            case 'P':
                SETTING(usePromotion) = false;
                break;

            case 'p':
                SETTING(nameParameters) = false;
                break;

            case 'r':
                SETTING(removeLabels) = false;
                break;

            case 'R':
                SETTING(removeReturns) = false;
                break;

            case 'g':
                SETTING(useGlobals) = false;
                break;

            default:
                help();
            }

            break;

        case 'p':
            if (arg[2] == 'a') {
                SETTING(propOnlyToAll) = true;
                LOG_WARN(" * * Warning! -pa is not implemented yet!");
            }
            else {
                if (++i == args.size()) {
                    usage();
                    return 1;
                }

                SETTING(numToPropagate) = args[i].toInt();
            }

            break;

        case 's':
            {
                if (arg[2] == 'f') {
                    Boomerang::get()->getSettings()->m_symbolFiles.push_back(args[i + 1]);
                    i++;
                    break;
                }

                Address addr;

                if (++i == args.size()) {
                    usage();
                    return 1;
                }

                bool converted = false;
                addr = Address(args[i].toLongLong(&converted, 0));

                if (!converted) {
                    LOG_FATAL("Bad address: %1", args[i + 1]);
                }

                Boomerang::get()->getSettings()->m_symbolMap[addr] = args[++i];
            }
            break;

        case 'd':

            switch (arg[2].toLatin1())
            {
            case 'c':
                SETTING(debugSwitch) = true;
                break;

            case 'd':
                SETTING(debugDecoder) = true;
                break;

            case 'g':
                SETTING(debugGen) = true;
                break;

            case 'l':
                SETTING(debugLiveness) = true;
                break;

            case 'p':
                SETTING(debugProof) = true;
                break;

            case 's':
                SETTING(stopAtDebugPoints) = true;
                break;

            case 't': // debug type analysis
                SETTING(debugTA) = true;
                break;

            case 'u': // debug unused locations (including returns and parameters now)
                SETTING(debugUnused) = true;
                break;

            default:
                help();
            }

            break;

        case 'a':
            SETTING(assumeABI) = true;
            break;

        case 'l':

            if (++i == args.size()) {
                usage();
                return 1;
            }

            SETTING(propMaxDepth) = args[i].toInt();
            break;

        case 'S':
            minsToStopAfter = args[++i].toInt();
            break;

        default:
            help();
        }
    }

    if (interactiveMode) {
        return interactiveMain();
    }

    if (minsToStopAfter > 0) {
        LOG_MSG("Stopping decompile after %1 minutes", minsToStopAfter);
        m_kill_timer.setSingleShot(true);
        m_kill_timer.start(1000 * 60 * minsToStopAfter);
    }

    m_thread.setPathToBinary(args.last());
    return 0;
}


int CommandlineDriver::interactiveMain()
{
    CommandStatus status = m_console.replayFile(SETTING(replayFile));

    if (status == CommandStatus::ExitProgram) {
        return 2;
    }

    // now handle user commands
    QTextStream strm(stdin);
    QString     line;

    while (true) {
        std::cout << "boomerang: ";
        std::cout.flush();

        if (strm.atEnd()) {
            return 0;
        }

        line   = strm.readLine();
        status = m_console.handleCommand(line);

        if (status == CommandStatus::ExitProgram) {
            return 2;
        }
    }
}


int CommandlineDriver::decompile()
{
    Log::getOrCreateLog().addDefaultLogSinks();

    m_thread.start();
    m_thread.wait(); // wait indefinitely
    return m_thread.resCode();
}


void CommandlineDriver::onCompilationTimeout()
{
    LOG_WARN("Compilation timed out, Boomerang will now exit");
    exit(1);
}


void DecompilationThread::run()
{
    Boomerang& boom(*Boomerang::get());
    QDir       wd = boom.getSettings()->getWorkingDirectory();
    QFileInfo inf = QFileInfo(wd.absoluteFilePath(m_pathToBinary));

    m_result = decompile(inf.absoluteFilePath(), inf.baseName());
}


bool DecompilationThread::loadAndDecode(const QString& fname, const QString& pname)
{
    LOG_MSG("Loading...");

    assert(m_project);
    const bool ok = m_project->loadBinaryFile(fname);
    if (!ok) {
        // load failed
        return false;
    }

    Prog *prog = m_project->getProg();
    assert(prog);

    prog->setName(pname);
    return m_project->decodeBinaryFile();
}


int DecompilationThread::decompile(const QString& fname, const QString& pname)
{
    time_t start;
    time(&start);

    if (!loadAndDecode(fname, pname)) {
        return 1;
    }


    if (SETTING(stopBeforeDecompile)) {
        return 0;
    }

    LOG_MSG("Decompiling...");
    m_project->decompileBinaryFile();

    if (!SETTING(dotFile).isEmpty()) {
        CfgDotWriter().writeCFG(m_project->getProg(), SETTING(dotFile));
    }

    m_project->generateCode();

    QDir outDir = Boomerang::get()->getSettings()->getOutputDirectory();
    LOG_MSG("Output written to '%1'", outDir.absolutePath());

    time_t end;
    time(&end);
    int hours = static_cast<int>((end - start) / 60 / 60);
    int mins  = static_cast<int>((end - start) / 60 - hours * 60);
    int secs  = static_cast<int>((end - start) - (hours * 60 * 60) - (mins * 60));

    LOG_MSG("Completed in %1 hours %2 minutes %3 seconds.", hours, mins, secs);
    return 0;
}
