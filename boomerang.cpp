/*
 * Copyright (C) 2002-2006, Mike Van Emmerik and Trent Waddington
 */
/***************************************************************************/ /**
  * \file       boomerang.cpp
  * \brief   Command line processing for the Boomerang decompiler
  ******************************************************************************/

#include "boomerang.h"

#include "config.h"
#include "prog.h"
#include "proc.h"
#include "BinaryFile.h"
#include "frontend.h"
#include "hllcode.h"
#include "codegen/chllcode.h"
//#include "transformer.h"
#include "util.h"
#include "log.h"
#include "xmlprogparser.h"

// For the -nG switch to disable the garbage collector
#include "config.h"
#ifdef HAVE_LIBGC
#include "gc.h"
#else
#define NO_GARBAGE_COLLECTOR
#endif

#include <QDebug>
#include <inttypes.h>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <ctime>

Boomerang *Boomerang::boomerang = nullptr;

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
Boomerang::Boomerang() : progPath("./"), outputPath("./output/"), logger(nullptr),LogStream(stdout),ErrStream(stderr) {

}

/**
 * \returns the Log object associated with the object.
 */
Log &Boomerang::log() { return *logger; }

SeparateLogger Boomerang::separate_log(const QString &v) { return SeparateLogger(v); }
Log &Boomerang::if_verbose_log(int verbosity_level) {
    static NullLogger null_log;
    if (verbosity_level == 2)
        return *logger;
    if ((verbosity_level == 1) && VERBOSE)
        return *logger;
    else
        return null_log;
}

/**
 * Sets the outputfile to be the file "log" in the default output directory.
 */
FileLogger::FileLogger() : out((Boomerang::get()->getOutputPath() + "log").toStdString()) {}

SeparateLogger::SeparateLogger(const QString &v) {
    static QMap<QString, int> versions;
    if (!versions.contains(v))
        versions[v] = 0;
    QDir outDir(Boomerang::get()->getOutputPath());
    QString full_path = outDir.absoluteFilePath(QString("%1_%2.log").arg(v).arg(versions[v]++, 2, 10, QChar('0')));
    out = new std::ofstream(full_path.toStdString());
}

/**
 * Returns the HLLCode for the given proc.
 * \return The HLLCode for the specified UserProc.
 */
HLLCode *Boomerang::getHLLCode(UserProc *p) { return new CHLLCode(p); }

/**
 * Prints help for the interactive mode.
 */
void Boomerang::helpcmd() const {
    QTextStream c_out(stdout);
    // Column 98 of this source file is column 80 of output (don't use tabs)
    //            ____.____1____.____2____.____3____.____4____.____5____.____6____.____7____.____8
    c_out << "Available commands (for use with -k):\n";
    c_out << "  decode                             : Loads and decodes the specified binary.\n";
    c_out << "  decompile [proc]                   : Decompiles the program or specified proc.\n";
    c_out << "  codegen [cluster]                  : Generates code for the program or a\n";
    c_out << "                                       specified cluster.\n";
    c_out << "  move proc <proc> <cluster>         : Moves the specified proc to the specified\n";
    c_out << "                                       cluster.\n";
    c_out << "  move cluster <cluster> <parent>    : Moves the specified cluster to the\n";
    c_out << "                                       specified parent cluster.\n";
    c_out << "  add cluster <cluster> [parent]     : Adds a new cluster to the root/specified\n";
    c_out << "                                       cluster.\n";
    c_out << "  delete cluster <cluster>           : Deletes an empty cluster.\n";
    c_out << "  rename proc <proc> <newname>       : Renames the specified proc.\n";
    c_out << "  rename cluster <cluster> <newname> : Renames the specified cluster.\n";
    c_out << "  info prog                          : Print info about the program.\n";
    c_out << "  info cluster <cluster>             : Print info about a cluster.\n";
    c_out << "  info proc <proc>                   : Print info about a proc.\n";
    c_out << "  print <proc>                       : Print the RTL for a proc.\n";
    c_out << "  help                               : This help.\n";
    c_out << "  exit                               : Quit the shell.\n";
}

/**
 * Creates a directory and tests it.
 *
 * \param dir The name of the directory.
 *
 * \retval true The directory is valid.
 * \retval false The directory is invalid.
 */
bool createDirectory(const QString &dir) {
    QFileInfo dir_fi(dir);
    QString abspath = dir_fi.absolutePath();
    return QDir::root().mkpath(abspath);
}

/**
 * Prints a tree graph.
 */
void Cluster::printTree(QTextStream &ostr) {
    ostr << "\t\t" << Name << "\n";
    for (auto &elem : Children)
        elem->printTree(ostr);
}
enum CommandType {
    CT_unknown=-1,
    CT_decode=1,
    CT_load=2,
    CT_save=3,
    CT_decompile=4,
    CT_codegen=5,
    CT_move=6,
    CT_add=7,
    CT_delete=8,
    CT_rename=9,
    CT_info=10,
    CT_print=11,
    CT_exit=12,
    CT_help=13
};
static CommandType commandNameToID(const QString &_cmd) {
    if (_cmd=="decode")
        return CT_decode;
    if (_cmd == "load")
        return CT_load;
    if (_cmd == "save")
        return CT_save;
    if (_cmd == "decompile")
        return CT_decompile;
    if (_cmd == "codegen")
        return CT_codegen;
    if (_cmd == "move")
        return CT_move;
    if (_cmd == "add")
        return CT_add;
    if (_cmd == "delete")
        return CT_delete;
    if (_cmd == "rename")
        return CT_rename;
    if (_cmd == "info")
        return CT_info;
    if (_cmd == "print")
        return CT_print;
    if ((_cmd == "exit") || _cmd == "quit")
        return CT_exit;
    if (_cmd == "help")
        return CT_help;
    return CT_unknown;
}
/**
 * Parse and execute a command supplied in interactive mode.
 *
 * \param args        The array of argument strings.
 *
 * \return A value indicating what happened.
 *
 * \retval 0 Success
 * \retval 1 Failure
 * \retval 2 The user exited with \a quit or \a exit
 */
int Boomerang::processCommand(QStringList &args) {
    static Prog *prog = nullptr;
    QTextStream err_stream(stderr);
    QTextStream out_stream(stdout);
    if (args.isEmpty()) {
        err_stream << "not enough arguments\n";
        return 1;
    }

    CommandType command = commandNameToID(args[0]);
    switch (command) {
    case CT_decode: {
        if (args.size() < 2) {
            err_stream << "not enough arguments for cmd\n";
            return 1;
        }
        Prog *p = loadAndDecode(args[1]);
        if (p == nullptr) {
            err_stream << "failed to load " << args[1] << "\n";
            return 1;
        }
        prog = p;
        break;
    }
    case CT_load: {
        if (args.size() < 2) {
            err_stream << "not enough arguments for cmd\n";
            return 1;
        }
        QString fname = args[1];
        XMLProgParser *p = new XMLProgParser();
        Prog *pr = p->parse(fname);
        if (pr == nullptr) {
            // try guessing
            pr = p->parse(outputPath + fname + "/" + fname + ".xml");
            if (pr == nullptr) {
                err_stream << "failed to read xml " << fname << "\n";
                return 1;
            }
        }
        prog = pr;
        break;
    }
    case CT_save: {
        if (prog == nullptr) {
            err_stream << "need to load or decode before save!\n";
            return 1;
        }
        XMLProgParser *p = new XMLProgParser();
        p->persistToXML(prog);
        break;
    }
    case CT_decompile: {
        if (prog == nullptr) {
            err_stream << "no valid Prog object !\n";
            return 1;
        }

        if (args.size() > 1) {
            Function *proc = prog->findProc(args[1]);
            if (proc == nullptr) {
                err_stream << "cannot find proc " << args[1] << "\n";
                return 1;
            }
            if (proc->isLib()) {
                err_stream << "cannot decompile a lib proc\n";
                return 1;
            }
            int indent = 0;
            assert(dynamic_cast<UserProc *>(proc) != nullptr);
            ((UserProc *)proc)->decompile(new ProcList, indent);
        } else {
            prog->decompile();
        }
        break;
    }
    case CT_codegen: {
        if (prog == nullptr) {
            err_stream << "no valid Prog object !\n";
            return 1;
        }
        if (args.size() > 1) {
            Cluster *cluster = prog->findCluster(args[1]);
            if (cluster == nullptr) {
                err_stream << "cannot find cluster " << args[1] << "\n";
                return 1;
            }
            prog->generateCode(cluster);
        } else {
            prog->generateCode();
        }
        break;
    }
    case CT_move: {
        if (prog == nullptr) {
            err_stream << "no valid Prog object !\n";
            return 1;
        }
        if (args.size() <= 1) {
            err_stream << "not enough arguments for cmd\n";
            return 1;
        }
        if (args[1]=="proc") {
            if (args.size() < 4) {
                err_stream << "not enough arguments for cmd\n";
                return 1;
            }

            Function *proc = prog->findProc(args[2]);
            if (proc == nullptr) {
                err_stream << "cannot find proc " << args[2] << "\n";
                return 1;
            }

            Cluster *cluster = prog->findCluster(args[3]);
            if (cluster == nullptr) {
                err_stream << "cannot find cluster " << args[3] << "\n";
                return 1;
            }
            proc->setCluster(cluster);
        } else if (!args[1].compare("cluster")) {
            if (args.size() <= 3) {
                err_stream << "not enough arguments for cmd\n";
                return 1;
            }

            Cluster *cluster = prog->findCluster(args[2]);
            if (cluster == nullptr) {
                err_stream << "cannot find cluster " << args[2] << "\n";
                return 1;
            }

            Cluster *parent = prog->findCluster(args[3]);
            if (parent == nullptr) {
                err_stream << "cannot find cluster " << args[3] << "\n";
                return 1;
            }

            parent->addChild(cluster);
        } else {
            err_stream << "don't know how to move a " << args[1] << "\n";
            return 1;
        }
        break;
    }
    case CT_add: {
        if (prog == nullptr) {
            err_stream << "no valid Prog object !\n";
            return 1;
        }
        if (args.size() <= 1) {
            err_stream << "not enough arguments for cmd\n";
            return 1;
        }
        if (args[1] == "cluster") {
            if (args.size() <= 2) {
                err_stream << "not enough arguments for cmd\n";
                return 1;
            }

            Cluster *cluster = new Cluster(args[2]);
            if (cluster == nullptr) {
                err_stream << "cannot create cluster " << args[2] << "\n";
                return 1;
            }

            Cluster *parent = prog->getRootCluster();
            if (args.size() > 3) {
                parent = prog->findCluster(args[3]);
                if (cluster == nullptr) {
                    err_stream << "cannot find cluster " << args[3] << "\n";
                    return 1;
                }
            }

            parent->addChild(cluster);
        } else {
            err_stream << "don't know how to add a " << args[1] << "\n";
            return 1;
        }
        break;
    }
    case CT_delete: {
        if (prog == nullptr) {
            err_stream << "no valid Prog object !\n";
            return 1;
        }
        if (args.size() <= 1) {
            err_stream << "not enough arguments for cmd\n";
            return 1;
        }
        if (!args[1].compare("cluster")) {
            if (args.size() <= 2) {
                err_stream << "not enough arguments for cmd\n";
                return 1;
            }

            Cluster *cluster = prog->findCluster(args[2]);
            if (cluster == nullptr) {
                err_stream << "cannot find cluster " << args[2] << "\n";
                return 1;
            }

            if (cluster->hasChildren() || cluster == prog->getRootCluster()) {
                err_stream << "cluster " << args[2] << " is not empty\n";
                return 1;
            }

            if (prog->clusterUsed(cluster)) {
                err_stream << "cluster " << args[2] << " is not empty\n";
                return 1;
            }
            QFile::remove(cluster->getOutPath("xml"));
            QFile::remove(cluster->getOutPath("c"));
            assert(cluster->getParent());
            cluster->getParent()->removeChild(cluster);
        } else {
            err_stream << "don't know how to delete a " << args[1] << "\n";
            return 1;
        }
        break;
    }
    case CT_rename: {
        if (prog == nullptr) {
            err_stream << "no valid Prog object !\n";
            return 1;
        }
        if (args.size() <= 1) {
            err_stream << "not enough arguments for cmd\n";
            return 1;
        }
        if (args[1] == "proc") {
            if (args.size() <= 3) {
                err_stream << "not enough arguments for cmd\n";
                return 1;
            }

            Function *proc = prog->findProc(args[2]);
            if (proc == nullptr) {
                err_stream << "cannot find proc " << args[2] << "\n";
                return 1;
            }

            Function *nproc = prog->findProc(args[3]);
            if (nproc != nullptr) {
                err_stream << "proc " << args[3] << " already exists\n";
                return 1;
            }

            proc->setName(args[3]);
        } else if (args[1] == "cluster") {
            if (args.size() <= 3) {
                err_stream << "not enough arguments for cmd\n";
                return 1;
            }

            Cluster *cluster = prog->findCluster(args[2]);
            if (cluster == nullptr) {
                err_stream << "cannot find cluster " << args[2] << "\n";
                return 1;
            }

            Cluster *ncluster = prog->findCluster(args[3]);
            if (ncluster == nullptr) {
                err_stream << "cluster " << args[3] << " already exists\n";
                return 1;
            }

            cluster->setName(args[3]);
        } else {
            err_stream << "don't know how to rename a " << args[1] << "\n";
            return 1;
        }
        break;
    }
    case CT_info: {
        if (prog == nullptr) {
            err_stream << "no valid Prog object !\n";
            return 1;
        }
        if (args.size() <= 1) {
            err_stream << "not enough arguments for cmd\n";
            return 1;
        }
        if (args[1] == "prog") {

            out_stream << "prog " << prog->getName() << ":\n";
            out_stream << "\tclusters:\n";
            prog->getRootCluster()->printTree(out_stream);
            out_stream << "\n\tlibprocs:\n";
            PROGMAP::const_iterator it;
            for (Function *p = prog->getFirstProc(it); p; p = prog->getNextProc(it))
                if (p->isLib())
                    out_stream << "\t\t" << p->getName() << "\n";
            out_stream << "\n\tuserprocs:\n";
            for (Function *p = prog->getFirstProc(it); p; p = prog->getNextProc(it))
                if (!p->isLib())
                    out_stream << "\t\t" << p->getName() << "\n";
            out_stream << "\n";

            return 0;
        } else if (args[1] == "cluster") {
            if (args.size() <= 2) {
                err_stream << "not enough arguments for cmd\n";
                return 1;
            }

            Cluster *cluster = prog->findCluster(args[2]);
            if (cluster == nullptr) {
                err_stream << "cannot find cluster " << args[2] << "\n";
                return 1;
            }

            out_stream << "cluster " << cluster->getName() << ":\n";
            if (cluster->getParent())
                out_stream << "\tparent = " << cluster->getParent()->getName() << "\n";
            else
                out_stream << "\troot cluster.\n";
            out_stream << "\tprocs:\n";
            PROGMAP::const_iterator it;
            for (Function *p = prog->getFirstProc(it); p; p = prog->getNextProc(it))
                if (p->getCluster() == cluster)
                    out_stream << "\t\t" << p->getName() << "\n";
            out_stream << "\n";

            return 0;
        } else if (args[1] == "proc") {
            if (args.size() <= 2) {
                err_stream << "not enough arguments for cmd\n";
                return 1;
            }

            Function *proc = prog->findProc(args[2]);
            if (proc == nullptr) {
                err_stream << "cannot find proc " << args[2] << "\n";
                return 1;
            }

            out_stream << "proc " << proc->getName() << ":\n";
            out_stream << "\tbelongs to cluster " << proc->getCluster()->getName() << "\n";
            out_stream << "\tnative address " << proc->getNativeAddress() << "\n";
            if (proc->isLib())
                out_stream << "\tis a library proc.\n";
            else {
                out_stream << "\tis a user proc.\n";
                UserProc *p = (UserProc *)proc;
                if (p->isDecoded())
                    out_stream << "\thas been decoded.\n";
                // if (p->isAnalysed())
                //    out_stream << "\thas been analysed.\n";
            }
            out_stream << "\n";

            return 0;
        } else {
            err_stream << "don't know how to print info about a " << args[1] << "\n";
            return 1;
        }
        // no break needed all branches return
    }
    case CT_print: {
        if (prog == nullptr) {
            err_stream << "no valid Prog object !\n";
            return 1;
        }
        if (args.size() < 2) {
            err_stream << "not enough arguments for cmd\n";
            return 1;
        }

        Function *proc = prog->findProc(args[1]);
        if (proc == nullptr) {
            err_stream << "cannot find proc " << args[1] << "\n";
            return 1;
        }
        if (proc->isLib()) {
            err_stream << "cannot print a libproc.\n";
            return 1;
        }

        ((UserProc *)proc)->print(out_stream);
        out_stream << "\n";
        return 0;
    }
    case CT_exit: {
        return 2;
    }
    case CT_help: {
        helpcmd();
        return 0;
    }
    default:
        err_stream << "unknown cmd " << args[0] << ".\n";
        return 1;
    }
    return 0;
}

/**
 * Set the path to the %Boomerang executable.
 *
 */
void Boomerang::setProgPath(const QString &p) {
    progPath = p + "/";
    outputPath = progPath + "/output/"; // Default output path (can be overridden with -o below)
}

/**
 * Set the path where the %Boomerang executable will search for plugins.
 *
 */
void Boomerang::setPluginPath(const QString &p) { BinaryFileFactory::setBasePath(p); }

/**
 * Sets the directory in which Boomerang creates its output files.  The directory will be created if it doesn't exist.
 *
 * \param path        the path to the directory
 *
 * \retval true Success.
 * \retval false The directory could not be created.
 */
bool Boomerang::setOutputDirectory(const QString &path) {
    outputPath = path;
    // Create the output directory, if needed
    if (!createDirectory(path)) {
        qWarning() << "Warning! Could not create path " << outputPath << "!\n";
        return false;
    }
    if (logger == nullptr)
        setLogger(new FileLogger());
    return true;
}

/**
 * Adds information about functions and classes from Objective-C modules to the Prog object.
 *
 * \param modules A map from name to the Objective-C modules.
 * \param prog The Prog object to add the information to.
 */
void Boomerang::objcDecode(std::map<std::string, ObjcModule> &modules, Prog *prog) {
    LOG_VERBOSE(1) << "Adding Objective-C information to Prog.\n";
    Cluster *root = prog->getRootCluster();
    for (auto &modules_it : modules) {
        ObjcModule &mod = (modules_it).second;
        Module *module = new Module(mod.name.c_str());
        root->addChild(module);
        LOG_VERBOSE(1) << "\tModule: " << mod.name.c_str() << "\n";
        for (auto &elem : mod.classes) {
            ObjcClass &c = (elem).second;
            Class *cl = new Class(c.name.c_str());
            root->addChild(cl);
            LOG_VERBOSE(1) << "\t\tClass: " << c.name.c_str() << "\n";
            for (auto &_it2 : c.methods) {
                ObjcMethod &m = (_it2).second;
                // TODO: parse :'s in names
                Function *p = prog->newProc(m.name.c_str(), m.addr);
                p->setCluster(cl);
                // TODO: decode types in m.types
                LOG_VERBOSE(1) << "\t\t\tMethod: " << m.name.c_str() << "\n";
            }
        }
    }
    LOG_VERBOSE(1) << "\n";
}

/**
 * Loads the executable file and decodes it.
 *
 * \param fname The name of the file to load.
 * \param pname How the Prog will be named.
 *
 * \returns A Prog object.
 */
Prog *Boomerang::loadAndDecode(const QString &fname, const char *pname) {
    QTextStream q_cout(stdout);
    q_cout << "loading...\n";
    Prog *prog = new Prog();
    FrontEnd *fe = FrontEnd::Load(fname, prog);
    if (fe == nullptr) {
        LOG_STREAM() << "failed.\n";
        return nullptr;
    }
    prog->setFrontEnd(fe);

    // Add symbols from -s switch(es)
    for (auto &elem : symbols) {
        fe->AddSymbol((elem).first, (elem).second.c_str());
    }
    fe->readLibraryCatalog(); // Needed before readSymbolFile()

    for (auto &elem : symbolFiles) {
        q_cout << "reading symbol file " << elem << "\n";
        prog->readSymbolFile(elem);
    }
    ObjcAccessInterface *objc = qobject_cast<ObjcAccessInterface *>(fe->getBinaryFile());
    if (objc) {
        std::map<std::string, ObjcModule> &objcmodules = objc->getObjcModules();
        if (objcmodules.size())
            objcDecode(objcmodules, prog);
    }
    // Entry points from -e (and -E) switch(es)
    for (auto &elem : entrypoints) {
        q_cout << "decoding specified entrypoint " << elem << "\n";
        prog->decodeEntryPoint(elem);
    }

    if (entrypoints.size() == 0) { // no -e or -E given
        if (decodeMain)
            q_cout << "decoding entry point...\n";
        fe->decode(prog, decodeMain, pname);

        if (!noDecodeChildren) {
            // this causes any undecoded userprocs to be decoded
            q_cout << "decoding anything undecoded...\n";
            fe->decode(prog, NO_ADDRESS);
        }
    }

    q_cout << "finishing decode...\n";
    prog->finishDecode();

    Boomerang::get()->alertEndDecode();

    q_cout << "found " << prog->getNumUserProcs() << " procs\n";

    // GK: The analysis which was performed was not exactly very "analysing", and so it has been moved to
    // prog::finishDecode, UserProc::assignProcsToCalls and UserProc::finalSimplify
    // std::cout << "analysing...\n";
    // prog->analyse();

    if (generateSymbols) {
        prog->printSymbolsToFile();
    }
    if (generateCallGraph) {
        prog->printCallGraph();
        prog->printCallGraphXML();
    }
    return prog;
}

/**
 * The program will be subsequently be loaded, decoded, decompiled and written to a source file.
 * After decompilation the elapsed time is printed to LOG_STREAM().
 *
 * \param fname The name of the file to load.
 * \param pname The name that will be given to the Proc.
 *
 * \return Zero on success, nonzero on faillure.
 */
int Boomerang::decompile(const QString &fname, const char *pname) {
    Prog *prog;
    time_t start;
    time(&start);
    if (logger == nullptr)
        setLogger(new FileLogger());
    QTextStream q_cout(stdout);

//    std::cout << "setting up transformers...\n";
//    ExpTransformer::loadAll();

    if (loadBeforeDecompile) {
        LOG_STREAM() << "loading persisted state...\n";
        XMLProgParser *p = new XMLProgParser();
        prog = p->parse(fname);
    } else
    {
        prog = loadAndDecode(fname, pname);
        if (prog == nullptr)
            return 1;
    }

    if (saveBeforeDecompile) {
        LOG_STREAM() << "saving persistable state...\n";
        XMLProgParser *p = new XMLProgParser();
        p->persistToXML(prog);
    }

    if (stopBeforeDecompile)
        return 0;

    LOG_STREAM() << "decompiling...\n";
    prog->decompile();

    if (!dotFile.isEmpty())
        prog->generateDotFile();

    if (printAST) {
        LOG_STREAM() << "printing AST...\n";
        PROGMAP::const_iterator it;
        for (Function *p = prog->getFirstProc(it); p; p = prog->getNextProc(it))
            if (!p->isLib()) {
                UserProc *u = (UserProc *)p;
                u->getCFG()->compressCfg();
                u->printAST();
            }
    }
    q_cout << "generating code...\n";
    prog->generateCode();

    q_cout << "output written to " << outputPath << prog->getRootCluster()->getName() << "\n";

    time_t end;
    time(&end);
    int hours = (int)((end - start) / 60 / 60);
    int mins = (int)((end - start) / 60 - hours * 60);
    int secs = (int)((end - start) - (hours * 60 * 60) - (mins * 60));
    q_cout << "completed in ";
    if (hours)
        q_cout << hours << " hours ";
    if (hours || mins)
        q_cout << mins << " mins ";
    q_cout << secs << " sec" << (secs == 1 ? "" : "s") << ".\n";

    return 0;
}

/**
 * Saves the state of the Prog object to a XML file.
 * \param prog The Prog object to save.
 */
void Boomerang::persistToXML(Prog *prog) {
    LOG << "saving persistable state...\n";
    XMLProgParser *p = new XMLProgParser();
    p->persistToXML(prog);
}
/**
 * Loads the state of a Prog object from a XML file.
 * \param fname The name of the XML file.
 * \return The loaded Prog object.
 */
Prog *Boomerang::loadFromXML(const char *fname) {
    LOG << "loading persistable state...\n";
    XMLProgParser *p = new XMLProgParser();
    return p->parse(fname);
}

/**
 * Prints the last lines of the log file.
 */
void Boomerang::logTail() { logger->tail(); }

void Boomerang::miniDebugger(UserProc *p, const char *description)
{
    QTextStream q_cout(stdout);
    QTextStream q_cin(stdin);
    q_cout << "decompiling " << p->getName() << ": " << description << "\n";
    QString stopAt;
    static std::set<Instruction *> watches;
    if (stopAt.isEmpty() || !p->getName().compare(stopAt)) {
        // This is a mini command line debugger.  Feel free to expand it.
        for (auto const &watche : watches) {
            (watche)->print(q_cout);
            q_cout << "\n";
        }
        q_cout << " <press enter to continue> \n";
        QString line;
        while (1) {
            line.clear();
            q_cin >> line;
            if (line.startsWith("print"))
                p->print(q_cout);
            else if (line.startsWith("fprint")) {
                QFile tgt("out.proc");
                if(tgt.open(QFile::WriteOnly)) {
                    QTextStream of(&tgt);
                    p->print(of);
                }
            } else if (line.startsWith("run ")) {
                QStringList parts = line.trimmed().split(" ",QString::SkipEmptyParts);
                if(parts.size()>1)
                    stopAt = parts[1];
                break;
            } else if (line.startsWith("watch ")) {
                QStringList parts = line.trimmed().split(" ",QString::SkipEmptyParts);
                if(parts.size()>1) {
                    int n = parts[1].toInt();
                    StatementList stmts;
                    p->getStatements(stmts);
                    StatementList::iterator it;
                    for (it = stmts.begin(); it != stmts.end(); it++)
                        if ((*it)->getNumber() == n) {
                            watches.insert(*it);
                            q_cout << "watching " << *it << "\n";
                        }

                }
            } else
                break;
        }
    }
}

void Boomerang::alertDecompileDebugPoint(UserProc *p, const char *description) {
    if (stopAtDebugPoints) {
        miniDebugger(p,description);
    }
    for (Watcher *elem : watchers)
        elem->alertDecompileDebugPoint(p, description);
}
//! Return TextStream to which given \a level of messages shoudl be directed
//! \param level - describes the message level TODO: describe message levels
QTextStream &Boomerang::getLogStream(int level)
{
    if(level>=LL_Error)
        return ErrStream;
    return LogStream;
}

const char *Boomerang::getVersionStr() { return VERSION; }
