/*
 * Copyright (C) 1997-2001, The University of Queensland
 * Copyright (C) 2000-2001, Sun Microsystems, Inc
 * Copyright (C) 2002-2006, Trent Waddington and Mike Van Emmerik
 *
 * See the file "LICENSE.TERMS" for information on usage and
 * redistribution of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 *
 */

/***************************************************************************/ /**
 * \file    proc.cpp
 * \brief   Implementation of the Proc hierachy (Proc, UserProc, LibProc).
 *               All aspects of a procedure, apart from the actual code in the
 *               Cfg, are stored here
 *
 * Copyright (C) 1997-2001, The University of Queensland, BT group
 * Copyright (C) 2000-2001, Sun Microsystems, Inc
 ******************************************************************************/

#include "Proc.h"

#include "boomerang/core/Boomerang.h"
#include "boomerang/core/BinaryFileFactory.h"

#include "boomerang/codegen/ICodeGenerator.h"
#include "boomerang/codegen/SyntaxNode.h"

#include "boomerang/db/Module.h"
#include "boomerang/db/Register.h"
#include "boomerang/db/RTL.h"
#include "boomerang/db/Prog.h"
#include "boomerang/db/Signature.h"
#include "boomerang/db/BasicBlock.h"
#include "boomerang/db/statements/PhiAssign.h"
#include "boomerang/db/statements/CallStatement.h"
#include "boomerang/db/statements/BranchStatement.h"
#include "boomerang/db/statements/ImplicitAssign.h"
#include "boomerang/db/statements/ImpRefStatement.h"
#include "boomerang/db/Visitor.h"

#include "boomerang/type/Constraint.h"
#include "boomerang/type/Type.h"

#include "boomerang/util/Log.h"
#include "boomerang/util/Types.h"
#include "boomerang/util/Util.h"

#include <QtCore/QDebug>
#include <QtCore/QFile>
#include <QtCore/QTextStream>

#include <sstream>
#include <algorithm> // For find()
#include <cstring>


#ifdef _WIN32
#  include <windows.h>
#  ifndef __MINGW32__
namespace dbghelp
{
#    include <dbghelp.h>
}
#  endif
#endif

typedef std::map<Instruction *, int> RefCounter;


Function::~Function()
{
}


void Function::eraseFromParent()
{
    // Replace the entry in the procedure map with -1 as a warning not to decode that address ever again
    m_parent->setLocationMap(getNativeAddress(), (Function *)-1);
    // Delete the cfg etc.
    m_parent->getFunctionList().remove(this);
    this->deleteCFG();
    delete this;  // Delete ourselves
}


Function::Function(Address uNative, Signature *sig, Module *mod)
    : m_signature(sig)
    , m_address(uNative)
    , m_firstCaller(nullptr)
    , m_parent(mod)
{
    assert(mod);
    m_prog = mod->getParent();
}


QString Function::getName() const
{
    assert(m_signature);
    return m_signature->getName();
}


void Function::setName(const QString& nam)
{
    assert(m_signature);
    m_signature->setName(nam);
}


Address Function::getNativeAddress() const
{
    return m_address;
}


void Function::setNativeAddress(Address a)
{
    m_address = a;
}


bool LibProc::isNoReturn() const
{
    return IFrontEnd::isNoReturnCallDest(getName()) || m_signature->isNoReturn();
}


bool UserProc::isNoReturn() const
{
    // undecoded procs are assumed to always return (and define everything)
    if (!this->isDecoded()) {
        return false;
    }

    BasicBlock *exitbb = m_cfg->getExitBB();

    if (exitbb == nullptr) {
        return true;
    }

    if (exitbb->getNumInEdges() == 1) {
        Instruction *s = exitbb->getInEdges()[0]->getLastStmt();

        if (!s->isCall()) {
            return false;
        }

        CallStatement *call = (CallStatement *)s;

        if (call->getDestProc() && call->getDestProc()->isNoReturn()) {
            return true;
        }
    }

    return false;
}


bool UserProc::containsAddr(Address uAddr) const
{
    BBIterator it;

    for (BasicBlock *bb = m_cfg->getFirstBB(it); bb; bb = m_cfg->getNextBB(it)) {
        if (bb->getRTLs() && (bb->getLowAddr() <= uAddr) && (bb->getHiAddr() >= uAddr)) {
            return true;
        }
    }

    return false;
}


void Function::renameParam(const char *oldName, const char *newName)
{
    m_signature->renameParam(oldName, newName);
}


void Function::matchParams(std::list<SharedExp>& /*actuals*/, UserProc& /*caller*/)
{
    // TODO: not implemented, not used, but large amount of docs :)
}


std::list<Type> *Function::getParamTypeList(const std::list<SharedExp>& /*actuals*/)
{
    // TODO: not implemented, not used
    return nullptr;
}


void UserProc::renameParam(const char *oldName, const char *newName)
{
    Function::renameParam(oldName, newName);
    // cfg->searchAndReplace(Location::param(oldName, this), Location::param(newName, this));
}


void UserProc::setParamType(const char *nam, SharedType ty)
{
    m_signature->setParamType(nam, ty);
}


void UserProc::setParamType(int idx, SharedType ty)
{
    int n = 0;

    StatementList::iterator it;

    // find n-th parameter it
    for (it = m_parameters.begin(); n != idx && it != m_parameters.end(); it++, n++) {
    }

    if (it != m_parameters.end()) {
        Assignment *a = (Assignment *)*it;
        a->setType(ty);
        // Sometimes the signature isn't up to date with the latest parameters
        m_signature->setParamType(a->getLeft(), ty);
    }
}


void UserProc::renameLocal(const char *oldName, const char *newName)
{
    SharedType     ty     = m_locals[oldName];
    SharedConstExp oldExp = expFromSymbol(oldName);

    m_locals.erase(oldName);
    SharedExp oldLoc = getSymbolFor(oldExp, ty);
    auto      newLoc = Location::local(newName, this);
    mapSymbolToRepl(oldExp, oldLoc, newLoc);
    m_locals[newName] = ty;
    m_cfg->searchAndReplace(*oldLoc, newLoc);
}


bool UserProc::searchAll(const Exp& search, std::list<SharedExp>& result)
{
    return m_cfg->searchAll(search, result);
}


void Function::printCallGraphXML(QTextStream& os, int depth, bool /*recurse*/)
{
    if (!DUMP_XML) {
        return;
    }

    m_visited = true;

    for (int i = 0; i < depth; i++) {
        os << "      ";
    }

    os << "<proc name=\"" << getName() << "\"/>\n";
}


void UserProc::printCallGraphXML(QTextStream& os, int depth, bool recurse)
{
    if (!DUMP_XML) {
        return;
    }

    bool wasVisited = m_visited;
    m_visited = true;
    int i;

    for (i = 0; i < depth; i++) {
        os << "      ";
    }

    os << "<proc name=\"" << getName() << "\">\n";

    if (recurse) {
        for (auto& elem : m_calleeList) {
            (elem)->printCallGraphXML(os, depth + 1, !wasVisited && !(elem)->isVisited());
        }
    }

    for (i = 0; i < depth; i++) {
        os << "      ";
    }

    os << "</proc>\n";
}


void Function::printDetailsXML()
{
    if (!DUMP_XML) {
        return;
    }

    QFile file(Boomerang::get()->getOutputDirectory().absoluteFilePath(getName() + "-details.xml"));

    if (!file.open(QFile::WriteOnly)) {
        qDebug() << "Can't write to file:" << file.fileName();
        return;
    }

    QTextStream out(&file);
    out << "<proc name=\"" << getName() << "\">\n";
    unsigned i;

    for (i = 0; i < m_signature->getNumParams(); i++) {
        out << "   <param name=\"" << m_signature->getParamName(i) << "\" "
            << "exp=\"" << m_signature->getParamExp(i) << "\" "
            << "type=\"" << m_signature->getParamType(i)->getCtype() << "\"\n";
    }

    for (i = 0; i < m_signature->getNumReturns(); i++) {
        out << "   <return exp=\"" << m_signature->getReturnExp(i) << "\" "
            << "type=\"" << m_signature->getReturnType(i)->getCtype() << "\"/>\n";
    }

    out << "</proc>\n";
}


void Function::removeFromParent()
{
    assert(m_parent);
    m_parent->getFunctionList().remove(this);
    m_parent->setLocationMap(m_address, nullptr);
}


void Function::setParent(Module *c)
{
    if (c == m_parent) {
        return;
    }

    removeFromParent();
    m_parent = c;
    c->getFunctionList().push_back(this);
    c->setLocationMap(m_address, this);
}


void UserProc::printDecodedXML()
{
    if (!DUMP_XML) {
        return;
    }

    QFile file(Boomerang::get()->getOutputDirectory().absoluteFilePath(getName() + "-decoded.xml"));

    if (!file.open(QFile::WriteOnly)) {
        qDebug() << "Can't write to file:" << file.fileName();
        return;
    }

    QTextStream out(&file);
    out << "<proc name=\"" << getName() << "\">\n";
    out << "    <decoded>\n";
    QString     enc;
    QTextStream os(&enc);
    print(os);
    out << enc.toHtmlEscaped();
    out << "    </decoded>\n";
    out << "</proc>\n";
}


void UserProc::printAnalysedXML()
{
    if (!DUMP_XML) {
        return;
    }

    QFile file(Boomerang::get()->getOutputDirectory().absoluteFilePath(getName() + "-analysed.xml"));

    if (!file.open(QFile::WriteOnly)) {
        qDebug() << "Can't write to file:" << file.fileName();
        return;
    }

    QTextStream out(&file);
    out << "<proc name=\"" << getName() << "\">\n";
    out << "    <analysed>\n";
    QString     enc;
    QTextStream os(&enc);
    print(os);
    out << enc.toHtmlEscaped();
    out << "    </analysed>\n";
    out << "</proc>\n";
}


void UserProc::printSSAXML()
{
    if (!DUMP_XML) {
        return;
    }

    QFile file(Boomerang::get()->getOutputDirectory().absoluteFilePath(getName() + "-ssa.xml"));

    if (!file.open(QFile::WriteOnly)) {
        qDebug() << "Can't write to file:" << file.fileName();
        return;
    }

    QTextStream out(&file);
    out << "<proc name=\"" << getName() << "\">\n";
    out << "    <ssa>\n";
    QString     enc;
    QTextStream os(&enc);
    print(os);
    out << enc.toHtmlEscaped();
    out << "    </ssa>\n";
    out << "</proc>\n";
}


void UserProc::printXML()
{
    if (!DUMP_XML) {
        return;
    }

    printDetailsXML();
    printSSAXML();
    m_prog->printCallGraphXML();
    printUseGraph();
}


void UserProc::printUseGraph()
{
    QFile file(Boomerang::get()->getOutputDirectory().absoluteFilePath(getName() + "-usegraph.dot"));

    if (!file.open(QFile::WriteOnly)) {
        qDebug() << "Can't write to file:" << file.fileName();
        return;
    }

    QTextStream out(&file);
    out << "digraph " << getName() << " {\n";
    StatementList stmts;
    getStatements(stmts);
    StatementList::iterator it;

    for (it = stmts.begin(); it != stmts.end(); it++) {
        Instruction *s = *it;

        if (s->isPhi()) {
            out << s->getNumber() << " [shape=diamond];\n";
        }

        LocationSet refs;
        s->addUsedLocs(refs);
        LocationSet::iterator rr;

        for (rr = refs.begin(); rr != refs.end(); rr++) {
            if (((SharedExp) * rr)->isSubscript()) {
                auto r = (*rr)->access<RefExp>();

                if (r->getDef()) {
                    out << r->getDef()->getNumber() << " -> " << s->getNumber() << ";\n";
                }
            }
        }
    }

    out << "}\n";
}


Function *Function::getFirstCaller()
{
    if ((m_firstCaller == nullptr) && (m_firstCallerAddr != Address::INVALID)) {
        m_firstCaller     = m_prog->findProc(m_firstCallerAddr);
        m_firstCallerAddr = Address::INVALID;
    }

    return m_firstCaller;
}


LibProc::LibProc(Module *mod, const QString& name, Address uNative)
    : Function(uNative, nullptr, mod)
{
    m_signature = mod->getLibSignature(name);
}


SharedExp LibProc::getProven(SharedExp left)
{
    // Just use the signature information (all we have, after all)
    return m_signature->getProven(left);
}


bool LibProc::isPreserved(SharedExp e)
{
    return m_signature->isPreserved(e);
}


UserProc::UserProc(Module *mod, const QString& name, Address uNative)
    : // Not quite ready for the below fix:
      // Proc(prog, uNative, prog->getDefaultSignature(name.c_str())),
    Function(uNative, new Signature(name), mod)
    , m_cfg(new Cfg())
    , m_status(PROC_UNDECODED)
    , m_cycleGroup(nullptr)
    , theReturnStatement(nullptr)
    , DFGcount(0)
{
    m_cfg->setProc(this); // Initialise cfg.myProc
    m_localTable.setProc(this);
}


UserProc::~UserProc()
{
    deleteCFG();
}


void UserProc::deleteCFG()
{
    delete m_cfg;
    m_cfg = nullptr;
}


class lessEvaluate : public std::binary_function<SyntaxNode *, SyntaxNode *, bool>
{
public:
    bool operator()(const SyntaxNode *x, const SyntaxNode *y) const
    {
        return ((SyntaxNode *)x)->getScore() > ((SyntaxNode *)y)->getScore();
    }
};


SyntaxNode *UserProc::getAST() const
{
    int             numBBs = 0;
    BlockSyntaxNode *init  = new BlockSyntaxNode();
    BBIterator           it;

    for (BasicBlock *bb = m_cfg->getFirstBB(it); bb; bb = m_cfg->getNextBB(it)) {
        BlockSyntaxNode *b = new BlockSyntaxNode();
        b->setBB(bb);
        init->addStatement(b);
        numBBs++;
    }

    // perform a best first search for the nicest AST
    std::priority_queue<SyntaxNode *, std::vector<SyntaxNode *>, lessEvaluate> ASTs;
    ASTs.push(init);

    SyntaxNode *best      = init;
    int        best_score = init->getScore();
    int        count      = 0;

    while (!ASTs.empty()) {
        if (best_score < numBBs * 2) {
            LOG << "exit early: " << best_score << "\n";
            break;
        }

        SyntaxNode *top = ASTs.top();
        ASTs.pop();
        int score = top->evaluate(top);

        printAST(top); // debug

        if (score < best_score) {
            if (best && (top != best)) {
                delete best;
            }

            best       = top;
            best_score = score;
        }

        count++;

        if (count > 100) {
            break;
        }

        // add successors
        std::vector<SyntaxNode *> successors;
        top->addSuccessors(top, successors);

        for (auto& successor : successors) {
            // successors[i]->addToScore(top->getScore());    // uncomment for A*
            successor->addToScore(successor->getDepth()); // or this
            ASTs.push(successor);
        }

        if (top != best) {
            delete top;
        }
    }

    // clean up memory
    while (!ASTs.empty()) {
        SyntaxNode *top = ASTs.top();
        ASTs.pop();

        if (top != best) {
            delete top;
        }
    }

    return best;
}


void UserProc::printAST(SyntaxNode *a) const
{
    static int count = 1;
    char       s[1024];

    if (a == nullptr) {
        a = getAST();
    }

    sprintf(s, "ast%i-%s.dot", count++, qPrintable(getName()));
    QFile tgt(s);

    if (!tgt.open(QFile::WriteOnly)) {
        return; // TODO: report error ?
    }

    QTextStream of(&tgt);
    of << "digraph " << getName() << " {" << '\n';
    of << "     label=\"score: " << a->evaluate(a) << "\";" << '\n';
    a->printAST(a, of);
    of << "}" << '\n';
}


void UserProc::setDecoded()
{
    setStatus(PROC_DECODED);
    printDecodedXML();
}


void UserProc::unDecode()
{
    m_cfg->clear();
    setStatus(PROC_UNDECODED);
}


BasicBlock *UserProc::getEntryBB()
{
    return m_cfg->getEntryBB();
}


void UserProc::setEntryBB()
{
    std::list<BasicBlock *>::iterator bbit;
    BasicBlock *pBB = m_cfg->getFirstBB(bbit); // Get an iterator to the first BB

    // Usually, but not always, this will be the first BB, or at least in the first few
    while (pBB && m_address != pBB->getLowAddr()) {
        pBB = m_cfg->getNextBB(bbit);
    }

    m_cfg->setEntryBB(pBB);
}


void UserProc::addCallee(Function *callee)
{
    // is it already in? (this is much slower than using a set)
    std::list<Function *>::iterator cc;

    for (cc = m_calleeList.begin(); cc != m_calleeList.end(); cc++) {
        if (*cc == callee) {
            return; // it's already in
        }
    }

    m_calleeList.push_back(callee);
}


void UserProc::generateCode(ICodeGenerator *hll)
{
    assert(m_cfg);
    assert(getEntryBB());

    m_cfg->structure();
    removeUnusedLocals();

    // Note: don't try to remove unused statements here; that requires the
    // RefExps, which are all gone now (transformed out of SSA form)!

    if (VERBOSE || Boomerang::get()->printRtl) {
        LOG << *this;
    }

    hll->addProcStart(this);

    // Local variables; print everything in the locals map
    std::map<QString, SharedType>::iterator last = m_locals.end();

    if (!m_locals.empty()) {
        last--;
    }

    for (std::map<QString, SharedType>::iterator it = m_locals.begin(); it != m_locals.end(); it++) {
        SharedType locType = it->second;

        if ((locType == nullptr) || locType->isVoid()) {
            locType = IntegerType::get(STD_SIZE);
        }

        hll->addLocal(it->first, locType, it == last);
    }

    if (Boomerang::get()->noDecompile && (getName() == "main")) {
        StatementList args, results;

        if (m_prog->getFrontEndId() == Platform::PENTIUM) {
            hll->addCallStatement(1, nullptr, "PENTIUMSETUP", args, &results);
        }
        else if (m_prog->getFrontEndId() == Platform::SPARC) {
            hll->addCallStatement(1, nullptr, "SPARCSETUP", args, &results);
        }
    }

    std::list<BasicBlock *> followSet, gotoSet;
    getEntryBB()->generateCode(hll, 1, nullptr, followSet, gotoSet, this);

    hll->addProcEnd();

    if (!Boomerang::get()->noRemoveLabels) {
        m_cfg->removeUnneededLabels(hll);
    }

    setStatus(PROC_CODE_GENERATED);
}


void UserProc::print(QTextStream& out, bool html) const
{
    QString     tgt1;
    QString     tgt2;
    QString     tgt3;
    QTextStream ost1(&tgt1);
    QTextStream ost2(&tgt2);
    QTextStream ost3(&tgt3);

    printParams(ost1, html);
    dumpLocals(ost1, html);
    m_procUseCollector.print(ost2, html);
    m_cfg->print(ost3, html);

    m_signature->print(out, html);
    out << "\n";

    if (html) {
        out << "<br>";
    }

    out << "in module " << m_parent->getName() << "\n";

    if (html) {
        out << "<br>";
    }

    out << tgt1;
    printSymbolMap(out, html);

    if (html) {
        out << "<br>";
    }

    out << "live variables: " << tgt2 << "\n";

    if (html) {
        out << "<br>";
    }

    out << "end live variables\n" << tgt3 << "\n";
}


void UserProc::setStatus(ProcStatus s)
{
    m_status = s;
    Boomerang::get()->alertProcStatusChange(this);
}


void UserProc::printParams(QTextStream& out, bool html /*= false*/) const
{
    if (html) {
        out << "<br>";
    }

    out << "parameters: ";
    bool first = true;

    for (auto const& elem : m_parameters) {
        if (first) {
            first = false;
        }
        else {
            out << ", ";
        }

        out << ((Assignment *)elem)->getType() << " " << ((Assignment *)elem)->getLeft();
    }

    out << "\n";

    if (html) {
        out << "<br>";
    }

    out << "end parameters\n";
}


char *UserProc::prints() const
{
    QString     tgt;
    QTextStream ost(&tgt);

    print(ost);
    strncpy(debug_buffer, qPrintable(tgt), DEBUG_BUFSIZE - 1);
    debug_buffer[DEBUG_BUFSIZE - 1] = '\0';
    return debug_buffer;
}


void UserProc::dump() const
{
    QTextStream q_cerr(stderr);

    print(q_cerr);
}


void UserProc::printDFG() const
{
    QString fname = QString("%1%2-%3-dfg.dot")
        .arg(Boomerang::get()->getOutputDirectory().absolutePath())
        .arg(getName())
        .arg(DFGcount);

    DFGcount++;
    LOG_MSG("Outputing DFG to '%1'", fname);
    QFile file(fname);

    if (!file.open(QFile::WriteOnly)) {
        LOG_WARN("Can't open DFG '%1'", fname);
        return;
    }

    QTextStream out(&file);
    out << "digraph " << getName() << " {\n";
    StatementList stmts;
    getStatements(stmts);

    for (Instruction *s : stmts) {
        if (s->isPhi()) {
            out << s->getNumber() << " [shape=\"triangle\"];\n";
        }

        if (s->isCall()) {
            out << s->getNumber() << " [shape=\"box\"];\n";
        }

        if (s->isBranch()) {
            out << s->getNumber() << " [shape=\"diamond\"];\n";
        }

        LocationSet refs;
        s->addUsedLocs(refs);
        LocationSet::iterator rr;

        for (rr = refs.begin(); rr != refs.end(); rr++) {
            auto r = std::dynamic_pointer_cast<RefExp>(*rr);

            if (r) {
                if (r->getDef()) {
                    out << r->getDef()->getNumber();
                }
                else {
                    out << "input";
                }

                out << " -> ";

                if (s->isReturn()) {
                    out << "output";
                }
                else {
                    out << s->getNumber();
                }

                out << ";\n";
            }
        }
    }

    out << "}\n";
}


void UserProc::initStatements()
{
    BBIterator it;

    BasicBlock::rtlit       rit;
    StatementList::iterator sit;

    for (BasicBlock *bb = m_cfg->getFirstBB(it); bb; bb = m_cfg->getNextBB(it)) {
        for (Instruction *s = bb->getFirstStmt(rit, sit); s; s = bb->getNextStmt(rit, sit)) {
            s->setProc(this);
            s->setBB(bb);
            CallStatement *call = dynamic_cast<CallStatement *>(s);

            if (call) {
                call->setSigArguments();

                if (call->getDestProc() && call->getDestProc()->isNoReturn() && (bb->getNumOutEdges() == 1)) {
                    BasicBlock *out = bb->getOutEdge(0);

                    if ((out != m_cfg->getExitBB()) || (m_cfg->getExitBB()->getNumInEdges() != 1)) {
                        out->deleteInEdge(bb);
                        bb->clearOutEdges();
                    }
                }
            }
        }
    }
}


void UserProc::numberStatements()
{
    BBIterator it;

    BasicBlock::rtlit       rit;
    StatementList::iterator sit;

    for (BasicBlock *bb = m_cfg->getFirstBB(it); bb; bb = m_cfg->getNextBB(it)) {
        for (Instruction *s = bb->getFirstStmt(rit, sit); s; s = bb->getNextStmt(rit, sit)) {
            if (!s->isImplicit() &&      // Don't renumber implicits (remain number 0)
                (s->getNumber() == 0)) { // Don't renumber existing (or waste numbers)
                s->setNumber(++m_stmtNumber);
            }
        }
    }
}


void UserProc::getStatements(StatementList& stmts) const
{
    BBCIterator it;

    for (const BasicBlock *bb = m_cfg->getFirstBB(it); bb; bb = m_cfg->getNextBB(it)) {
        bb->getStatements(stmts);
    }

    for (Instruction *s : stmts) {
        if (s->getProc() == nullptr) {
            s->setProc(const_cast<UserProc *>(this));
        }
    }
}


void UserProc::removeStatement(Instruction *stmt)
{
    // remove anything proven about this statement
    for (std::map<SharedExp, SharedExp, lessExpStar>::iterator it = m_provenTrue.begin(); it != m_provenTrue.end(); ) {
        LocationSet refs;
        it->second->addUsedLocs(refs);
        it->first->addUsedLocs(refs); // Could be say m[esp{99} - 4] on LHS and we are deleting stmt 99
        LocationSet::iterator rr;
        bool usesIt = false;

        for (rr = refs.begin(); rr != refs.end(); rr++) {
            SharedExp r = *rr;

            if (r->isSubscript() && (r->access<RefExp>()->getDef() == stmt)) {
                usesIt = true;
                break;
            }
        }

        if (usesIt) {
            if (VERBOSE) {
                LOG << "removing proven true exp " << it->first << " = " << it->second
                    << " that uses statement being removed.\n";
            }

            m_provenTrue.erase(it++);
            // it = provenTrue.begin();
            continue;
        }

        ++it; // it is incremented with the erase, or here
    }

    // remove from BB/RTL
    BasicBlock       *bb   = stmt->getBB(); // Get our enclosing BB
    std::list<RTL *> *rtls = bb->getRTLs();

    for (RTL *rit : *rtls) {
        for (RTL::iterator it = rit->begin(); it != rit->end(); it++) {
            if (*it == stmt) {
                rit->erase(it);
                return;
            }
        }
    }
}


void UserProc::insertAssignAfter(Instruction *s, SharedExp left, SharedExp right)
{
    std::list<Instruction *>::iterator it;
    std::list<Instruction *>           *stmts;

    if (s == nullptr) {
        // This means right is supposed to be a parameter. We can insert the assignment at the start of the entryBB
        BasicBlock       *entryBB = m_cfg->getEntryBB();
        std::list<RTL *> *rtls    = entryBB->getRTLs();
        assert(rtls->size()); // Entry BB should have at least 1 RTL
        stmts = rtls->front();
        it    = stmts->begin();
    }
    else {
        // An ordinary definition; put the assignment at the end of s's BB
        BasicBlock       *bb   = s->getBB(); // Get the enclosing BB for s
        std::list<RTL *> *rtls = bb->getRTLs();
        assert(rtls->size());                // If s is defined here, there should be
        // at least 1 RTL
        stmts = rtls->back();
        it    = stmts->end(); // Insert before the end
    }

    Assign *as = new Assign(left, right);
    as->setProc(this);
    stmts->insert(it, as);
}


void UserProc::insertStatementAfter(Instruction *s, Instruction *a)
{
    BBIterator bb;

    for (bb = m_cfg->begin(); bb != m_cfg->end(); bb++) {
        std::list<RTL *> *rtls = (*bb)->getRTLs();

        if (rtls == nullptr) {
            continue; // e.g. *bb is (as yet) invalid
        }

        for (RTL *rr : *rtls) {
            std::list<Instruction *>::iterator ss;

            for (ss = rr->begin(); ss != rr->end(); ss++) {
                if (*ss == s) {
                    ss++; // This is the point to insert before
                    rr->insert(ss, a);
                    return;
                }
            }
        }
    }

    assert(false); // Should have found this statement in this BB
}


std::shared_ptr<ProcSet> UserProc::decompile(ProcList *path, int& indent)
{
    /* Cycle detection logic:
    * *********************
    * cycleGrp is an initially null pointer to a set of procedures, representing the procedures involved in the current
    * recursion group, if any. These procedures have to be analysed together as a group, after individual pre-group
    * analysis.
    * child is a set of procedures, cleared at the top of decompile(), representing the cycles associated with the
    * current procedure and all of its children. If this is empty, the current procedure is not involved in recursion,
    * and can be decompiled up to and including removing unused statements.
    * path is an initially empty list of procedures, representing the call path from the current entry point to the
    * current procedure, inclusive.
    * If (after all children have been processed: important!) the first element in path and also cycleGrp is the current
    * procedure, we have the maximal set of distinct cycles, so we can do the recursion group analysis and return an empty
    * set. At the end of the recursion group analysis, the whole group is complete, ready for the global analyses.
    * cycleSet decompile(ProcList path)        // path initially empty
    *      child = new ProcSet
    *      append this proc to path
    *      for each child c called by this proc
    *              if c has already been visited but not finished
    *                      // have new cycle
    *                      if c is in path
    *                        // this is a completely new cycle
    *                        insert every proc from c to the end of path into child
    *                      else
    *                        // this is a new branch of an existing cycle
    *                        child = c->cycleGrp
    *                        find first element f of path that is in cycleGrp
    *                        insert every proc after f to the end of path into child
    *                      for each element e of child
    *            insert e->cycleGrp into child
    *                        e->cycleGrp = child
    *              else
    *                      // no new cycle
    *                      tmp = c->decompile(path)
    *                      child = union(child, tmp)
    *                      set return statement in call to that of c
    *      if (child empty)
    *              earlyDecompile()
    *              child = middleDecompile()
    *              removeUnusedStatments()            // Not involved in recursion
    *      else
    *              // Is involved in recursion
    *              find first element f in path that is also in cycleGrp
    *              if (f == this)          // The big test: have we got the complete strongly connected component?
    *                      recursionGroupAnalysis()        // Yes, we have
    *                      child = new ProcSet            // Don't add these processed cycles to the parent
    *      remove last element (= this) from path
    *      return child
    */

    Boomerang::get()->alertConsidering(path->empty() ? nullptr : path->back(), this);
    Util::alignStream(LOG_STREAM_OLD(), ++indent) << (m_status >= PROC_VISITED ? "re" : "") << "considering "
                                        << getName() << "\n";
    LOG_VERBOSE("Begin decompile (%1)", getName());

    // Prevent infinite loops when there are cycles in the call graph (should never happen now)
    if (m_status >= PROC_FINAL) {
        LOG_ERROR("Proc %1 already has status PROC_FINAL", getName());
        return nullptr; // Already decompiled
    }

    if (m_status < PROC_DECODED) {
        // Can happen e.g. if a callee is visible only after analysing a switch statement
        m_prog->reDecode(this); // Actually decoding for the first time, not REdecoding
    }

    if (m_status < PROC_VISITED) {
        setStatus(PROC_VISITED); // We have at least visited this proc "on the way down"
    }

    std::shared_ptr<ProcSet> child = std::make_shared<ProcSet>();
    path->push_back(this); // Append this proc to path

    /*    *    *    *    *    *    *    *    *    *    *    *
    *                                            *
    *    R e c u r s e   t o   c h i l d r e n    *
    *                                            *
    *    *    *    *    *    *    *    *    *    *    *    */

    if (!Boomerang::get()->noDecodeChildren) {
        // Recurse to children first, to perform a depth first search
        BBIterator it;

        // Look at each call, to do the DFS
        for (BasicBlock *bb = m_cfg->getFirstBB(it); bb; bb = m_cfg->getNextBB(it)) {
            if (bb->getType() != BBType::Call) {
                continue;
            }

            // The call Statement will be in the last RTL in this BB
            CallStatement *call = (CallStatement *)bb->getRTLs()->back()->getHlStmt();

            if (!call->isCall()) {
                LOG << "bb at " << bb->getLowAddr() << " is a CALL but last stmt is not a call: " << call << "\n";
            }

            assert(call->isCall());
            UserProc *c = dynamic_cast<UserProc *>(call->getDestProc());

            if (c == nullptr) { // not an user proc, or missing dest
                continue;
            }

            if (c->m_status == PROC_FINAL) {
                // Already decompiled, but the return statement still needs to be set for this call
                call->setCalleeReturn(c->getTheReturnStatement());
                continue;
            }

            // if c has already been visited but not done (apart from global analyses, i.e. we have a new cycle)
            if ((c->m_status >= PROC_VISITED) && (c->m_status <= PROC_EARLYDONE)) {
                // if c is in path
                ProcList::iterator pi;
                bool               inPath = false;

                for (pi = path->begin(); pi != path->end(); ++pi) {
                    if (*pi == c) {
                        inPath = true;
                        break;
                    }
                }

                if (inPath) {
                    // This is a completely new cycle
                    // Insert every proc from c to the end of path into child
                    do {
                        child->insert(*pi);
                        ++pi;
                    } while (pi != path->end());
                }
                else {
                    // This is new branch of an existing cycle
                    child = c->m_cycleGroup;
                    // Find first element f of path that is in c->cycleGrp
                    ProcList::iterator _pi;
                    Function           *f = nullptr;

                    for (_pi = path->begin(); _pi != path->end(); ++_pi) {
                        if (c->m_cycleGroup->find(*_pi) != c->m_cycleGroup->end()) {
                            f = *_pi;
                            break;
                        }
                    }

                    if (!f) {
                        assert(false);
                    }

                    // Insert every proc after f to the end of path into child
                    // There must be at least one element in the list (this proc), so the ++pi should be safe
                    while (++_pi != path->end()) {
                        child->insert(*_pi);
                    }
                }

                // point cycleGrp for each element of child to child, unioning in each element's cycleGrp
                ProcSet entries;

                for (auto cc : *child) {
                    if (cc->m_cycleGroup) {
                        entries.insert(cc->m_cycleGroup->begin(), cc->m_cycleGroup->end());
                    }
                }

                child->insert(entries.begin(), entries.end());

                for (UserProc *proc : *child) {
                    proc->m_cycleGroup = child;
                }

                setStatus(PROC_INCYCLE);
            }
            else {
                // No new cycle
                LOG_VERBOSE("Visiting on the way down child %1 from %2", c->getName(), getName());

                c->promoteSignature();
                std::shared_ptr<ProcSet> tmp = c->decompile(path, indent);
                child->insert(tmp->begin(), tmp->end());
                // Child has at least done middleDecompile(), possibly more
                call->setCalleeReturn(c->getTheReturnStatement());

                if (!tmp->empty()) {
                    setStatus(PROC_INCYCLE);
                }
            }
        }
    }

    // if child is empty, i.e. no child involved in recursion
    if (child->empty()) {
        Boomerang::get()->alertDecompiling(this);
        Util::alignStream(LOG_STREAM_OLD(LogLevel::Default), indent) << "decompiling " << getName() << "\n";
        initialiseDecompile(); // Sort the CFG, number statements, etc
        earlyDecompile();
        child = middleDecompile(path, indent);

        // If there is a switch statement, middleDecompile could contribute some cycles. If so, we need to test for
        // the recursion logic again
        if (!child->empty()) {
            // We've just come back out of decompile(), so we've lost the current proc from the path.
            path->push_back(this);
        }
    }

    if (child->empty()) {
        remUnusedStmtEtc(); // Do the whole works
        setStatus(PROC_FINAL);
        Boomerang::get()->alertEndDecompile(this);
    }
    else {
        // this proc's children, and hence this proc, is/are involved in recursion
        // find first element f in path that is also in cycleGrp
        ProcList::iterator f;

        for (f = path->begin(); f != path->end(); ++f) {
            if (m_cycleGroup->find(*f) != m_cycleGroup->end()) {
                break;
            }
        }

        // The big test: have we found all the strongly connected components (in the call graph)?
        if (*f == this) {
            // Yes, process these procs as a group
            recursionGroupAnalysis(path, indent); // Includes remUnusedStmtEtc on all procs in cycleGrp
            setStatus(PROC_FINAL);
            Boomerang::get()->alertEndDecompile(this);
            child->clear(); // delete child;
            child = std::make_shared<ProcSet>();
        }
    }

    // Remove last element (= this) from path
    // The if should not be neccesary, but nestedswitch needs it
    if (!path->empty()) {
        assert(std::find(path->begin(), path->end(), this) != path->end());

        if (path->back() != this) {
            qDebug() << "Last UserProc in UserProc::decompile/path is not this!";
        }

        path->remove(this);
    }
    else {
        LOG_WARN("Empty path when trying to remove last proc");
    }

    --indent;
    LOG_VERBOSE("End decompile(%1)", getName());
    return child;
}


void UserProc::debugPrintAll(const char *step_name)
{
    if (VERBOSE) {
        LOG_SEPARATE_OLD(getName()) << "--- debug print " << step_name << " for " << getName() << " ---\n" << *this
                                << "=== end debug print " << step_name << " for " << getName() << " ===\n\n";
    }
}


/*    *    *    *    *    *    *    *    *    *    *    *
*                                            *
*        D e c o m p i l e   p r o p e r        *
*            ( i n i t i a l )                *
*                                            *
*    *    *    *    *    *    *    *    *    *    *    */
void UserProc::initialiseDecompile()
{
    Boomerang::get()->alertStartDecompile(this);

    Boomerang::get()->alertDecompileDebugPoint(this, "before initialise");

    if (VERBOSE) {
        LOG << "initialise decompile for " << getName() << "\n";
    }

    // Sort by address, so printouts make sense
    m_cfg->sortByAddress();

    // Initialise statements
    initStatements();

    debugPrintAll("before SSA ");

    // Compute dominance frontier
    m_df.dominators(m_cfg);

    // Number the statements
    m_stmtNumber = 0;
    numberStatements();

    printXML();

    if (Boomerang::get()->noDecompile) {
        LOG_MSG("Not decompiling.");
        setStatus(PROC_FINAL); // ??!
        return;
    }

    debugPrintAll("after decoding");
    Boomerang::get()->alertDecompileDebugPoint(this, "after initialise");
}


void UserProc::earlyDecompile()
{
    if (m_status >= PROC_EARLYDONE) {
        return;
    }

    Boomerang::get()->alertDecompileDebugPoint(this, "Before Early");
    LOG_VERBOSE("Early decompile for %1", getName());

    // Update the defines in the calls. Will redo if involved in recursion
    updateCallDefines();

    // This is useful for obj-c
    replaceSimpleGlobalConstants();

    // First placement of phi functions, renaming, and initial propagation. This is mostly for the stack pointer
    // maxDepth = findMaxDepth() + 1;
    // if (Boomerang::get()->maxMemDepth < maxDepth)
    //    maxDepth = Boomerang::get()->maxMemDepth;
    // TODO: Check if this makes sense. It seems to me that we only want to do one pass of propagation here, since
    // the status == check had been knobbled below. Hopefully, one call to placing phi functions etc will be
    // equivalent to depth 0 in the old scheme
    LOG_VERBOSE("Placing phi functions 1st pass");

    // Place the phi functions
    m_df.placePhiFunctions(this);

    LOG_VERBOSE("Numbering phi statements 1st pass");
    numberStatements(); // Number them

    // Rename variables
    LOG_VERBOSE("Renaming block variables 1st pass");
    doRenameBlockVars(1, true);
    debugPrintAll("after rename (1)");

    bool convert;
    propagateStatements(convert, 1);

    debugPrintAll("After propagation (1)");

    Boomerang::get()->alertDecompileDebugPoint(this, "After Early");
}


std::shared_ptr<ProcSet> UserProc::middleDecompile(ProcList *path, int indent)
{
    Boomerang::get()->alertDecompileDebugPoint(this, "Before Middle");

    // The call bypass logic should be staged as well. For example, consider m[r1{11}]{11} where 11 is a call.
    // The first stage bypass yields m[r1{2}]{11}, which needs another round of propagation to yield m[r1{-}-32]{11}
    // (which can safely be processed at depth 1).
    // Except that this is now inherent in the visitor nature of the latest algorithm.
    fixCallAndPhiRefs(); // Bypass children that are finalised (if any)
    bool convert;

    if (m_status != PROC_INCYCLE) { // FIXME: need this test?
        propagateStatements(convert, 2);
    }

    debugPrintAll("After call and phi bypass (1)");

    // This part used to be calle middleDecompile():

    findSpPreservation();
    // Oops - the idea of splitting the sp from the rest of the preservations was to allow correct naming of locals
    // so you are alias conservative. But of course some locals are ebp (etc) based, and so these will never be correct
    // until all the registers have preservation analysis done. So I may as well do them all together here.
    findPreserveds();
    fixCallAndPhiRefs(); // Propagate and bypass sp

    debugPrintAll("After preservation, bypass and propagation");

    // Oh, no, we keep doing preservations till almost the end...
    // setStatus(PROC_PRESERVEDS);        // Preservation done

    if (!Boomerang::get()->noPromote) {
        // We want functions other than main to be promoted. Needed before mapExpressionsToLocals
        promoteSignature();
    }

    // The problem with doing locals too early is that the symbol map ends up with some {-} and some {0}
    // Also, once named as a local, it is tempting to propagate the memory location, but that might be unsafe if the
    // address is taken. But see mapLocalsAndParams just a page below.
    // mapExpressionsToLocals();

    // Update the arguments for calls (mainly for the non recursion affected calls)
    // We have only done limited propagation and collecting to this point. Need e.g. to put m[esp-K]
    // into the collectors of calls, so when a stack parameter is created, it will be correctly localised
    // Note that we'd like to limit propagation before this point, because we have not yet created any arguments, so
    // it is possible to get "excessive propagation" to parameters. In fact, because uses vary so much throughout a
    // program, it may end up better not limiting propagation until very late in the decompilation, and undoing some
    // propagation just before removing unused statements. Or even later, if that is possible.
    // For now, we create the initial arguments here (relatively early), and live with the fact that some apparently
    // distinct memof argument expressions (e.g. m[eax{30}] and m[esp{40}-4]) will turn out to be duplicates, and so
    // the duplicates must be eliminated.
    bool change = m_df.placePhiFunctions(this);

    if (change) {
        numberStatements(); // Number the new statements
    }

    doRenameBlockVars(2);
    propagateStatements(convert, 2); // Otherwise sometimes sp is not fully propagated
    // Map locals and temporary parameters as symbols, so that they can be propagated later
    //    mapLocalsAndParams();                // FIXME: unsure where this belongs
    updateArguments();
    reverseStrengthReduction();
    // processTypes();

    // Repeat until no change
    int pass;

    for (pass = 3; pass <= 12; ++pass) {
        // Redo the renaming process to take into account the arguments
        if (VERBOSE) {
            LOG << "renaming block variables (2) pass " << pass << "\n";
        }

        // Rename variables
        change = m_df.placePhiFunctions(this);

        if (change) {
            numberStatements();                   // Number the new statements
        }

        change |= doRenameBlockVars(pass, false); // E.g. for new arguments

        // Seed the return statement with reaching definitions
        // FIXME: does this have to be in this loop?
        if (theReturnStatement) {
            theReturnStatement->updateModifieds(); // Everything including new arguments reaching the exit
            theReturnStatement->updateReturns();
        }

        printXML();

        // Print if requested
        if (VERBOSE) { // was if debugPrintSSA
            LOG_SEPARATE_OLD(getName()) << "--- debug print SSA for " << getName() << " pass " << pass
                                    << " (no propagations) ---\n" << *this << "=== end debug print SSA for "
                                    << getName() << " pass " << pass << " (no propagations) ===\n\n";
        }

        if (!Boomerang::get()->dotFile.isEmpty()) { // Require -gd now (though doesn't listen to file name)
            printDFG();
        }

        Boomerang::get()->alertDecompileSSADepth(this, pass); // FIXME: need depth -> pass in GUI code

// (* Was: mapping expressions to Parameters as we go *)

        // FIXME: Check if this is needed any more. At least fib seems to need it at present.
        if (!Boomerang::get()->noChangeSignatures) {
            // addNewReturns(depth);
            for (int i = 0; i < 3; i++) { // FIXME: should be iterate until no change
                if (VERBOSE) {
                    LOG << "### update returns loop iteration " << i << " ###\n";
                }

                if (m_status != PROC_INCYCLE) {
                    doRenameBlockVars(pass, true);
                }

                findPreserveds();
                updateCallDefines(); // Returns have uses which affect call defines (if childless)
                fixCallAndPhiRefs();
                findPreserveds();    // Preserveds subtract from returns
            }

            printXML();

            if (VERBOSE) {
                LOG_SEPARATE_OLD(getName()) << "--- debug print SSA for " << getName() << " at pass " << pass
                                        << " (after updating returns) ---\n" << *this << "=== end debug print SSA for "
                                        << getName() << " at pass " << pass << " ===\n\n";
            }
        }

        printXML();

        // Print if requested
        if (VERBOSE) { // was if debugPrintSSA
            LOG_SEPARATE_OLD(getName()) << "--- debug print SSA for " << getName() << " at pass " << pass
                                    << " (after trimming return set) ---\n" << *this << "=== end debug print SSA for "
                                    << getName() << " at pass " << pass << " ===\n\n";
        }

        Boomerang::get()->alertDecompileBeforePropagate(this, pass);
        Boomerang::get()->alertDecompileDebugPoint(this, "before propagating statements");

        // Propagate
        bool _convert; // True when indirect call converted to direct

        do {
            _convert = false;
            LOG_VERBOSE("Propagating at pass %1", pass);
            change |= propagateStatements(_convert, pass);
            change |= doRenameBlockVars(pass, true);

            // If you have an indirect to direct call conversion, some propagations that were blocked by
            // the indirect call might now succeed, and may be needed to prevent alias problems
            // FIXME: I think that the below, and even the convert parameter to propagateStatements(), is no longer
            // needed - MVE
            if (_convert) {
                if (VERBOSE) {
                    LOG << "\nabout to restart propagations and dataflow at pass " << pass
                        << " due to conversion of indirect to direct call(s)\n\n";
                }

                m_df.setRenameLocalsParams(false);
                change |= doRenameBlockVars(0, true); // Initial dataflow level 0
                LOG_SEPARATE_OLD(getName()) << "\nafter rename (2) of " << getName() << ":\n" << *this
                                        << "\ndone after rename (2) of " << getName() << ":\n\n";
            }
        } while (_convert);

        printXML();

        if (VERBOSE) {
            LOG_SEPARATE_OLD(getName()) << "--- after propagate for " << getName() << " at pass " << pass << " ---\n"
                                    << *this << "=== end propagate for " << getName() << " at pass " << pass
                                    << " ===\n\n";
        }

        Boomerang::get()->alertDecompileAfterPropagate(this, pass);
        Boomerang::get()->alertDecompileDebugPoint(this, "after propagating statements");

        // this is just to make it readable, do NOT rely on these statements being removed
        removeSpAssignsIfPossible();
        // The problem with removing %flags and %CF is that %CF is a subset of %flags
        // removeMatchingAssignsIfPossible(Terminal::get(opFlags));
        // removeMatchingAssignsIfPossible(Terminal::get(opCF));
        removeMatchingAssignsIfPossible(Unary::get(opTemp, Terminal::get(opWildStrConst)));
        removeMatchingAssignsIfPossible(Terminal::get(opPC));

        // processTypes();

        if (!change) {
            break; // Until no change
        }
    }

    // At this point, there will be some memofs that have still not been renamed. They have been prevented from
    // getting renamed so that they didn't get renamed incorrectly (usually as {-}), when propagation and/or bypassing
    // may have ended up changing the address expression. There is now no chance that this will happen, so we need
    // to rename the existing memofs. Note that this can still link uses to definitions, e.g.
    // 50 r26 := phi(...)
    // 51 m[r26{50}] := 99;
    //    ... := m[r26{50}]{should be 51}

    if (VERBOSE) {
        LOG << "### allowing SSA renaming of all memof expressions ###\n";
    }

    m_df.setRenameLocalsParams(true);

    // Now we need another pass to inert phis for the memofs, rename them and propagate them
    ++pass;

    if (VERBOSE) {
        LOG << "setting phis, renaming block variables after memofs renamable pass " << pass << "\n";
    }

    change = m_df.placePhiFunctions(this);

    if (change) {
        numberStatements();         // Number the new statements
    }

    doRenameBlockVars(pass, false); // MVE: do we want this parameter false or not?
    debugPrintAll("after setting phis for memofs, renaming them");
    propagateStatements(convert, pass);
    // Now that memofs are renamed, the bypassing for memofs can work
    fixCallAndPhiRefs(); // Bypass children that are finalised (if any)

    if (!Boomerang::get()->noParameterNames) {
        // ? Crazy time to do this... haven't even done "final" parameters as yet
        // mapExpressionsToParameters();
    }

    // Check for indirect jumps or calls not already removed by propagation of constants
    if (m_cfg->decodeIndirectJmp(this)) {
        // There was at least one indirect jump or call found and decoded. That means that most of what has been done
        // to this function so far is invalid. So redo everything. Very expensive!!
        // Code pointed to by the switch table entries has merely had FrontEnd::processFragment() called on it
        LOG << "=== about to restart decompilation of " << getName()
            << " because indirect jumps or calls have been analysed\n\n";
        Boomerang::get()->alertDecompileDebugPoint(
            this, "before restarting decompilation because indirect jumps or calls have been analysed");

        // First copy any new indirect jumps or calls that were decoded this time around. Just copy them all, the map
        // will prevent duplicates
        processDecodedICTs();
        // Now, decode from scratch
        theReturnStatement = nullptr;
        m_cfg->clear();
        m_prog->reDecode(this);
        m_df.setRenameLocalsParams(false);                      // Start again with memofs
        setStatus(PROC_VISITED);                                // Back to only visited progress
        path->erase(--path->end());                             // Remove self from path
        --indent;                                               // Because this is not recursion
        std::shared_ptr<ProcSet> ret = decompile(path, indent); // Restart decompiling this proc
        ++indent;                                               // Restore indent
        path->push_back(this);                                  // Restore self to path
        // It is important to keep the result of this call for the recursion analysis
        return ret;
    }

    findPreserveds();

    // Used to be later...
    if (!Boomerang::get()->noParameterNames) {
        // findPreserveds();        // FIXME: is this necessary here?
        // fixCallBypass();    // FIXME: surely this is not necessary now?
        // trimParameters();    // FIXME: surely there aren't any parameters to trim yet?
        debugPrintAll("after replacing expressions, trimming params and returns");
    }

    eliminateDuplicateArgs();

    if (VERBOSE) {
        LOG << "===== end early decompile for " << getName() << " =====\n\n";
    }

    setStatus(PROC_EARLYDONE);

    Boomerang::get()->alertDecompileDebugPoint(this, "after middle");

    return std::make_shared<ProcSet>();
}


/*    *    *    *    *    *    *    *    *    *    *    *    *    *
*                                                    *
*    R e m o v e   u n u s e d   s t a t e m e n t s    *
*                                                    *
*    *    *    *    *    *    *    *    *    *    *    *    *    */
void UserProc::remUnusedStmtEtc()
{
    bool convert;
    bool change;

    // NO! Removing of unused statements is an important part of the global removing unused returns analysis, which
    // happens after UserProc::decompile is complete
    // if (status >= PROC_FINAL)
    //    return;

    Boomerang::get()->alertDecompiling(this);
    Boomerang::get()->alertDecompileDebugPoint(this, "before final");

    LOG_VERBOSE("--- Remove unused statements for %1 ---", getName());
    // A temporary hack to remove %CF = %CF{7} when 7 isn't a SUBFLAGS
    //    if (theReturnStatement)
    //        theReturnStatement->specialProcessing();

    // Perform type analysis. If we are relying (as we are at present) on TA to perform ellipsis processing,
    // do the local TA pass now. Ellipsis processing often reveals additional uses (e.g. additional parameters
    // to printf/scanf), and removing unused statements is unsafe without full use information
    if (m_status < PROC_FINAL) {
        typeAnalysis();
        // Now that locals are identified, redo the dataflow
        change = m_df.placePhiFunctions(this);

        if (change) {
            numberStatements();           // Number the new statements
        }

        doRenameBlockVars(20);            // Rename the locals
        propagateStatements(convert, 20); // Surely need propagation too

        if (VERBOSE) {
            debugPrintAll("after propagating locals");
        }
    }

    // Only remove unused statements after decompiling as much as possible of the proc
    // Remove unused statements
    RefCounter refCounts; // The map
    // Count the references first
    countRefs(refCounts);

    // Now remove any that have no used
    if (!Boomerang::get()->noRemoveNull) {
        remUnusedStmtEtc(refCounts);
    }

    // Remove null statements
    if (!Boomerang::get()->noRemoveNull) {
        removeNullStatements();
    }

    printXML();

    if (!Boomerang::get()->noRemoveNull) {
        debugPrintAll("after removing unused and null statements pass 1");
    }

    Boomerang::get()->alertDecompileAfterRemoveStmts(this, 1);

    findFinalParameters();

    if (!Boomerang::get()->noParameterNames) {
        // Replace the existing temporary parameters with the final ones:
        // mapExpressionsToParameters();
        addParameterSymbols();
        debugPrintAll("after adding new parameters");
    }

    updateCalls(); // Or just updateArguments?

    branchAnalysis();
    fixUglyBranches();

    debugPrintAll("after remove unused statements etc");
    Boomerang::get()->alertDecompileDebugPoint(this, "after final");
}


void UserProc::remUnusedStmtEtc(RefCounter& refCounts)
{
    Boomerang::get()->alertDecompileDebugPoint(this, "before remUnusedStmtEtc");

    StatementList stmts;
    getStatements(stmts);
    bool change;

    do { // FIXME: check if this is ever needed
        change = false;
        StatementList::iterator ll = stmts.begin();

        while (ll != stmts.end()) {
            Instruction *s = *ll;

            if (!s->isAssignment()) {
                // Never delete a statement other than an assignment (e.g. nothing "uses" a Jcond)
                ll++;
                continue;
            }

            Assignment *as    = (Assignment *)s;
            SharedExp  asLeft = as->getLeft();

            // If depth < 0, consider all depths
            // if (asLeft && depth >= 0 && asLeft->getMemDepth() > depth) {
            //    ll++;
            //    continue;
            // }
            if (asLeft && (asLeft->getOper() == opGlobal)) {
                // assignments to globals must always be kept
                ll++;
                continue;
            }

            // If it's a memof and renameable it can still be deleted
            if ((asLeft->getOper() == opMemOf) && !canRename(asLeft)) {
                // Assignments to memof-anything-but-local must always be kept.
                ll++;
                continue;
            }

            if ((asLeft->getOper() == opMemberAccess) || (asLeft->getOper() == opArrayIndex)) {
                // can't say with these; conservatively never remove them
                ll++;
                continue;
            }

            if ((refCounts.find(s) == refCounts.end()) || (refCounts[s] == 0)) { // Care not to insert unnecessarily
                // First adjust the counts, due to statements only referenced by statements that are themselves unused.
                // Need to be careful not to count two refs to the same def as two; refCounts is a count of the number
                // of statements that use a definition, not the total number of refs
                InstructionSet stmtsRefdByUnused;
                LocationSet    components;
                s->addUsedLocs(components, false); // Second parameter false to ignore uses in collectors
                LocationSet::iterator cc;

                for (cc = components.begin(); cc != components.end(); cc++) {
                    if ((*cc)->isSubscript()) {
                        stmtsRefdByUnused.insert((*cc)->access<RefExp>()->getDef());
                    }
                }

                InstructionSet::iterator dd;

                for (dd = stmtsRefdByUnused.begin(); dd != stmtsRefdByUnused.end(); dd++) {
                    if (*dd == nullptr) {
                        continue;
                    }

                    if (DEBUG_UNUSED) {
                        LOG << "decrementing ref count of " << (*dd)->getNumber() << " because " << s->getNumber()
                            << " is unused\n";
                    }

                    refCounts[*dd]--;
                }

                if (DEBUG_UNUSED) {
                    LOG << "removing unused statement " << s->getNumber() << " " << s << "\n";
                }

                removeStatement(s);
                ll     = stmts.erase(ll); // So we don't try to re-remove it
                change = true;
                continue;                 // Don't call getNext this time
            }

            ll++;
        }
    } while (change);

    // Recaluclate at least the livenesses. Example: first call to printf in test/pentium/fromssa2, eax used only in a
    // removed statement, so liveness in the call needs to be removed
    removeCallLiveness();  // Kill all existing livenesses
    doRenameBlockVars(-2); // Recalculate new livenesses
    setStatus(PROC_FINAL); // Now fully decompiled (apart from one final pass, and transforming out of SSA form)

    Boomerang::get()->alertDecompileDebugPoint(this, "after remUnusedStmtEtc");
}


void UserProc::recursionGroupAnalysis(ProcList *path, int indent)
{
    /* Overall algorithm:
     *  for each proc in the group
     *          initialise
     *          earlyDecompile
     *  for each proc in the group
     *          middleDecompile
     *  mark all calls involved in cs as non-childless
     *  for each proc in cs
     *          update parameters and returns, redoing call bypass, until no change
     *  for each proc in cs
     *          remove unused statements
     *  for each proc in cs
     *          update parameters and returns, redoing call bypass, until no change
     */
    if (VERBOSE) {
        LOG << "\n\n# # # recursion group analysis for ";
        ProcSet::iterator csi;

        for (csi = m_cycleGroup->begin(); csi != m_cycleGroup->end(); ++csi) {
            LOG << (*csi)->getName() << ", ";
        }

        LOG << "# # #\n";
    }

    // First, do the initial decompile, and call earlyDecompile
    ProcSet::iterator curp;

    for (curp = m_cycleGroup->begin(); curp != m_cycleGroup->end(); ++curp) {
        (*curp)->setStatus(PROC_INCYCLE); // So the calls are treated as childless
        Boomerang::get()->alertDecompiling(*curp);
        (*curp)->initialiseDecompile();   // Sort the CFG, number statements, etc
        (*curp)->earlyDecompile();
    }

    // Now all the procs in the group should be ready for preservation analysis
    // The standard preservation analysis should automatically perform conditional preservation
    for (curp = m_cycleGroup->begin(); curp != m_cycleGroup->end(); ++curp) {
        (*curp)->middleDecompile(path, indent);
        (*curp)->setStatus(PROC_PRESERVEDS);
    }

    // FIXME: why exactly do we do this?
    // Mark all the relevant calls as non childless (will harmlessly get done again later)
    ProcSet::iterator it;

    for (it = m_cycleGroup->begin(); it != m_cycleGroup->end(); it++) {
        (*it)->markAsNonChildless(m_cycleGroup);
    }

    ProcSet::iterator p;
    // Need to propagate into the initial arguments, since arguments are uses, and we are about to remove unused
    // statements.
    bool convert;

    for (p = m_cycleGroup->begin(); p != m_cycleGroup->end(); ++p) {
        // (*p)->initialParameters();                    // FIXME: I think this needs to be mapping locals and params now
        (*p)->mapLocalsAndParams();
        (*p)->updateArguments();
        (*p)->propagateStatements(convert, 0); // Need to propagate into arguments
    }

    // while no change
    for (int i = 0; i < 2; i++) {
        for (p = m_cycleGroup->begin(); p != m_cycleGroup->end(); ++p) {
            (*p)->remUnusedStmtEtc(); // Also does final parameters and arguments at present
        }
    }

    LOG_VERBOSE("=== End recursion group analysis ===");
    Boomerang::get()->alertEndDecompile(this);
}


void UserProc::updateCalls()
{
    if (VERBOSE) {
        LOG << "### updateCalls for " << getName() << " ###\n";
    }

    updateCallDefines();
    updateArguments();
    debugPrintAll("after update calls");
}


bool UserProc::branchAnalysis()
{
    Boomerang::get()->alertDecompileDebugPoint(this, "Before branch analysis.");
    bool          removedBBs = false;
    StatementList stmts;
    getStatements(stmts);

    for (auto stmt : stmts) {
        if (!stmt->isBranch()) {
            continue;
        }

        BranchStatement *branch = (BranchStatement *)stmt;

        if (branch->getFallBB() && branch->getTakenBB()) {
            StatementList fallstmts;
            branch->getFallBB()->getStatements(fallstmts);

            if ((fallstmts.size() == 1) && (*fallstmts.begin())->isBranch()) {
                BranchStatement *fallto = (BranchStatement *)*fallstmts.begin();

                //   branch to A if cond1
                //   branch to B if cond2
                // A: something
                // B:
                // ->
                //   branch to B if !cond1 && cond2
                // A: something
                // B:
                //
                if ((fallto->getFallBB() == branch->getTakenBB()) && (fallto->getBB()->getNumInEdges() == 1)) {
                    branch->setFallBB(fallto->getFallBB());
                    branch->setTakenBB(fallto->getTakenBB());
                    branch->setDest(fallto->getFixedDest());
                    SharedExp cond =
                        Binary::get(opAnd, Unary::get(opNot, branch->getCondExpr()), fallto->getCondExpr()->clone());
                    branch->setCondExpr(cond->simplify());
                    assert(fallto->getBB()->getNumInEdges() == 0);
                    fallto->getBB()->deleteEdge(fallto->getBB()->getOutEdge(0));
                    fallto->getBB()->deleteEdge(fallto->getBB()->getOutEdge(0));
                    assert(fallto->getBB()->getNumOutEdges() == 0);
                    m_cfg->removeBB(fallto->getBB());
                    removedBBs = true;
                }

                //   branch to B if cond1
                //   branch to B if cond2
                // A: something
                // B:
                // ->
                //   branch to B if cond1 || cond2
                // A: something
                // B:
                if ((fallto->getTakenBB() == branch->getTakenBB()) && (fallto->getBB()->getNumInEdges() == 1)) {
                    branch->setFallBB(fallto->getFallBB());
                    branch->setCondExpr(Binary::get(opOr, branch->getCondExpr(), fallto->getCondExpr()->clone()));
                    assert(fallto->getBB()->getNumInEdges() == 0);
                    fallto->getBB()->deleteEdge(fallto->getBB()->getOutEdge(0));
                    fallto->getBB()->deleteEdge(fallto->getBB()->getOutEdge(0));
                    assert(fallto->getBB()->getNumOutEdges() == 0);
                    m_cfg->removeBB(fallto->getBB());
                    removedBBs = true;
                }
            }
        }
    }

    Boomerang::get()->alertDecompileDebugPoint(this, "After branch analysis.");
    return removedBBs;
}


void UserProc::fixUglyBranches()
{
    if (VERBOSE) {
        LOG << "### fixUglyBranches for " << getName() << " ###\n";
    }

    StatementList stmts;
    getStatements(stmts);

    for (auto stmt : stmts) {
        if (!stmt->isBranch()) {
            continue;
        }

        SharedExp hl = ((BranchStatement *)stmt)->getCondExpr();

        // of the form: x{n} - 1 >= 0
        if (hl && (hl->getOper() == opGtrEq) && hl->getSubExp2()->isIntConst() &&
            (hl->access<Const, 2>()->getInt() == 0) && (hl->getSubExp1()->getOper() == opMinus) &&
            hl->getSubExp1()->getSubExp2()->isIntConst() && (hl->access<Const, 1, 2>()->getInt() == 1) &&
            hl->getSubExp1()->getSubExp1()->isSubscript()) {
            Instruction *n = hl->access<RefExp, 1, 1>()->getDef();

            if (n && n->isPhi()) {
                PhiAssign *p = (PhiAssign *)n;

                for (const auto& phi : *p) {
                    if (!phi.second.def()->isAssign()) {
                        continue;
                    }

                    Assign *a = (Assign *)phi.second.def();

                    if (*a->getRight() == *hl->getSubExp1()) {
                        hl->setSubExp1(RefExp::get(a->getLeft(), a));
                        break;
                    }
                }
            }
        }
    }

    debugPrintAll("after fixUglyBranches");
}


bool UserProc::doRenameBlockVars(int pass, bool clearStacks)
{
    LOG_VERBOSE("### Rename block vars for %1 pass %2, clear = %3 ###", getName(), pass, clearStacks);
    bool b = m_df.renameBlockVars(this, 0, clearStacks);
    LOG_VERBOSE("df.renameBlockVars return %1", (b ? "true" : "false"));
    return b;
}


void UserProc::findSpPreservation()
{
    LOG_VERBOSE("Finding stack pointer preservation for %1", getName());

    bool stdsp = false; // FIXME: are these really used?
    // Note: need this non-virtual version most of the time, since nothing proved yet
    int sp = m_signature->getStackRegister(m_prog);

    for (int n = 0; n < 2; n++) {
        // may need to do multiple times due to dependencies FIXME: efficiency! Needed any more?

        // Special case for 32-bit stack-based machines (e.g. Pentium).
        // RISC machines generally preserve the stack pointer (so no special case required)
        for (int p = 0; !stdsp && p < 8; p++) {
            if (DEBUG_PROOF) {
                LOG << "attempting to prove sp = sp + " << p * 4 << " for " << getName() << "\n";
            }

            stdsp = prove(
                Binary::get(opEquals,
                            Location::regOf(sp),
                            Binary::get(opPlus, Location::regOf(sp), Const::get(p * 4))));
        }
    }

    if (DEBUG_PROOF) {
        LOG << "proven for " << getName() << ":\n";

        for (auto& elem : m_provenTrue) {
            LOG << elem.first << " = " << elem.second << "\n";
        }
    }
}


void UserProc::findPreserveds()
{
    std::set<SharedExp> removes;

    LOG_VERBOSE("Finding preserveds for %1", getName());

    Boomerang::get()->alertDecompileDebugPoint(this, "before finding preserveds");

    if (theReturnStatement == nullptr) {
        if (DEBUG_PROOF) {
            LOG << "can't find preservations as there is no return statement!\n";
        }

        Boomerang::get()->alertDecompileDebugPoint(this, "after finding preserveds (no return)");
        return;
    }

    // prove preservation for all modifieds in the return statement
    ReturnStatement::iterator mm;
    StatementList&            modifieds = theReturnStatement->getModifieds();

    for (mm = modifieds.begin(); mm != modifieds.end(); ++mm) {
        SharedExp lhs      = ((Assignment *)*mm)->getLeft();
        auto      equation = Binary::get(opEquals, lhs, lhs);

        if (DEBUG_PROOF) {
            LOG << "attempting to prove " << equation << " is preserved by " << getName() << "\n";
        }

        if (prove(equation)) {
            removes.insert(equation);
        }
    }

    if (DEBUG_PROOF) {
        LOG << "### proven true for procedure " << getName() << ":\n";

        for (auto& elem : m_provenTrue) {
            LOG << elem.first << " = " << elem.second << "\n";
        }

        LOG << "### end proven true for procedure " << getName() << "\n\n";
    }

    // Remove the preserved locations from the modifieds and the returns
    std::map<SharedExp, SharedExp, lessExpStar>::iterator pp;

    for (pp = m_provenTrue.begin(); pp != m_provenTrue.end(); ++pp) {
        SharedExp lhs = pp->first;
        SharedExp rhs = pp->second;

        // Has to be of the form loc = loc, not say loc+4, otherwise the bypass logic won't see the add of 4
        if (!(*lhs == *rhs)) {
            continue;
        }

        theReturnStatement->removeModified(lhs);
    }

    Boomerang::get()->alertDecompileDebugPoint(this, "after finding preserveds");
}


void UserProc::removeSpAssignsIfPossible()
{
    // if there are no uses of sp other than sp{-} in the whole procedure,
    // we can safely remove all assignments to sp, this will make the output
    // more readable for human eyes.

    auto sp(Location::regOf(m_signature->getStackRegister(m_prog)));
    bool foundone = false;

    StatementList stmts;

    getStatements(stmts);

    for (auto stmt : stmts) {
        if (stmt->isAssign() && (*((Assign *)stmt)->getLeft() == *sp)) {
            foundone = true;
        }

        LocationSet refs;
        stmt->addUsedLocs(refs);

        for (const SharedExp& rr : refs) {
            if (rr->isSubscript() && (*rr->getSubExp1() == *sp)) {
                Instruction *def = rr->access<RefExp>()->getDef();

                if (def && (def->getProc() == this)) {
                    return;
                }
            }
        }
    }

    if (!foundone) {
        return;
    }

    Boomerang::get()->alertDecompileDebugPoint(this, "before removing stack pointer assigns.");

    for (auto& stmt : stmts) {
        if ((stmt)->isAssign()) {
            Assign *a = (Assign *)stmt;

            if (*a->getLeft() == *sp) {
                removeStatement(a);
            }
        }
    }

    Boomerang::get()->alertDecompileDebugPoint(this, "after removing stack pointer assigns.");
}


void UserProc::removeMatchingAssignsIfPossible(SharedExp e)
{
    // if there are no uses of %flags in the whole procedure,
    // we can safely remove all assignments to %flags, this will make the output
    // more readable for human eyes and makes short circuit analysis easier.

    bool foundone = false;

    StatementList stmts;

    getStatements(stmts);

    for (auto stmt : stmts) {
        if (stmt->isAssign() && (*((Assign *)stmt)->getLeft() == *e)) {
            foundone = true;
        }

        if (stmt->isPhi()) {
            if (*((PhiAssign *)stmt)->getLeft() == *e) {
                foundone = true;
            }

            continue;
        }

        LocationSet refs;
        stmt->addUsedLocs(refs);

        for (const SharedExp& rr : refs) {
            if (rr->isSubscript() && (*rr->getSubExp1() == *e)) {
                Instruction *def = rr->access<RefExp>()->getDef();

                if (def && (def->getProc() == this)) {
                    return;
                }
            }
        }
    }

    if (!foundone) {
        return;
    }

    QString     res_str;
    QTextStream str(&res_str);
    str << "Before removing matching assigns (" << e << ").";
    Boomerang::get()->alertDecompileDebugPoint(this, qPrintable(res_str));
    LOG_VERBOSE(res_str);

    for (auto& stmt : stmts) {
        if ((stmt)->isAssign()) {
            Assign *a = (Assign *)stmt;

            if (*a->getLeft() == *e) {
                removeStatement(a);
            }
        }
        else if ((stmt)->isPhi()) {
            PhiAssign *a = (PhiAssign *)stmt;

            if (*a->getLeft() == *e) {
                removeStatement(a);
            }
        }
    }

    res_str.clear();
    str << "after removing matching assigns (" << e << ").";
    Boomerang::get()->alertDecompileDebugPoint(this, qPrintable(res_str));
    LOG << res_str << "\n";
}


void UserProc::assignProcsToCalls()
{
    std::list<BasicBlock *>::iterator it;
    BasicBlock *pBB = m_cfg->getFirstBB(it);

    while (pBB) {
        std::list<RTL *> *rtls = pBB->getRTLs();

        if (rtls == nullptr) {
            pBB = m_cfg->getNextBB(it);
            continue;
        }

        for (auto& rtl : *rtls) {
            if (!(rtl)->isCall()) {
                continue;
            }

            CallStatement *call = (CallStatement *)(rtl)->back();

            if ((call->getDestProc() == nullptr) && !call->isComputed()) {
                Function *p = m_prog->findProc(call->getFixedDest());

                if (p == nullptr) {
                    LOG_STREAM_OLD() << "Cannot find proc for dest " << call->getFixedDest() << " in call at "
                                 << (rtl)->getAddress() << "\n";
                    assert(p);
                }

                call->setDestProc(p);
            }

            // call->setSigArguments();        // But BBs not set yet; will get done in initStatements()
        }

        pBB = m_cfg->getNextBB(it);
    }
}


void UserProc::finalSimplify()
{
    std::list<BasicBlock *>::iterator it;
    BasicBlock *pBB = m_cfg->getFirstBB(it);

    while (pBB) {
        std::list<RTL *> *pRtls = pBB->getRTLs();

        if (pRtls == nullptr) {
            pBB = m_cfg->getNextBB(it);
            continue;
        }

        for (RTL *rit : *pRtls) {
            for (Instruction *rt : *rit) {
                rt->simplifyAddr();
                // Also simplify everything; in particular, stack offsets are
                // often negative, so we at least canonicalise [esp + -8] to [esp-8]
                rt->simplify();
            }
        }

        pBB = m_cfg->getNextBB(it);
    }
}


// m[WILD]{-}
static SharedExp memOfWild = RefExp::get(Location::memOf(Terminal::get(opWild)), nullptr);
// r[WILD INT]{-}
static SharedExp regOfWild = RefExp::get(Location::regOf(Terminal::get(opWildIntConst)), nullptr);


void UserProc::findFinalParameters()
{
    Boomerang::get()->alertDecompileDebugPoint(this, "before find final parameters.");

    m_parameters.clear();

    if (m_signature->isForced()) {
        // Copy from signature
        int               n = m_signature->getNumParams();
        ImplicitConverter ic(m_cfg);

        for (int i = 0; i < n; ++i) {
            SharedExp             paramLoc = m_signature->getParamExp(i)->clone(); // E.g. m[r28 + 4]
            LocationSet           components;
            LocationSet::iterator cc;
            paramLoc->addUsedLocs(components);

            for (cc = components.begin(); cc != components.end(); ++cc) {
                if (*cc != paramLoc) {                       // Don't subscript outer level
                    paramLoc->expSubscriptVar(*cc, nullptr); // E.g. r28 -> r28{-}
                    paramLoc->accept(&ic);                   // E.g. r28{-} -> r28{0}
                }
            }

            ImplicitAssign *ia = new ImplicitAssign(m_signature->getParamType(i), paramLoc);
            m_parameters.append(ia);
            QString   name       = m_signature->getParamName(i);
            SharedExp param      = Location::param(name, this);
            SharedExp reParamLoc = RefExp::get(paramLoc, m_cfg->findImplicitAssign(paramLoc));
            mapSymbolTo(reParamLoc, param); // Update name map
        }

        return;
    }

    if (DEBUG_PARAMS) {
        LOG_VERBOSE("Finding final parameters for %1", getName());
    }

    //    int sp = signature->getStackRegister();
    m_signature->setNumParams(0); // Clear any old ideas
    StatementList stmts;
    getStatements(stmts);

    StatementList::iterator it;

    for (it = stmts.begin(); it != stmts.end(); ++it) {
        Instruction *s = *it;

        // Assume that all parameters will be m[]{0} or r[]{0}, and in the implicit definitions at the start of the
        // program
        if (!s->isImplicit()) {
            // Note: phis can get converted to assignments, but I hope that this is only later on: check this!
            break; // Stop after reading all implicit assignments
        }

        SharedExp e = ((ImplicitAssign *)s)->getLeft();

        if (m_signature->findParam(e) == -1) {
            if (VERBOSE || DEBUG_PARAMS) {
                LOG << "potential param " << e << "\n";
            }

            // I believe that the only true parameters will be registers or memofs that look like locals (stack
            // pararameters)
            if (!(e->isRegOf() || isLocalOrParamPattern(e))) {
                continue;
            }

            if (DEBUG_PARAMS) {
                LOG_VERBOSE("Found new parameter %1", e);
            }

            SharedType ty = ((ImplicitAssign *)s)->getType();
            // Add this parameter to the signature (for now; creates parameter names)
            addParameter(e, ty);
            // Insert it into the parameters StatementList, in sensible order
            insertParameter(e, ty);
        }
    }

    Boomerang::get()->alertDecompileDebugPoint(this, "after find final parameters.");
}


void UserProc::removeReturn(SharedExp e)
{
    if (theReturnStatement) {
        theReturnStatement->removeReturn(e);
    }
}


void Function::removeParameter(SharedExp e)
{
    int n = m_signature->findParam(e);

    if (n != -1) {
        m_signature->removeParameter(n);

        for (auto const& elem : m_callerSet) {
            if (DEBUG_UNUSED) {
                LOG << "removing argument " << e << " in pos " << n << " from " << elem << "\n";
            }

            (elem)->removeArgument(n);
        }
    }
}


void Function::removeReturn(SharedExp e)
{
    m_signature->removeReturn(e);
}


void UserProc::addParameter(SharedExp e, SharedType ty)
{
    // In case it's already an implicit argument:
    removeParameter(e);

    m_signature->addParameter(e, ty);
}


void UserProc::processFloatConstants()
{
    StatementList stmts;

    getStatements(stmts);

    static Ternary match(opFsize, Terminal::get(opWild), Terminal::get(opWild), Location::memOf(Terminal::get(opWild)));

    StatementList::iterator it;

    for (it = stmts.begin(); it != stmts.end(); it++) {
        Instruction *s = *it;

        std::list<SharedExp> results;
        s->searchAll(match, results);

        for (auto& result : results) {
            auto fsize = result->access<Ternary>();

            if ((fsize->getSubExp3()->getOper() == opMemOf) &&
                (fsize->getSubExp3()->getSubExp1()->getOper() == opIntConst)) {
                SharedExp memof = fsize->getSubExp3();
                            Address   u     = memof->access<Const, 1>()->getAddr();
                bool      ok;
                double    d = m_prog->getFloatConstant(u, ok);

                if (ok) {
                    LOG << "replacing " << memof << " with " << d << " in " << fsize << "\n";
                    fsize->setSubExp3(Const::get(d));
                }
            }
        }

        s->simplify();
    }
}


void UserProc::addParameterSymbols()
{
    StatementList::iterator it;
    ImplicitConverter       ic(m_cfg);
    int i = 0;

    for (it = m_parameters.begin(); it != m_parameters.end(); ++it, ++i) {
        SharedExp lhs = ((Assignment *)*it)->getLeft();
        lhs = lhs->expSubscriptAllNull();
        lhs = lhs->accept(&ic);
        SharedExp to = Location::param(m_signature->getParamName(i), this);
        mapSymbolTo(lhs, to);
    }
}


SharedExp UserProc::getSymbolExp(SharedExp le, SharedType ty, bool lastPass)
{
    SharedExp e = nullptr;

    // check for references to the middle of a local
    if (le->isMemOf() && (le->getSubExp1()->getOper() == opMinus) && le->getSubExp1()->getSubExp1()->isSubscript() &&
        le->getSubExp1()->getSubExp1()->getSubExp1()->isRegN(m_signature->getStackRegister()) &&
        le->getSubExp1()->getSubExp2()->isIntConst()) {
        for (auto& elem : m_symbolMap) {
            if (!(elem).second->isLocal()) {
                continue;
            }

            QString nam = (elem).second->access<Const, 1>()->getStr();

            if (m_locals.find(nam) == m_locals.end()) {
                continue;
            }

            SharedType     lty = m_locals[nam];
            SharedConstExp loc = elem.first;

            if (loc->isMemOf() && (loc->getSubExp1()->getOper() == opMinus) &&
                loc->access<Exp, 1, 1>()->isSubscript() &&
                loc->access<Exp, 1, 1, 1>()->isRegN(m_signature->getStackRegister()) &&
                loc->access<Exp, 1, 2>()->isIntConst()) {
                int n = -loc->access<Const, 1, 2>()->getInt();
                int m = -le->access<Const, 1, 2>()->getInt();

                if ((m > n) && (m < n + (int)(lty->getSize() / 8))) {
                    e = Location::memOf(Binary::get(opPlus, Unary::get(opAddrOf, elem.second->clone()), Const::get(m - n)));
                    LOG_VERBOSE("Seems %1 is in the middle of %2 returning %3", le, loc, e);
                    return e;
                }
            }
        }
    }

    if (m_symbolMap.find(le) == m_symbolMap.end()) {
        if (ty == nullptr) {
            if (lastPass) {
                ty = IntegerType::get(STD_SIZE);
            }
            else {
                ty = VoidType::get(); // HACK MVE
            }
        }

        // the default of just assigning an int type is bad..  if the locals is not an int then assigning it this
        // type early results in aliases to this local not being recognised
        if (ty) {
            //            Exp* base = le;
            //            if (le->isSubscript())
            //                base = ((RefExp*)le)->getSubExp1();
            // NOTE: using base below instead of le does not enhance anything, but causes us to lose def information
            e = newLocal(ty->clone(), le);
            mapSymbolTo(le->clone(), e);
            e = e->clone();
        }
    }
    else {
        e = getSymbolFor(le, ty);
    }

    return e;
}


void UserProc::mapExpressionsToLocals(bool lastPass)
{
    static SharedExp sp_location = Location::regOf(0);
    // parse("[*] + sp{0}")
    static Binary nn(opPlus, Terminal::get(opWild), RefExp::get(sp_location, nullptr));
    StatementList stmts;

    getStatements(stmts);

    Boomerang::get()->alertDecompileDebugPoint(this, "before mapping expressions to locals");

    if (VERBOSE) {
        LOG << "mapping expressions to locals for " << getName();

        if (lastPass) {
            LOG << " last pass";
        }

        LOG << "\n";
    }

    int sp = m_signature->getStackRegister(m_prog);

    if (getProven(Location::regOf(sp)) == nullptr) {
        if (VERBOSE) {
            LOG << "can't map locals since sp unproven\n";
        }

        return; // can't replace if nothing proven about sp
    }

    // start with calls because that's where we have the most types
    StatementList::iterator it;

    for (it = stmts.begin(); it != stmts.end(); it++) {
        if ((*it)->isCall()) {
            CallStatement *call = (CallStatement *)*it;

            for (int i = 0; i < call->getNumArguments(); i++) {
                SharedType ty = call->getArgumentType(i);
                SharedExp  e  = call->getArgumentExp(i);

                // If a pointer type and e is of the form m[sp{0} - K]:
                if (ty && ty->resolvesToPointer() && m_signature->isAddrOfStackLocal(m_prog, e)) {
                    LOG << "argument " << e << " is an addr of stack local and the type resolves to a pointer\n";
                    auto       olde(e->clone());
                    SharedType pty = ty->as<PointerType>()->getPointsTo();

                    if (e->isAddrOf() && e->getSubExp1()->isSubscript() && e->getSubExp1()->getSubExp1()->isMemOf()) {
                        e = e->getSubExp1()->getSubExp1()->getSubExp1();
                    }

                    if (pty->resolvesToArray() && pty->as<ArrayType>()->isUnbounded()) {
                        auto a = std::static_pointer_cast<ArrayType>(pty->as<ArrayType>()->clone());
                        pty = a;
                        a->setLength(1024); // just something arbitrary

                        if (i + 1 < call->getNumArguments()) {
                            SharedType nt = call->getArgumentType(i + 1);

                            if (nt->isNamed()) {
                                nt = std::static_pointer_cast<NamedType>(nt)->resolvesTo();
                            }

                            if (nt->isInteger() && call->getArgumentExp(i + 1)->isIntConst()) {
                                a->setLength(call->getArgumentExp(i + 1)->access<Const>()->getInt());
                            }
                        }
                    }

                    e = getSymbolExp(Location::memOf(e->clone(), this), pty);

                    if (e) {
                        auto ne = Unary::get(opAddrOf, e);
                        LOG_VERBOSE("Replacing argument %1 with %2 in %3", olde, ne, (const CallStatement*)call);
                        call->setArgumentExp(i, ne);
                    }
                }
            }
        }
    }

    Boomerang::get()->alertDecompileDebugPoint(this, "after processing locals in calls");

    // normalise sp usage (turn WILD + sp{0} into sp{0} + WILD)
    sp_location->access<Const, 1>()->setInt(sp); // set to search sp value

    for (it = stmts.begin(); it != stmts.end(); it++) {
        Instruction          *s = *it;
        std::list<SharedExp> results;
        s->searchAll(nn, results);

        for (SharedExp& result : results) {
            auto wild = result->getSubExp1();
            result->setSubExp1(result->getSubExp2());
            result->setSubExp2(wild);
        }
    }

    // FIXME: this is probably part of the ADHOC TA
    // look for array locals
    // l = m[(sp{0} + WILD1) - K2]
    static std::shared_ptr<Const> sp_const(Const::get(0));
    static auto sp_loc(Location::get(opRegOf, sp_const, nullptr));
    static auto query_f(Location::get(
                            opMemOf, Binary::get(opMinus, Binary::get(opPlus, RefExp::get(sp_loc, nullptr), Terminal::get(opWild)),
                                                 Terminal::get(opWildIntConst)),
                            nullptr));

    for (it = stmts.begin(); it != stmts.end(); it++) {
        Instruction          *s = *it;
        std::list<SharedExp> results;
        sp_const->setInt(sp);
        s->searchAll(*query_f, results);

        for (SharedExp& result : results) {
            // arr = m[sp{0} - K2]
            SharedExp arr = Location::memOf(Binary::get(opMinus, RefExp::get(Location::regOf(sp), nullptr),
                                                        result->getSubExp1()->getSubExp2()->clone()),
                                            this);
            int        n    = result->access<Const, 1, 2>()->getInt();
            SharedType base = IntegerType::get(STD_SIZE);

            if (s->isAssign() && (((Assign *)s)->getLeft() == result)) {
                SharedType at = ((Assign *)s)->getType();

                if (at && (at->getSize() != 0)) {
                    base = ((Assign *)s)->getType()->clone();
                }
            }

            // arr->setType(ArrayType::get(base, n / (base->getSize() / 8))); //TODO: why is this commented out ?
            LOG_VERBOSE("Found a local array using %1 bytes", n);
            SharedExp replace = Location::memOf(Binary::get(opPlus, Unary::get(opAddrOf, arr),
                                                            result->access<Exp, 1, 1, 2>()->clone()),
                                                this);
            // TODO: the change from de8c876e9ca33e6f5aab39191204e80b81048d67 doesn't change anything, but 'looks'
            // better
            auto actual_replacer = std::make_shared<TypedExp>(ArrayType::get(base, n / (base->getSize() / 8)), replace);

            if (VERBOSE) {
                LOG << "replacing " << result << " with " << actual_replacer << " in " << s << "\n";
            }

            s->searchAndReplace(*result, actual_replacer);
        }
    }

    Boomerang::get()->alertDecompileDebugPoint(this, "after processing array locals");

    // Stack offsets for local variables could be negative (most machines), positive (PA/RISC), or both (SPARC)
    if (m_signature->isLocalOffsetNegative()) {
        searchRegularLocals(opMinus, lastPass, sp, stmts);
    }

    if (m_signature->isLocalOffsetPositive()) {
        searchRegularLocals(opPlus, lastPass, sp, stmts);
    }

    // Ugh - m[sp] is a special case: neither positive or negative.  SPARC uses this to save %i0
    if (m_signature->isLocalOffsetPositive() && m_signature->isLocalOffsetNegative()) {
        searchRegularLocals(opWild, lastPass, sp, stmts);
    }

    Boomerang::get()->alertDecompileDebugPoint(this, "after mapping expressions to locals");
}


void UserProc::searchRegularLocals(OPER minusOrPlus, bool lastPass, int sp, StatementList& stmts)
{
    // replace expressions in regular statements with locals
    SharedExp l;

    if (minusOrPlus == opWild) {
        // l = m[sp{0}]
        l = Location::memOf(RefExp::get(Location::regOf(sp), nullptr));
    }
    else {
        // l = m[sp{0} +/- K]
        l = Location::memOf(
            Binary::get(minusOrPlus, RefExp::get(Location::regOf(sp), nullptr), Terminal::get(opWildIntConst)));
    }

    StatementList::iterator it;

    for (it = stmts.begin(); it != stmts.end(); it++) {
        Instruction          *s = *it;
        std::list<SharedExp> results;
        s->searchAll(*l, results);

        for (auto result : results) {
            SharedType ty = s->getTypeFor(result);
            SharedExp  e  = getSymbolExp(result, ty, lastPass);

            if (e) {
                SharedExp search = result->clone();

                if (VERBOSE) {
                    LOG << "mapping " << search << " to " << e << " in " << s << "\n";
                }

                // s->searchAndReplace(search, e);
            }
        }

        // s->simplify();
    }
}


bool UserProc::removeNullStatements()
{
    bool          change = false;
    StatementList stmts;

    getStatements(stmts);
    // remove null code
    StatementList::iterator it;

    for (it = stmts.begin(); it != stmts.end(); it++) {
        Instruction *s = *it;

        if (s->isNullStatement()) {
            // A statement of the form x := x
            if (VERBOSE) {
                LOG << "removing null statement: " << s->getNumber() << " " << s << "\n";
            }

            removeStatement(s);
            change = true;
        }
    }

    return change;
}


bool UserProc::propagateStatements(bool& convert, int pass)
{
    LOG_VERBOSE("--- Begin propagating statements pass %1 ---", pass);
    StatementList stmts;
    getStatements(stmts);
    // propagate any statements that can be
    StatementList::iterator it;
    // Find the locations that are used by a live, dominating phi-function
    LocationSet usedByDomPhi;
    findLiveAtDomPhi(usedByDomPhi);
    // Next pass: count the number of times each assignment LHS would be propagated somewhere
    std::map<SharedExp, int, lessExpStar> destCounts;

    // Also maintain a set of locations which are used by phi statements
    for (it = stmts.begin(); it != stmts.end(); it++) {
        Instruction     *s = *it;
        ExpDestCounter  edc(destCounts);
        StmtDestCounter sdc(&edc);
        s->accept(&sdc);
    }

#if USE_DOMINANCE_NUMS
    // A third pass for dominance numbers
    setDominanceNumbers();
#endif
    // A fourth pass to propagate only the flags (these must be propagated even if it results in extra locals)
    bool change = false;

    for (it = stmts.begin(); it != stmts.end(); it++) {
        Instruction *s = *it;

        if (s->isPhi()) {
            continue;
        }

        change |= s->propagateFlagsTo();
    }

    // Finally the actual propagation
    convert = false;

    for (it = stmts.begin(); it != stmts.end(); it++) {
        Instruction *s = *it;

        if (s->isPhi()) {
            continue;
        }

        change |= s->propagateTo(convert, &destCounts, &usedByDomPhi);
    }

    simplify();
    propagateToCollector();
    LOG_VERBOSE("=== End propagating statements at pass %1 ===", pass);
    return change;
}


Instruction *UserProc::getStmtAtLex(unsigned int begin, unsigned int end)
{
    StatementList stmts;

    getStatements(stmts);

    unsigned int lowest      = begin;
    Instruction  *loweststmt = nullptr;

    for (auto& stmt : stmts) {
        if ((begin >= (stmt)->getLexBegin()) && (begin <= lowest) && (begin <= (stmt)->getLexEnd()) &&
            ((end == (unsigned)-1) || (end < (stmt)->getLexEnd()))) {
            loweststmt = (stmt);
            lowest     = (stmt)->getLexBegin();
        }
    }

    return loweststmt;
}


void UserProc::promoteSignature()
{
    m_signature = m_signature->promote(this);
}


QString UserProc::newLocalName(const SharedExp& e)
{
    QString     tgt;
    QTextStream ost(&tgt);

    if (e->isSubscript() && e->getSubExp1()->isRegOf()) {
        // Assume that it's better to know what register this location was created from
        QString regName = getRegName(e->getSubExp1());
        int     tag     = 0;

        do {
            ost.flush();
            tgt.clear();
            ost << regName << "_" << ++tag;
        } while (m_locals.find(tgt) != m_locals.end());

        return tgt;
    }

    ost << "local" << m_nextLocal++;
    return tgt;
}


SharedExp UserProc::newLocal(SharedType ty, const SharedExp& e, char *nam /* = nullptr */)
{
    QString name;

    if (nam == nullptr) {
        name = newLocalName(e);
    }
    else {
        name = nam; // Use provided name
    }

    m_locals[name] = ty;

    if (ty == nullptr) {
        LOG_FATAL("Null type passed to newLocal");
    }

    LOG_VERBOSE("Assigning type %1 to new %2", ty->getCtype(), name);

    return Location::local(name, this);
}


void UserProc::addLocal(SharedType ty, const QString& nam, SharedExp e)
{
    // symbolMap is a multimap now; you might have r8->o0 for integers and r8->o0_1 for char*
    // assert(symbolMap.find(e) == symbolMap.end());
    mapSymbolTo(e, Location::local(nam, this));
    // assert(locals.find(nam) == locals.end());        // Could be r10{20} -> o2, r10{30}->o2 now
    m_locals[nam] = ty;
}


SharedType UserProc::getLocalType(const QString& nam)
{
    if (m_locals.find(nam) == m_locals.end()) {
        return nullptr;
    }

    SharedType ty = m_locals[nam];
    return ty;
}


void UserProc::setLocalType(const QString& nam, SharedType ty)
{
    m_locals[nam] = ty;

    LOG_VERBOSE("Updating type of %1 to %2", nam, ty->getCtype());
}


SharedType UserProc::getParamType(const QString& nam)
{
    for (unsigned int i = 0; i < m_signature->getNumParams(); i++) {
        if (nam == m_signature->getParamName(i)) {
            return m_signature->getParamType(i);
        }
    }

    return nullptr;
}


void UserProc::mapSymbolToRepl(const SharedConstExp& from, SharedExp oldTo, SharedExp newTo)
{
    removeSymbolMapping(from, oldTo);
    mapSymbolTo(from, newTo); // The compiler could optimise this call to a fall through
}


void UserProc::mapSymbolTo(const SharedConstExp& from, SharedExp to)
{
    SymbolMap::iterator it = m_symbolMap.find(from);

    while (it != m_symbolMap.end() && *it->first == *from) {
        if (*it->second == *to) {
            return; // Already in the multimap
        }

        ++it;
    }

    std::pair<SharedConstExp, SharedExp> pr = { from, to };
    m_symbolMap.insert(pr);
}


SharedExp UserProc::getSymbolFor(const SharedConstExp& from, SharedType ty)
{
    SymbolMap::iterator ff = m_symbolMap.find(from);

    while (ff != m_symbolMap.end() && *ff->first == *from) {
        SharedExp currTo = ff->second;
        assert(currTo->isLocal() || currTo->isParam());
        QString    name   = std::static_pointer_cast<Const>(currTo->getSubExp1())->getStr();
        SharedType currTy = getLocalType(name);

        if (currTy == nullptr) {
            currTy = getParamType(name);
        }

        if (currTy && currTy->isCompatibleWith(*ty)) {
            return currTo;
        }

        ++ff;
    }

    return nullptr;
}


void UserProc::removeSymbolMapping(const SharedConstExp& from, SharedExp to)
{
    SymbolMap::iterator it = m_symbolMap.find(from);

    while (it != m_symbolMap.end() && *it->first == *from) {
        if (*it->second == *to) {
            m_symbolMap.erase(it);
            return;
        }

        it++;
    }
}


SharedConstExp UserProc::expFromSymbol(const QString& nam) const
{
    for (const std::pair<SharedConstExp, SharedExp>& it : m_symbolMap) {
        auto e = it.second;

        if (e->isLocal() && (e->access<Const, 1>()->getStr() == nam)) {
            return it.first;
        }
    }

    return nullptr;
}


QString UserProc::getLocalName(int n)
{
    int i = 0;

    for (std::map<QString, SharedType>::iterator it = m_locals.begin(); it != m_locals.end(); it++, i++) {
        if (i == n) {
            return it->first;
        }
    }

    return nullptr;
}


QString UserProc::getSymbolName(SharedExp e)
{
    SymbolMap::iterator it = m_symbolMap.find(e);

    if (it == m_symbolMap.end()) {
        return "";
    }

    SharedExp loc = it->second;

    if (!loc->isLocal() && !loc->isParam()) {
        return "";
    }

    return loc->access<Const, 1>()->getStr();
}


void UserProc::countRefs(RefCounter& refCounts)
{
    StatementList stmts;

    getStatements(stmts);
    StatementList::iterator it;

    for (it = stmts.begin(); it != stmts.end(); it++) {
        Instruction *s = *it;

        // Don't count uses in implicit statements. There is no RHS of course, but you can still have x from m[x] on the
        // LHS and so on, and these are not real uses
        if (s->isImplicit()) {
            continue;
        }

        if (DEBUG_UNUSED) {
            LOG << "counting references in " << s << "\n";
        }

        LocationSet refs;
        s->addUsedLocs(refs, false); // Ignore uses in collectors

        for (const SharedExp& rr : refs) {
            if (rr->isSubscript()) {
                Instruction *def = rr->access<RefExp>()->getDef();

                // Used to not count implicit refs here (def->getNumber() == 0), meaning that implicit definitions get
                // removed as dead code! But these are the ideal place to read off final parameters, and it is
                // guaranteed now that implicit statements are sorted out for us by now (for dfa type analysis)
                if (def /* && def->getNumber() */) {
                    refCounts[def]++;

                    if (DEBUG_UNUSED) {
                        LOG << "counted ref to " << *rr << "\n";
                    }
                }
            }
        }
    }

    if (DEBUG_UNUSED) {
        RefCounter::iterator rr;
        LOG << "### reference counts for " << getName() << ":\n";

        for (rr = refCounts.begin(); rr != refCounts.end(); ++rr) {
            LOG << "  " << rr->first->getNumber() << ":" << rr->second << "\t";
        }

        LOG << "\n### end reference counts\n";
    }
}


void UserProc::removeUnusedLocals()
{
    Boomerang::get()->alertDecompileDebugPoint(this, "before removing unused locals");

    if (VERBOSE) {
        LOG << "removing unused locals (final) for " << getName() << "\n";
    }

    QSet<QString> usedLocals;
    StatementList stmts;
    getStatements(stmts);
    // First count any uses of the locals
    StatementList::iterator ss;
    bool all = false;

    for (ss = stmts.begin(); ss != stmts.end(); ss++) {
        Instruction *s = *ss;
        LocationSet locs;
        all |= s->addUsedLocals(locs);
        LocationSet::iterator uu;

        for (uu = locs.begin(); uu != locs.end(); uu++) {
            SharedExp u = *uu;

            // Must be a real symbol, and not defined in this statement, unless it is a return statement (in which case
            // it is used outside this procedure), or a call statement. Consider local7 = local7+1 and
            // return local7 = local7+1 and local7 = call(local7+1), where in all cases, local7 is not used elsewhere
            // outside this procedure. With the assign, it can be deleted, but with the return or call statements, it
            // can't.
            if ((s->isReturn() || s->isCall() || !s->definesLoc(u))) {
                if (!u->isLocal()) {
                    continue;
                }

                QString name(u->access<Const, 1>()->getStr());
                usedLocals.insert(name);

                if (DEBUG_UNUSED) {
                    LOG << "counted local " << name << " in " << s << "\n";
                }
            }
        }

        if (s->isAssignment() && !s->isImplicit() && ((Assignment *)s)->getLeft()->isLocal()) {
            Assignment *as = (Assignment *)s;
            auto       c   = as->getLeft()->access<Const, 1>();
            QString    name(c->getStr());
            usedLocals.insert(name);

            if (DEBUG_UNUSED) {
                LOG << "counted local " << name << " on left of " << s << "\n";
            }
        }
    }

    // Now record the unused ones in set removes
    std::map<QString, SharedType>::iterator it;
    QSet<QString> removes;

    for (it = m_locals.begin(); it != m_locals.end(); it++) {
        const QString& name(it->first);

        // LOG << "Considering local " << name << "\n";
        if (VERBOSE && all && removes.size()) {
            LOG << "WARNING: defineall seen in procedure " << name << " so not removing " << removes.size()
                << " locals\n";
        }

        if ((usedLocals.find(name) == usedLocals.end()) && !all) {
            if (VERBOSE) {
                LOG << "removed unused local " << name << "\n";
            }

            removes.insert(name);
        }
    }

    // Remove any definitions of the removed locals
    for (ss = stmts.begin(); ss != stmts.end(); ++ss) {
        Instruction           *s = *ss;
        LocationSet           ls;
        LocationSet::iterator ll;
        s->getDefinitions(ls);

        for (ll = ls.begin(); ll != ls.end(); ++ll) {
            SharedType ty   = s->getTypeFor(*ll);
            QString    name = findLocal(*ll, ty);

            if (name.isNull()) {
                continue;
            }

            if (removes.find(name) != removes.end()) {
                // Remove it. If an assign, delete it; otherwise (call), remove the define
                if (s->isAssignment()) {
                    removeStatement(s);
                    break; // Break to next statement
                }
                else if (s->isCall()) {
                    // Remove just this define. May end up removing several defines from this call.
                    ((CallStatement *)s)->removeDefine(*ll);
                }

                // else if a ReturnStatement, don't attempt to remove it. The definition is used *outside* this proc.
            }
        }
    }

    // Finally, remove them from locals, so they don't get declared
    for (QString str : removes) {
        m_locals.erase(str);
    }

    // Also remove them from the symbols, since symbols are a superset of locals at present
    for (SymbolMap::iterator sm = m_symbolMap.begin(); sm != m_symbolMap.end(); ) {
        SharedExp mapsTo = sm->second;

        if (mapsTo->isLocal()) {
            QString tmpName = mapsTo->access<Const, 1>()->getStr();

            if (removes.find(tmpName) != removes.end()) {
                m_symbolMap.erase(sm++);
                continue;
            }
        }

        ++sm; // sm is itcremented with the erase, or here
    }

    Boomerang::get()->alertDecompileDebugPoint(this, "after removing unused locals");
}


//
//    SSA code
//

void UserProc::fromSSAform()
{
    Boomerang::get()->alertDecompiling(this);

    LOG_VERBOSE("Transforming %1 from SSA form");

    Boomerang::get()->alertDecompileDebugPoint(this, "Before transforming from SSA form");

    if (m_cfg->getNumBBs() >= 100) { // Only for the larger procs
        // Note: emit newline at end of this proc, so we can distinguish getting stuck in this proc with doing a lot of
        // little procs that don't get messages. Also, looks better with progress dots
        LOG_STREAM_OLD() << " transforming out of SSA form " << getName() << " with " << m_cfg->getNumBBs() << " BBs";
    }

    StatementList stmts;
    getStatements(stmts);
    StatementList::iterator it;

    for (it = stmts.begin(); it != stmts.end(); it++) {
        // Map registers to initial local variables
        (*it)->mapRegistersToLocals();
        // Insert casts where needed, as types are about to become inaccessible
        (*it)->insertCasts();
    }

    // First split the live ranges where needed by reason of type incompatibility, i.e. when the type of a subscripted
    // variable is different to its previous type. Start at the top, because we don't want to rename parameters (e.g.
    // argc)
    typedef std::pair<SharedType, SharedExp>               FirstTypeEnt;
    typedef std::map<SharedExp, FirstTypeEnt, lessExpStar> FirstTypesMap;
    FirstTypesMap           firstTypes;
    FirstTypesMap::iterator ff;
    ConnectionGraph         ig; // The interference graph; these can't have the same local variable
    ConnectionGraph         pu; // The Phi Unites: these need the same local variable or copies

    int progress = 0;

    for (it = stmts.begin(); it != stmts.end(); it++) {
        if (++progress > 2000) {
            LOG_VERBOSE(".");
            LOG_STREAM_OLD().flush();
            progress = 0;
        }

        Instruction *s = *it;
        LocationSet defs;
        s->getDefinitions(defs);
        LocationSet::iterator dd;

        for (dd = defs.begin(); dd != defs.end(); dd++) {
            SharedExp  base = *dd;
            SharedType ty   = s->getTypeFor(base);

            if (ty == nullptr) { // Can happen e.g. when getting the type for %flags
                ty = VoidType::get();
            }

            LOG_VERBOSE("Got type %1 for %2 from %3", ty->prints(), base, s);
            ff = firstTypes.find(base);
            SharedExp ref = RefExp::get(base, s);

            if (ff == firstTypes.end()) {
                // There is no first type yet. Record it.
                FirstTypeEnt fte;
                fte.first        = ty;
                fte.second       = ref;
                firstTypes[base] = fte;
            }
            else if (ff->second.first && !ty->isCompatibleWith(*ff->second.first)) {
                if (DEBUG_LIVENESS) {
                    LOG << "def of " << base << " at " << s->getNumber() << " type " << ty
                        << " is not compatible with first type " << ff->second.first << ".\n";
                }

                // There already is a type for base, and it is different to the type for this definition.
                // Record an "interference" so it will get a new variable
                if (!ty->isVoid()) { // just ignore void interferences ??!!
                    ig.connect(ref, ff->second.second);
                }
            }
        }
    }

    assert(ig.allRefsHaveDefs());
    // Find the interferences generated by more than one version of a variable being live at the same program point
    m_cfg->findInterferences(ig);
    assert(ig.allRefsHaveDefs());

    // Find the set of locations that are "united" by phi-functions
    // FIXME: are these going to be trivially predictable?
    findPhiUnites(pu);

    ConnectionGraph::iterator ii;

    if (DEBUG_LIVENESS) {
        LOG << "## ig interference graph:\n";

        for (ii = ig.begin(); ii != ig.end(); ii++) {
            LOG << "   ig " << ii->first << " -> " << ii->second << "\n";
        }

        LOG << "## pu phi unites graph:\n";

        for (ii = pu.begin(); ii != pu.end(); ii++) {
            LOG << "   pu " << ii->first << " -> " << ii->second << "\n";
        }

        LOG << "  ---\n";
    }

    // Choose one of each interfering location to give a new name to
    assert(ig.allRefsHaveDefs());

    for (ii = ig.begin(); ii != ig.end(); ++ii) {
        auto    r1    = ii->first->access<RefExp>();
        auto    r2    = ii->second->access<RefExp>(); // r1 -> r2 and vice versa
        QString name1 = lookupSymFromRefAny(r1);
        QString name2 = lookupSymFromRefAny(r2);

        if (!name1.isNull() && !name2.isNull() && (name1 != name2)) {
            continue; // Already different names, probably because of the redundant mapping
        }

        std::shared_ptr<RefExp> rename;

        if (r1->isImplicitDef()) {
            // If r1 is an implicit definition, don't rename it (it is probably a parameter, and should retain its
            // current name)
            rename = r2;
        }
        else if (r2->isImplicitDef()) {
            rename = r1; // Ditto for r2
        }

        if (rename == nullptr) {
            Instruction *def2 = r2->getDef();

            if (def2->isPhi()) // Prefer the destinations of phis
            {
                rename = r2;
            }
            else {
                rename = r1;
            }
        }

        SharedType ty    = rename->getDef()->getTypeFor(rename->getSubExp1());
        SharedExp  local = newLocal(ty, rename);

        if (DEBUG_LIVENESS) {
            LOG << "renaming " << rename << " to " << local << "\n";
        }

        mapSymbolTo(rename, local);
    }

    // Implement part of the Phi Unites list, where renamings or parameters have broken them, by renaming
    // The rest of them will be done as phis are removed
    // The idea is that where l1 and l2 have to unite, and exactly one of them already has a local/name, you can
    // implement the unification by giving the unnamed one the same name as the named one, as long as they don't
    // interfere
    for (ii = pu.begin(); ii != pu.end(); ++ii) {
        auto    r1    = ii->first->access<RefExp>();
        auto    r2    = ii->second->access<RefExp>();
        QString name1 = lookupSymFromRef(r1);
        QString name2 = lookupSymFromRef(r2);

        if (!name1.isNull() && !name2.isNull() && !ig.isConnected(r1, *r2)) {
            // There is a case where this is unhelpful, and it happen in test/pentium/fromssa2. We have renamed the
            // destination of the phi to ebx_1, and that leaves the two phi operands as ebx. However, we attempt to
            // unite them here, which will cause one of the operands to become ebx_1, so the neat oprimisation of
            // replacing the phi with one copy doesn't work. The result is an extra copy.
            // So check of r1 is a phi and r2 one of its operands, and all other operands for the phi have the same
            // name. If so, don't rename.
            Instruction *def1 = r1->getDef();

            if (def1->isPhi()) {
                bool                allSame     = true;
                bool                r2IsOperand = false;
                QString             firstName   = QString::null;
                PhiAssign::iterator rr;
                PhiAssign           *pi = (PhiAssign *)def1;

                for (rr = pi->begin(); rr != pi->end(); ++rr) {
                    auto re(RefExp::get(rr->second.e, rr->second.def()));

                    if (*re == *r2) {
                        r2IsOperand = true;
                    }

                    if (firstName.isNull()) {
                        firstName = lookupSymFromRefAny(re);
                    }
                    else {
                        QString tmp = lookupSymFromRefAny(re);

                        if (tmp.isNull() || (firstName != tmp)) {
                            allSame = false;
                            break;
                        }
                    }
                }

                if (allSame && r2IsOperand) {
                    continue; // This situation has happened, don't map now
                }
            }

            mapSymbolTo(r2, Location::local(name1, this));
            continue;
        }
    }

    /*    *    *    *    *    *    *    *    *    *    *    *    *    *    *\
    *                                                        *
    *     IR gets changed with hard locals and params here    *
    *                                                        *
    \*    *    *    *    *    *    *    *    *    *    *    *    *    *    */

    // First rename the variables (including phi's, but don't remove).
    // NOTE: it is not possible to postpone renaming these locals till the back end, since the same base location
    // may require different names at different locations, e.g. r28{0} is local0, r28{16} is local1
    // Update symbols and parameters, particularly for the stack pointer inside memofs.
    // NOTE: the ordering of the below operations is critical! Re-ordering may well prevent e.g. parameters from
    // renaming successfully.
    verifyPHIs();
    nameParameterPhis();
    mapLocalsAndParams();
    mapParameters();
    removeSubscriptsFromSymbols();
    removeSubscriptsFromParameters();

    for (it = stmts.begin(); it != stmts.end(); it++) {
        Instruction *s = *it;
        s->replaceSubscriptsWithLocals();
    }

    // Now remove the phis
    for (it = stmts.begin(); it != stmts.end(); it++) {
        Instruction *s = *it;

        if (!s->isPhi()) {
            continue;
        }

        // Check if the base variables are all the same
        PhiAssign *pa = (PhiAssign *)s;

        if (pa->begin() == pa->end()) {
            // no params to this phi, just remove it
            if (VERBOSE) {
                LOG << "phi with no params, removing: " << s << "\n";
            }

            removeStatement(s);
            continue;
        }

        LocationSet refs;
        pa->addUsedLocs(refs);
        bool      phiParamsSame = true;
        SharedExp first         = nullptr;

        if (pa->getNumDefs() > 1) {
            PhiAssign::iterator uu;

            for (uu = pa->begin(); uu != pa->end(); uu++) {
                if (uu->second.e == nullptr) {
                    continue;
                }

                if (first == nullptr) {
                    first = uu->second.e;
                    continue;
                }

                if (!(*uu->second.e == *first)) {
                    phiParamsSame = false;
                    break;
                }
            }
        }

        if (phiParamsSame && first) {
            // Is the left of the phi assignment the same base variable as all the operands?
            if (*pa->getLeft() == *first) {
                if (DEBUG_LIVENESS || DEBUG_UNUSED) {
                    LOG << "removing phi: left and all refs same or 0: " << s << "\n";
                }

                // Just removing the refs will work, or removing the whole phi
                // NOTE: Removing the phi here may cause other statments to be not used.
                removeStatement(s);
            }
            else {
                // Need to replace the phi by an expression,
                // e.g. local0 = phi(r24{3}, r24{5}) becomes
                //        local0 = r24
                pa->convertToAssign(first->clone());
            }
        }
        else {
            // Need new local(s) for phi operands that have different names from the lhs

            // This way is costly in copies, but has no problems with extending live ranges
            // Exp* tempLoc = newLocal(pa->getType());
            SharedExp tempLoc = getSymbolExp(RefExp::get(pa->getLeft(), pa), pa->getType());

            if (DEBUG_LIVENESS) {
                LOG << "phi statement " << s << " requires local, using " << tempLoc << "\n";
            }

            // For each definition ref'd in the phi
            PhiAssign::iterator rr;

            for (rr = pa->begin(); rr != pa->end(); rr++) {
                if (rr->second.e == nullptr) {
                    continue;
                }

                insertAssignAfter(rr->second.def(), tempLoc, rr->second.e);
            }

            // Replace the RHS of the phi with tempLoc
            pa->convertToAssign(tempLoc);
        }
    }

    if (m_cfg->getNumBBs() >= 100) { // Only for the larger procs
        LOG_STREAM_OLD() << "\n";
    }

    Boomerang::get()->alertDecompileDebugPoint(this, "after transforming from SSA form");
}


void UserProc::mapParameters()
{
    // Replace the parameters with their mappings
    StatementList::iterator pp;

    for (pp = m_parameters.begin(); pp != m_parameters.end(); ++pp) {
        SharedExp lhs        = ((Assignment *)*pp)->getLeft();
        QString   mappedName = lookupParam(lhs);

        if (mappedName.isNull()) {
            LOG << "WARNING! No symbol mapping for parameter " << lhs << "\n";
            bool      allZero;
            SharedExp clean = lhs->clone()->removeSubscripts(allZero);

            if (allZero) {
                ((Assignment *)*pp)->setLeft(clean);
            }

            // Else leave them alone
        }
        else {
            ((Assignment *)*pp)->setLeft(Location::param(mappedName, this));
        }
    }
}


void UserProc::removeSubscriptsFromSymbols()
{
    // Basically, use the symbol map to map the symbols in the symbol map!
    // However, do not remove subscripts from the outer level; they are still needed for comments in the output and also
    // for when removing subscripts from parameters (still need the {0})
    // Since this will potentially change the ordering of entries, need to copy the map
    SymbolMap sm2 = m_symbolMap; // Object copy

    SymbolMap::iterator it;
    m_symbolMap.clear();
    ExpSsaXformer esx(this);

    for (it = sm2.begin(); it != sm2.end(); ++it) {
        SharedExp from = std::const_pointer_cast<Exp>(it->first);

        if (from->isSubscript()) {
            // As noted above, don't touch the outer level of subscripts
            SharedExp& sub = from->refSubExp1();
            sub = sub->accept(&esx);
        }
        else {
            from = from->accept(&esx);
        }

        mapSymbolTo(from, it->second);
    }
}


void UserProc::removeSubscriptsFromParameters()
{
    StatementList::iterator it;
    ExpSsaXformer           esx(this);

    for (it = m_parameters.begin(); it != m_parameters.end(); ++it) {
        SharedExp left = ((Assignment *)*it)->getLeft();
        left = left->accept(&esx);
        ((Assignment *)*it)->setLeft(left);
    }
}


static std::shared_ptr<Binary> allEqAll = Binary::get(opEquals, Terminal::get(opDefineAll), Terminal::get(opDefineAll));


bool UserProc::prove(const std::shared_ptr<Binary>& query, bool conditional /* = false */)
{
    assert(query->isEquality());
    SharedExp queryLeft  = query->getSubExp1();
    SharedExp queryRight = query->getSubExp2();

    if ((m_provenTrue.find(queryLeft) != m_provenTrue.end()) && (*m_provenTrue[queryLeft] == *queryRight)) {
        if (DEBUG_PROOF) {
            LOG << "found true in provenTrue cache " << query << " in " << getName() << "\n";
        }

        return true;
    }

    if (Boomerang::get()->noProve) {
        return false;
    }

    SharedExp original(query->clone());
    SharedExp origLeft  = original->getSubExp1();
    SharedExp origRight = original->getSubExp2();

    // subscript locs on the right with {-} (nullptr reference)
    LocationSet locs;
    query->getSubExp2()->addUsedLocs(locs);

    for (const SharedExp& xx : locs) {
        query->setSubExp2(query->getSubExp2()->expSubscriptValNull(xx));
    }

    if (query->getSubExp1()->getOper() != opSubscript) {
        bool gotDef = false;

        // replace expression from return set with expression in the collector of the return
        if (theReturnStatement) {
            auto def = theReturnStatement->findDefFor(query->getSubExp1());

            if (def) {
                query->setSubExp1(def);
                gotDef = true;
            }
        }

        if (!gotDef) {
            // OK, the thing I'm looking for isn't in the return collector, but perhaps there is an entry for <all>
            // If this is proved, then it is safe to say that x == x for any x with no definition reaching the exit
            auto right = origRight->clone()->simplify(); // In case it's sp+0

            if ((*origLeft == *right) &&                 // x == x
                (origLeft->getOper() != opDefineAll) &&  // Beware infinite recursion
                prove(allEqAll)) {                       // Recurse in case <all> not proven yet
                if (DEBUG_PROOF) {
                    LOG << "Using all=all for " << query->getSubExp1() << "\n" << "prove returns true\n";
                }

                m_provenTrue[origLeft->clone()] = right;
                return true;
            }

            if (DEBUG_PROOF) {
                LOG << "not in return collector: " << query->getSubExp1() << "\n" << "prove returns false\n";
            }

            return false;
        }
    }

    if (m_cycleGroup) { // If in involved in a recursion cycle
        //    then save the original query as a premise for bypassing calls
        m_recurPremises[origLeft->clone()] = origRight;
    }

    std::set<PhiAssign *>            lastPhis;
    std::map<PhiAssign *, SharedExp> cache;
    bool result = prover(query, lastPhis, cache);

    if (m_cycleGroup) {
        m_recurPremises.erase(origLeft); // Remove the premise, regardless of result
    }

    if (DEBUG_PROOF) {
        LOG << "prove returns " << (result ? "true" : "false") << " for " << query << " in " << getName() << "\n";
    }

    if (!conditional) {
        if (result) {
            m_provenTrue[origLeft] = origRight; // Save the now proven equation
        }
    }

    return result;
}


bool UserProc::prover(SharedExp query, std::set<PhiAssign *>& lastPhis, std::map<PhiAssign *, SharedExp>& cache,
                      PhiAssign *lastPhi /* = nullptr */)
{
    // A map that seems to be used to detect loops in the call graph:
    std::map<CallStatement *, SharedExp> called;
    auto phiInd = query->getSubExp2()->clone();

    if (lastPhi && (cache.find(lastPhi) != cache.end()) && (*cache[lastPhi] == *phiInd)) {
        if (DEBUG_PROOF) {
            LOG << "true - in the phi cache\n";
        }

        return true;
    }

    std::set<Instruction *> refsTo;

    query = query->clone();
    bool change  = true;
    bool swapped = false;

    while (change) {
        if (DEBUG_PROOF) {
            LOG << query << "\n";
        }

        change = false;

        if (query->getOper() == opEquals) {
            // same left and right means true
            if (*query->getSubExp1() == *query->getSubExp2()) {
                query  = Terminal::get(opTrue);
                change = true;
            }

            // move constants to the right
            if (!change) {
                auto plus = query->getSubExp1();
                auto s1s2 = plus ? plus->getSubExp2() : nullptr;

                if (plus && s1s2) {
                    if ((plus->getOper() == opPlus) && s1s2->isIntConst()) {
                        query->setSubExp2(Binary::get(opPlus, query->getSubExp2(), Unary::get(opNeg, s1s2->clone())));
                        query->setSubExp1(plus->getSubExp1());
                        change = true;
                    }

                    if ((plus->getOper() == opMinus) && s1s2->isIntConst()) {
                        query->setSubExp2(Binary::get(opPlus, query->getSubExp2(), s1s2->clone()));
                        query->setSubExp1(plus->getSubExp1());
                        change = true;
                    }
                }
            }

            // substitute using a statement that has the same left as the query
            if (!change && (query->getSubExp1()->getOper() == opSubscript)) {
                auto          r     = query->access<RefExp, 1>();
                Instruction   *s    = r->getDef();
                CallStatement *call = dynamic_cast<CallStatement *>(s);

                if (call) {
                    // See if we can prove something about this register.
                    UserProc *destProc = dynamic_cast<UserProc *>(call->getDestProc());
                    auto     base      = r->getSubExp1();

                    if (destProc && !destProc->isLib() && (destProc->m_cycleGroup != nullptr) &&
                        (destProc->m_cycleGroup->find(this) != destProc->m_cycleGroup->end())) {
                        // The destination procedure may not have preservation proved as yet, because it is involved
                        // in our recursion group. Use the conditional preservation logic to determine whether query is
                        // true for this procedure
                        auto provenTo = destProc->getProven(base);

                        if (provenTo) {
                            // There is a proven preservation. Use it to bypass the call
                            auto queryLeft = call->localiseExp(provenTo->clone());
                            query->setSubExp1(queryLeft);
                            // Now try everything on the result
                            return prover(query, lastPhis, cache, lastPhi);
                        }
                        else {
                            // Check if the required preservation is one of the premises already assumed
                            auto premisedTo = destProc->getPremised(base);

                            if (premisedTo) {
                                if (DEBUG_PROOF) {
                                    LOG << "conditional preservation for call from " << getName() << " to "
                                        << destProc->getName() << ", allows bypassing\n";
                                }

                                auto queryLeft = call->localiseExp(premisedTo->clone());
                                query->setSubExp1(queryLeft);
                                return prover(query, lastPhis, cache, lastPhi);
                            }
                            else {
                                // There is no proof, and it's not one of the premises. It may yet succeed, by making
                                // another premise! Example: try to prove esp, depends on whether ebp is preserved, so
                                // recurse to check ebp's preservation. Won't infinitely loop because of the premise map
                                // FIXME: what if it needs a rx = rx + K preservation?
                                auto newQuery = Binary::get(opEquals, base->clone(), base->clone());
                                destProc->setPremise(base);

                                if (DEBUG_PROOF) {
                                    LOG << "new required premise " << newQuery << " for " << destProc->getName() << "\n";
                                }

                                // Pass conditional as true, since even if proven, this is conditional on other things
                                bool result = destProc->prove(newQuery, true);
                                destProc->killPremise(base);

                                if (result) {
                                    if (DEBUG_PROOF) {
                                        LOG << "conditional preservation with new premise " << newQuery
                                            << " succeeds for " << destProc->getName() << "\n";
                                    }

                                    // Use the new conditionally proven result
                                    auto queryLeft = call->localiseExp(base->clone());
                                    query->setSubExp1(queryLeft);
                                    return destProc->prover(query, lastPhis, cache, lastPhi);
                                }
                                else {
                                    if (DEBUG_PROOF) {
                                        LOG << "conditional preservation required premise " << newQuery << " fails!\n";
                                    }

                                    // Do nothing else; the outer proof will likely fail
                                }
                            }
                        }
                    } // End call involved in this recursion group

                    // Seems reasonable that recursive procs need protection from call loops too
                    auto right = call->getProven(r->getSubExp1()); // getProven returns the right side of what is

                    if (right) {                                   //    proven about r (the LHS of query)
                        right = right->clone();

                        if ((called.find(call) != called.end()) && (*called[call] == *query)) {
                            LOG << "found call loop to " << call->getDestProc()->getName() << " " << query << "\n";
                            query  = Terminal::get(opFalse);
                            change = true;
                        }
                        else {
                            called[call] = query->clone();

                            if (DEBUG_PROOF) {
                                LOG << "using proven for " << call->getDestProc()->getName() << " " << r->getSubExp1()
                                    << " = " << right << "\n";
                            }

                            right = call->localiseExp(right);

                            if (DEBUG_PROOF) {
                                LOG << "right with subs: " << right << "\n";
                            }

                            query->setSubExp1(right); // Replace LHS of query with right
                            change = true;
                        }
                    }
                }
                else if (s && s->isPhi()) {
                    // for a phi, we have to prove the query for every statement
                    PhiAssign           *pa = (PhiAssign *)s;
                    PhiAssign::iterator it;
                    bool                ok = true;

                    if ((lastPhis.find(pa) != lastPhis.end()) || (pa == lastPhi)) {
                        if (DEBUG_PROOF) {
                            LOG << "phi loop detected ";
                        }

                        ok = (*query->getSubExp2() == *phiInd);

                        if (ok && DEBUG_PROOF) {
                            LOG << "(set true due to induction)\n"; // FIXME: induction??!
                        }

                        if (!ok && DEBUG_PROOF) {
                            LOG << "(set false " << query->getSubExp2() << " != " << phiInd << ")\n";
                        }
                    }
                    else {
                        if (DEBUG_PROOF) {
                            LOG << "found " << s << " prove for each\n";
                        }

                        for (it = pa->begin(); it != pa->end(); it++) {
                            auto e  = query->clone();
                            auto r1 = e->access<RefExp, 1>();
                            r1->setDef(it->second.def());

                            if (DEBUG_PROOF) {
                                LOG << "proving for " << e << "\n";
                            }

                            lastPhis.insert(lastPhi);

                            if (!prover(e, lastPhis, cache, pa)) {
                                ok = false;
                                // delete e;
                                break;
                            }

                            lastPhis.erase(lastPhi);
                            // delete e;
                        }

                        if (ok) {
                            cache[pa] = query->getSubExp2()->clone();
                        }
                    }

                    if (ok) {
                        query = Terminal::get(opTrue);
                    }
                    else {
                        query = Terminal::get(opFalse);
                    }

                    change = true;
                }
                else if (s && s->isAssign()) {
                    if (s && (refsTo.find(s) != refsTo.end())) {
                        LOG << "detected ref loop " << s << "\n";
                        LOG << "refsTo: ";
                        std::set<Instruction *>::iterator ll;

                        for (ll = refsTo.begin(); ll != refsTo.end(); ++ll) {
                            LOG << (*ll)->getNumber() << ", ";
                        }

                        LOG << "\n";
                        assert(false);
                    }
                    else {
                        refsTo.insert(s);
                        query->setSubExp1(((Assign *)s)->getRight()->clone());
                        change = true;
                    }
                }
            }

            // remove memofs from both sides if possible
            if (!change && (query->getSubExp1()->getOper() == opMemOf) && (query->getSubExp2()->getOper() == opMemOf)) {
                query->setSubExp1(query->getSubExp1()->getSubExp1());
                query->setSubExp2(query->getSubExp2()->getSubExp1());
                change = true;
            }

            // is ok if both of the memofs are subscripted with nullptr
            if (!change && (query->getSubExp1()->getOper() == opSubscript) &&
                (query->access<Exp, 1, 1>()->getOper() == opMemOf) && (query->access<RefExp, 1>()->getDef() == nullptr) &&
                (query->access<Exp, 2>()->getOper() == opSubscript) && (query->access<Exp, 2, 1>()->getOper() == opMemOf) &&
                (query->access<RefExp, 2>()->getDef() == nullptr)) {
                query->setSubExp1(query->getSubExp1()->getSubExp1()->getSubExp1());
                query->setSubExp2(query->getSubExp2()->getSubExp1()->getSubExp1());
                change = true;
            }

            // find a memory def for the right if there is a memof on the left
            // FIXME: this seems pretty much like a bad hack!
            if (!change && (query->getSubExp1()->getOper() == opMemOf)) {
                StatementList stmts;
                getStatements(stmts);
                StatementList::iterator it;

                for (it = stmts.begin(); it != stmts.end(); it++) {
                    Assign *s = dynamic_cast<Assign *>(*it);

                    if (s && (*s->getRight() == *query->getSubExp2()) && (s->getLeft()->getOper() == opMemOf)) {
                        query->setSubExp2(s->getLeft()->clone());
                        change = true;
                        break;
                    }
                }
            }

            // last chance, swap left and right if haven't swapped before
            if (!change && !swapped) {
                auto e = query->getSubExp1();
                query->setSubExp1(query->getSubExp2());
                query->setSubExp2(e);
                change  = true;
                swapped = true;
                refsTo.clear();
            }
        }
        else if (query->isIntConst()) {
            auto c = query->access<Const>();
            query = Terminal::get(c->getInt() ? opTrue : opFalse);
        }

        auto old = query->clone();

        auto query_prev = query;
        query = query->clone()->simplify();

        if (change && !(*old == *query) && DEBUG_PROOF) {
            LOG << old << "\n";
        }
    }

    return query->getOper() == opTrue;
}


void UserProc::getDefinitions(LocationSet& ls)
{
    int n = m_signature->getNumReturns();

    for (int j = 0; j < n; j++) {
        ls.insert(m_signature->getReturnExp(j));
    }
}


void Function::addCallers(std::set<UserProc *>& callers)
{
    std::set<CallStatement *>::iterator it;

    for (it = m_callerSet.begin(); it != m_callerSet.end(); it++) {
        UserProc *callerProc = (*it)->getProc();
        callers.insert(callerProc);
    }
}


void UserProc::conTypeAnalysis()
{
    if (DEBUG_TA) {
        LOG << "type analysis for procedure " << getName() << "\n";
    }

    Constraints   consObj;
    LocationSet   cons;
    StatementList stmts;
    getStatements(stmts);

    // For each statement this proc
    int conscript = 0;

    for (auto ss = stmts.begin(); ss != stmts.end(); ss++) {
        cons.clear();
        // So we can co-erce constants:
        conscript = (*ss)->setConscripts(conscript);
        (*ss)->genConstraints(cons);
        consObj.addConstraints(cons);

        if (DEBUG_TA) {
            LOG << (*ss) << "\n" << &cons << "\n";
        }

        // Remove the sizes immediately the constraints are generated.
        // Otherwise, x and x*8* look like different expressions
        (*ss)->stripSizes();
    }

    std::list<ConstraintMap> solns;
    bool ret = consObj.solve(solns);

    if (VERBOSE || DEBUG_TA) {
        if (!ret) {
            LOG << "** could not solve type constraints for proc " << getName() << "!\n";
        }
        else if (solns.size() > 1) {
            LOG << "** " << solns.size() << " solutions to type constraints for proc " << getName() << "!\n";
        }
    }

    std::list<ConstraintMap>::iterator it;
    int solnNum = 0;
    ConstraintMap::iterator cc;

    if (DEBUG_TA) {
        for (it = solns.begin(); it != solns.end(); it++) {
            LOG << "solution " << ++solnNum << " for proc " << getName() << "\n";
            ConstraintMap& cm = *it;

            for (cc = cm.begin(); cc != cm.end(); cc++) {
                LOG << cc->first << " = " << cc->second << "\n";
            }

            LOG << "\n";
        }
    }

    // Just use the first solution, if there is one
    Prog *_prog = getProg();

    if (!solns.empty()) {
        ConstraintMap& cm = *solns.begin();

        for (cc = cm.begin(); cc != cm.end(); cc++) {
            // Ick. A workaround for now (see test/pentium/sumarray-O4)
            // assert(cc->first->isTypeOf());
            if (!cc->first->isTypeOf()) {
                continue;
            }

            auto loc = cc->first->getSubExp1();
            assert(cc->second->isTypeVal());
            SharedType ty = cc->second->access<TypeVal>()->getType();

            if (loc->isSubscript()) {
                loc = loc->getSubExp1();
            }

            if (loc->isGlobal()) {
                QString nam = loc->access<Const, 1>()->getStr();

                if (!ty->resolvesToVoid()) {
                    _prog->setGlobalType(nam, ty->clone());
                }
            }
            else if (loc->isLocal()) {
                QString nam = loc->access<Const, 1>()->getStr();
                setLocalType(nam, ty);
            }
            else if (loc->isIntConst()) {
                auto con = loc->access<Const>();
                int  val = con->getInt();

                if (ty->isFloat()) {
                    // Need heavy duty cast here
                    // MVE: check this! Especially when a double prec float
                    con->setFlt(*(float *)&val);
                    con->setOper(opFltConst);
                }
                else if (ty->isCString()) {
                    // Convert to a string
                    const char *str = _prog->getStringConstant(Address(val), true);

                    if (str) {
                        // Make a string
                        con->setStr(str);
                        con->setOper(opStrConst);
                    }
                }
                else {
                    if (ty->isInteger() && ty->getSize() && (ty->getSize() != STD_SIZE)) {
                        // Wrap the constant in a TypedExp (for a cast)
                        castConst(con->getConscript(), ty);
                    }
                }
            }
        }
    }

    // Clear the conscripts. These confuse the fromSSA logic, causing infinite
    // loops
    for (auto ss = stmts.begin(); ss != stmts.end(); ss++) {
        (*ss)->clearConscripts();
    }
}


bool UserProc::searchAndReplace(const Exp& search, SharedExp replace)
{
    bool          ch = false;
    StatementList stmts;

    getStatements(stmts);
    StatementList::iterator it;

    for (it = stmts.begin(); it != stmts.end(); it++) {
        Instruction *s = *it;
        ch |= s->searchAndReplace(search, replace);
    }

    return ch;
}


unsigned fudge(StatementList::iterator x)
{
    StatementList::iterator y = x;
    return *(unsigned *)&y;
}


SharedExp UserProc::getProven(SharedExp left)
{
    // Note: proven information is in the form r28 mapsto (r28 + 4)
    std::map<SharedExp, SharedExp, lessExpStar>::iterator it = m_provenTrue.find(left);

    if (it != m_provenTrue.end()) {
        return it->second;
    }

    //     not found, try the signature
    // No! The below should only be for library functions!
    // return signature->getProven(left);
    return nullptr;
}


SharedExp UserProc::getPremised(SharedExp left)
{
    std::map<SharedExp, SharedExp, lessExpStar>::iterator it = m_recurPremises.find(left);

    if (it != m_recurPremises.end()) {
        return it->second;
    }

    return nullptr;
}


bool UserProc::isPreserved(SharedExp e)
{
    return m_provenTrue.find(e) != m_provenTrue.end() && *m_provenTrue[e] == *e;
}


void UserProc::castConst(int num, SharedType ty)
{
    StatementList stmts;

    getStatements(stmts);
    StatementList::iterator it;

    for (it = stmts.begin(); it != stmts.end(); it++) {
        if ((*it)->castConst(num, ty)) {
            break;
        }
    }
}


bool UserProc::ellipsisProcessing()
{
    BBIterator it;

    BasicBlock::rtlrit              rrit;
    StatementList::reverse_iterator srit;
    bool ch = false;

    for (it = m_cfg->begin(); it != m_cfg->end(); ++it) {
        CallStatement *c = dynamic_cast<CallStatement *>((*it)->getLastStmt(rrit, srit));

        // Note: we may have removed some statements, so there may no longer be a last statement!
        if (c == nullptr) {
            continue;
        }

        ch |= c->ellipsisProcessing(m_prog);
    }

    if (ch) {
        fixCallAndPhiRefs();
    }

    return ch;
}


void UserProc::addImplicitAssigns()
{
    Boomerang::get()->alertDecompileDebugPoint(this, "before adding implicit assigns");

    StatementList stmts;
    getStatements(stmts);
    ImplicitConverter     ic(m_cfg);
    StmtImplicitConverter sm(&ic, m_cfg);

    for (auto it = stmts.begin(); it != stmts.end(); it++) {
        (*it)->accept(&sm);
    }

    m_cfg->setImplicitsDone();
    m_df.convertImplicits(m_cfg); // Some maps have m[...]{-} need to be m[...]{0} now
    makeSymbolsImplicit();
    // makeParamsImplicit();            // Not necessary yet, since registers are not yet mapped

    Boomerang::get()->alertDecompileDebugPoint(this, "after adding implicit assigns");
}


QString UserProc::lookupParam(SharedExp e)
{
    // Originally e.g. m[esp+K]
    Instruction *def = m_cfg->findTheImplicitAssign(e);

    if (def == nullptr) {
        LOG_ERROR("No implicit definition for parameter %1!", e);
        return QString::null;
    }

    auto       re = RefExp::get(e, def);
    SharedType ty = def->getTypeFor(e);
    return lookupSym(re, ty);
}


QString UserProc::lookupSymFromRef(const std::shared_ptr<RefExp>& r)
{
    Instruction *def = r->getDef();

    if (!def) {
        qDebug() << "UserProc::lookupSymFromRefAny null def";
        return QString::null;
    }

    auto       base = r->getSubExp1();
    SharedType ty   = def->getTypeFor(base);
    return lookupSym(r, ty);
}


QString UserProc::lookupSymFromRefAny(const std::shared_ptr<RefExp>& r)
{
    Instruction *def = r->getDef();

    if (!def) {
        qDebug() << "UserProc::lookupSymFromRefAny null def";
        return QString::null;
    }

    SharedExp  base = r->getSubExp1();
    SharedType ty   = def->getTypeFor(base);
    QString    ret  = lookupSym(r, ty);

    if (!ret.isNull()) {
        return ret;             // Found a specific symbol
    }

    return lookupSym(base, ty); // Check for a general symbol
}


QString UserProc::lookupSym(const SharedConstExp& arg, SharedType ty)
{
    SharedConstExp e = arg;

    if (arg->isTypedExp()) {
        e = e->getSubExp1();
    }

    SymbolMap::iterator it;
    it = m_symbolMap.find(e);

    while (it != m_symbolMap.end() && *it->first == *e) {
        auto sym = it->second;
        assert(sym->isLocal() || sym->isParam());
        QString    name = sym->access<Const, 1>()->getStr();
        SharedType type = getLocalType(name);

        if (type == nullptr) {
            type = getParamType(name); // Ick currently linear search
        }

        if (type && type->isCompatibleWith(*ty)) {
            return name;
        }

        ++it;
    }

    // Else there is no symbol
    return QString::null;
}


void UserProc::printSymbolMap(QTextStream& out, bool html /*= false*/) const
{
    if (html) {
        out << "<br>";
    }

    out << "symbols:\n";

    for (const std::pair<SharedConstExp, SharedExp>& it : m_symbolMap) {
        const SharedType ty = getTypeForLocation(it.second);
        out << "  " << it.first << " maps to " << it.second << " type " << (ty ? qPrintable(ty->getCtype()) : "nullptr") << "\n";

        if (html) {
            out << "<br>";
        }
    }

    if (html) {
        out << "<br>";
    }

    out << "end symbols\n";
}


void UserProc::dumpLocals(QTextStream& os, bool html) const
{
    if (html) {
        os << "<br>";
    }

    os << "locals:\n";

    for (const std::pair<QString, SharedType>& local_entry : m_locals) {
        os << local_entry.second->getCtype() << " " << local_entry.first << " ";
        SharedConstExp e = expFromSymbol(local_entry.first);

        // Beware: for some locals, expFromSymbol() returns nullptr (? No longer?)
        if (e) {
            os << e << "\n";
        }
        else {
            os << "-\n";
        }
    }

    if (html) {
        os << "<br>";
    }

    os << "end locals\n";
}


void UserProc::dumpSymbolMap() const
{
    for (auto it = m_symbolMap.begin(); it != m_symbolMap.end(); it++) {
        SharedType ty = getTypeForLocation(it->second);
        LOG_STREAM_OLD() << "  " << it->first << " maps to " << it->second << " type " << (ty ? qPrintable(ty->getCtype()) : "NULL")
                     << "\n";
    }
}


void UserProc::dumpSymbolMapx() const
{
    for (auto it = m_symbolMap.begin(); it != m_symbolMap.end(); it++) {
        SharedType ty = getTypeForLocation(it->second);
        LOG_STREAM_OLD() << "  " << it->first << " maps to " << it->second << " type " << (ty ? qPrintable(ty->getCtype()) : "NULL")
                     << "\n";
        it->first->printx(2);
    }
}


void UserProc::testSymbolMap() const
{
    SymbolMap::const_iterator it1, it2;
    bool OK = true;
    it1 = m_symbolMap.begin();

    if (it1 != m_symbolMap.end()) {
        it2 = it1;
        ++it2;

        while (it2 != m_symbolMap.end()) {
            if (*it2->first < *it1->first) { // Compare keys
                OK = false;
                LOG_STREAM_OLD() << "*it2->first < *it1->first: " << it2->first << " < " << it1->first << "!\n";
                // it2->first->printx(0); it1->first->printx(5);
            }

            ++it1;
            ++it2;
        }
    }

    LOG_STREAM_OLD() << "Symbolmap is " << (OK ? "OK" : "NOT OK!!!!!") << "\n";
}


void UserProc::dumpLocals() const
{
    QTextStream q_cerr(stderr);

    dumpLocals(q_cerr);
}


void UserProc::updateArguments()
{
    Boomerang::get()->alertDecompiling(this);
    LOG_VERBOSE("### Update arguments for %1 ###", getName());
    Boomerang::get()->alertDecompileDebugPoint(this, "Before updating arguments");
    BasicBlock::rtlrit              rrit;
    StatementList::reverse_iterator srit;

    for (BasicBlock *it : *m_cfg) {
        CallStatement *c = dynamic_cast<CallStatement *>(it->getLastStmt(rrit, srit));

        // Note: we may have removed some statements, so there may no longer be a last statement!
        if (c == nullptr) {
            continue;
        }

        c->updateArguments();
        // c->bypass();
        LOG_VERBOSE("%1", c);
    }

    LOG_VERBOSE("=== End update arguments for %1", getName());
    Boomerang::get()->alertDecompileDebugPoint(this, "After updating arguments");
}


void UserProc::updateCallDefines()
{
    LOG_VERBOSE("### Update call defines for % ###", getName());

    StatementList stmts;
    getStatements(stmts);
    StatementList::iterator it;

    for (it = stmts.begin(); it != stmts.end(); it++) {
        CallStatement *call = dynamic_cast<CallStatement *>(*it);

        if (call == nullptr) {
            continue;
        }

        call->updateDefines();
    }
}


void UserProc::replaceSimpleGlobalConstants()
{
    LOG_VERBOSE("### Replace simple global constants for %1 ###", getName());

    StatementList stmts;
    getStatements(stmts);
    StatementList::iterator it;

    for (Instruction *st : stmts) {
        Assign *assgn = dynamic_cast<Assign *>(st);

        if (assgn == nullptr) {
            continue;
        }

        if (!assgn->getRight()->isMemOf()) {
            continue;
        }

        if (!assgn->getRight()->getSubExp1()->isIntConst()) {
            continue;
        }

        Address addr = assgn->getRight()->access<Const, 1>()->getAddr();
        LOG_VERBOSE("Assign %1");

        if (m_prog->isReadOnly(addr)) {
            LOG_VERBOSE("is readonly");
            int val = 0;

            switch (assgn->getType()->getSize())
            {
            case 8:
                val = m_prog->readNative1(addr);
                break;

            case 16:
                val = m_prog->readNative2(addr);
                break;

            case 32:
                val = m_prog->readNative4(addr);
                break;

            default:
                assert(false);
            }

            assgn->setRight(Const::get(val));
        }
    }
}


void UserProc::reverseStrengthReduction()
{
    Boomerang::get()->alertDecompileDebugPoint(this, "Before reversing strength reduction");

    StatementList stmts;
    getStatements(stmts);
    StatementList::iterator it;

    for (it = stmts.begin(); it != stmts.end(); it++) {
        if ((*it)->isAssign()) {
            Assign *as = (Assign *)*it;

            // of the form x = x{p} + c
            if ((as->getRight()->getOper() == opPlus) && as->getRight()->getSubExp1()->isSubscript() &&
                (*as->getLeft() == *as->getRight()->getSubExp1()->getSubExp1()) &&
                as->getRight()->getSubExp2()->isIntConst()) {
                int  c = as->getRight()->access<Const, 2>()->getInt();
                auto r = as->getRight()->access<RefExp, 1>();

                if (r->getDef() && r->getDef()->isPhi()) {
                    PhiAssign *p = (PhiAssign *)r->getDef();

                    if (p->getNumDefs() == 2) {
                        Instruction *first  = p->front().def();
                        Instruction *second = p->back().def();

                        if (first == as) {
                            // want the increment in second
                            std::swap(first, second);
                        }

                        // first must be of form x := 0
                        if (first && first->isAssign() && ((Assign *)first)->getRight()->isIntConst() &&
                            ((((Assign *)first)->getRight())->access<Const>()->getInt() == 0)) {
                            // ok, fun, now we need to find every reference to p and
                            // replace with x{p} * c
                            StatementList stmts2;
                            getStatements(stmts2);
                            StatementList::iterator it2;

                            for (it2 = stmts2.begin(); it2 != stmts2.end(); it2++) {
                                if (*it2 != as) {
                                    (*it2)->searchAndReplace(*r, Binary::get(opMult, r->clone(), Const::get(c)));
                                }
                            }

                            // that done we can replace c with 1 in as
                            as->getRight()->access<Const, 2>()->setInt(1);
                        }
                    }
                }
            }
        }
    }

    Boomerang::get()->alertDecompileDebugPoint(this, "After reversing strength reduction");
}


void UserProc::insertParameter(SharedExp e, SharedType ty)
{
    if (filterParams(e)) {
        return; // Filtered out
    }

    // Used to filter out preserved locations here: no! Propagation and dead code elimination solve the problem.
    // See test/pentium/restoredparam for an example where you must not remove restored locations

    // Wrap it in an implicit assignment; DFA based TA should update the type later
    ImplicitAssign *as = new ImplicitAssign(ty->clone(), e->clone());
    // Insert as, in order, into the existing set of parameters
    StatementList::iterator nn;
    bool inserted = false;

    for (nn = m_parameters.begin(); nn != m_parameters.end(); ++nn) {
        // If the new assignment is less than the current one ...
        if (m_signature->argumentCompare(*as, *(Assignment *)*nn)) {
            nn       = m_parameters.insert(nn, as); // ... then insert before this position
            inserted = true;
            break;
        }
    }

    if (!inserted) {
        m_parameters.insert(m_parameters.end(), as); // In case larger than all existing elements
    }

    // update the signature
    m_signature->setNumParams(0);
    int i = 1;

    for (nn = m_parameters.begin(); nn != m_parameters.end(); ++nn, ++i) {
        Assignment *a = (Assignment *)*nn;
        char       tmp[20];
        sprintf(tmp, "param%i", i);
        m_signature->addParameter(a->getType(), tmp, a->getLeft());
    }
}


bool UserProc::filterReturns(SharedExp e)
{
    if (isPreserved(e)) {
        // If it is preserved, then it can't be a return (since we don't change it)
        return true;
    }

    switch (e->getOper())
    {
    case opPC:
        return true; // Ignore %pc

    case opDefineAll:
        return true; // Ignore <all>

    case opTemp:
        return true; // Ignore all temps (should be local to one instruction)

    // Would like to handle at least %ZF, %CF one day. For now, filter them out
    case opZF:
    case opCF:
    case opFlags:
        return true;

    case opMemOf:
        // return signature->isStackLocal(prog, e);        // Filter out local variables
        // Actually, surely all sensible architectures will only every return in registers. So for now, just
        // filter out all mem-ofs
        return true;

    case opGlobal:
        return true; // Never return in globals

    default:
        return false;
    }
}


bool UserProc::filterParams(SharedExp e)
{
    switch (e->getOper())
    {
    case opPC:
        return true;

    case opTemp:
        return true;

    case opRegOf:
        {
            int sp = 999;

            if (m_signature) {
                sp = m_signature->getStackRegister(m_prog);
            }

            int r = e->access<Const, 1>()->getInt();
            return r == sp;
        }

    case opMemOf:
        {
            auto addr = e->getSubExp1();

            if (addr->isIntConst()) {
                return true; // Global memory location
            }

            if (addr->isSubscript() && addr->access<RefExp>()->isImplicitDef()) {
                auto reg = addr->getSubExp1();
                int  sp  = 999;

                if (m_signature) {
                    sp = m_signature->getStackRegister(m_prog);
                }

                if (reg->isRegN(sp)) {
                    return true; // Filter out m[sp{-}] assuming it is the return address
                }
            }

            return false; // Might be some weird memory expression that is not a local
        }

    case opGlobal:
        return true; // Never use globals as argument locations (could appear on RHS of args)

    default:
        return false;
    }
}


QString UserProc::findLocal(const SharedExp& e, SharedType ty)
{
    if (e->isLocal()) {
        return e->access<Const, 1>()->getStr();
    }

    // Look it up in the symbol map
    QString name = lookupSym(e, ty);

    if (name.isNull()) {
        return name;
    }

    // Now make sure it is a local; some symbols (e.g. parameters) are in the symbol map but not locals
    if (m_locals.find(name) != m_locals.end()) {
        return name;
    }

    return QString::null;
}


QString UserProc::findLocalFromRef(const std::shared_ptr<RefExp>& r)
{
    Instruction *def = r->getDef();
    SharedExp   base = r->getSubExp1();
    SharedType  ty   = def->getTypeFor(base);
    // QString name = lookupSym(*base, ty); ?? this actually worked a bit
    QString name = lookupSym(r, ty);

    if (name.isNull()) {
        return name;
    }

    // Now make sure it is a local; some symbols (e.g. parameters) are in the symbol map but not locals
    if (m_locals.find(name) != m_locals.end()) {
        return name;
    }

    return QString::null;
}


QString UserProc::findFirstSymbol(const SharedExp& e)
{
    SymbolMap::iterator ff = m_symbolMap.find(e);

    if (ff == m_symbolMap.end()) {
        return QString::null;
    }

    return std::static_pointer_cast<Const>(ff->second->getSubExp1())->getStr();
}

// Perform call and phi statement bypassing at depth d <- missing
void UserProc::fixCallAndPhiRefs()
{
    /* Algorithm:
    *      for each statement s in this proc
    *        if s is a phi statement ps
    *              let r be a ref made up of lhs and s
    *              for each parameter p of ps
    *                if p == r                        // e.g. test/pentium/fromssa2 r28{56}
    *                      remove p from ps
    *              let lhs be left hand side of ps
    *              allSame = true
    *              let first be a ref built from first p
    *              do bypass but not propagation on first
    *              if result is of the form lhs{x}
    *                replace first with x
    *              for each parameter p of ps after the first
    *                let current be a ref built from p
    *                do bypass but not propagation on current
    *                if result is of form lhs{x}
    *                      replace cur with x
    *                if first != current
    *                      allSame = false
    *              if allSame
    *                let best be ref built from the "best" parameter p in ps ({-} better than {assign} better than {call})
    *                replace ps with an assignment lhs := best
    *      else (ordinary statement)
    *        do bypass and propagation for s
    */
    LOG_VERBOSE("### Start fix call and phi bypass analysis for %1 ###", getName());

    Boomerang::get()->alertDecompileDebugPoint(this, "Before fixing call and phi refs");

    std::map<SharedExp, int, lessExpStar> destCounts;
    StatementList::iterator               it;
    Instruction   *s;
    StatementList stmts;
    getStatements(stmts);

    // a[m[]] hack, aint nothing better.
    bool found = true;

    for (it = stmts.begin(); it != stmts.end(); it++) {
        if (!(*it)->isCall()) {
            continue;
        }

        CallStatement *call = (CallStatement *)*it;

        for (auto& elem : call->getArguments()) {
            Assign *a = (Assign *)elem;

            if (!a->getType()->resolvesToPointer()) {
                continue;
            }

            SharedExp e = a->getRight();

            if ((e->getOper() == opPlus) || (e->getOper() == opMinus)) {
                if (e->getSubExp2()->isIntConst()) {
                    if (e->getSubExp1()->isSubscript() &&
                        e->getSubExp1()->getSubExp1()->isRegN(m_signature->getStackRegister()) &&
                        (((e->access<RefExp, 1>())->getDef() == nullptr) ||
                         (e->access<RefExp, 1>())->getDef()->isImplicit())) {
                        a->setRight(Unary::get(opAddrOf, Location::memOf(e->clone())));
                        found = true;
                    }
                }
            }
        }
    }

    if (found) {
        doRenameBlockVars(2);
    }

    // Scan for situations like this:
    // 56 r28 := phi{6, 26}
    // ...
    // 26 r28 := r28{56}
    // So we can remove the second parameter, then reduce the phi to an assignment, then propagate it
    for (it = stmts.begin(); it != stmts.end(); it++) {
        s = *it;

        if (!s->isPhi()) {
            continue;
        }

        PhiAssign *ps = (PhiAssign *)s;
        auto      r   = RefExp::get(ps->getLeft(), ps);

        for (PhiAssign::iterator pi = ps->begin(); pi != ps->end(); ) {
            const PhiInfo& p(pi->second);
            assert(p.e);
            Instruction *def    = (Instruction *)p.def();
            auto        current = RefExp::get(p.e, def);

            if (*current == *r) {   // Will we ever see this?
                pi = ps->erase(pi); // Erase this phi parameter
                continue;
            }

            // Chase the definition
            if (def && def->isAssign()) {
                auto rhs = ((Assign *)def)->getRight();

                if (*rhs == *r) {       // Check if RHS is a single reference to ps
                    pi = ps->erase(pi); // Yes, erase this phi parameter
                    continue;
                }
            }

            ++pi;
        }
    }

    // Second pass
    for (it = stmts.begin(); it != stmts.end(); it++) {
        s = *it;

        if (!s->isPhi()) { // Ordinary statement
            s->bypass();
            continue;
        }

        PhiAssign *ps = (PhiAssign *)s;

        if (ps->getNumDefs() == 0) {
            continue; // Can happen e.g. for m[...] := phi {} when this proc is
        }

        // involved in a recursion group
        auto lhs     = ps->getLeft();
        bool allSame = true;
        // Let first be a reference built from the first parameter
        PhiAssign::iterator phi_iter = ps->begin();

        while (phi_iter->second.e == nullptr && phi_iter != ps->end()) {
            ++phi_iter;                // Skip any null parameters
        }

        assert(phi_iter != ps->end()); // Should have been deleted
        PhiInfo&  phi_inf(phi_iter->second);
        SharedExp first = RefExp::get(phi_inf.e, phi_inf.def());
        // bypass to first
        CallBypasser cb(ps);
        first = first->accept(&cb);

        if (cb.isTopChanged()) {
            first = first->simplify();
        }

        first = first->propagateAll(); // Propagate everything repeatedly

        if (cb.isMod()) {              // Modified?
            // if first is of the form lhs{x}
            if (first->isSubscript() && (*first->getSubExp1() == *lhs)) {
                // replace first with x
                phi_inf.def(first->access<RefExp>()->getDef());
            }
        }

        // For each parameter p of ps after the first
        for (++phi_iter; phi_iter != ps->end(); ++phi_iter) {
            assert(phi_iter->second.e);
            PhiInfo&     phi_inf2(phi_iter->second);
            SharedExp    current = RefExp::get(phi_inf2.e, phi_inf2.def());
            CallBypasser cb2(ps);
            current = current->accept(&cb2);

            if (cb2.isTopChanged()) {
                current = current->simplify();
            }

            current = current->propagateAll();

            if (cb2.isMod()) { // Modified?
                // if current is of the form lhs{x}
                if (current->isSubscript() && (*current->getSubExp1() == *lhs)) {
                    // replace current with x
                    phi_inf2.def(current->access<RefExp>()->getDef());
                }
            }

            if (!(*first == *current)) {
                allSame = false;
            }
        }

        if (allSame) {
            // let best be ref built from the "best" parameter p in ps ({-} better than {assign} better than {call})
            phi_iter = ps->begin();

            while (phi_iter->second.e == nullptr && phi_iter != ps->end()) {
                ++phi_iter;                // Skip any null parameters
            }

            assert(phi_iter != ps->end()); // Should have been deleted
            auto best = RefExp::get(phi_iter->second.e, phi_iter->second.def());

            for (++phi_iter; phi_iter != ps->end(); ++phi_iter) {
                assert(phi_iter->second.e);
                auto current = RefExp::get(phi_iter->second.e, phi_iter->second.def());

                if (current->isImplicitDef()) {
                    best = current;
                    break;
                }

                if (phi_iter->second.def()->isAssign()) {
                    best = current;
                }

                // If phi_iter->second.def is a call, this is the worst case; keep only (via first)
                // if all parameters are calls
            }

            ps->convertToAssign(best);
            LOG_VERBOSE("Redundant phi replaced with copy assign; now %1", ps);
        }
    }

    // Also do xxx in m[xxx] in the use collector
    for (const SharedExp& cc : m_procUseCollector) {
        if (!cc->isMemOf()) {
            continue;
        }

        auto         addr = cc->getSubExp1();
        CallBypasser cb(nullptr);
        addr = addr->accept(&cb);

        if (cb.isMod()) {
            cc->setSubExp1(addr);
        }
    }

    LOG_VERBOSE("### End fix call and phi bypass analysis for %1 ###", getName());

    Boomerang::get()->alertDecompileDebugPoint(this, "after fixing call and phi refs");
}


void UserProc::markAsNonChildless(const std::shared_ptr<ProcSet>& cs)
{
    BasicBlock::rtlrit              rrit;
    StatementList::reverse_iterator srit;

    for (BasicBlock *bb : *m_cfg) {
        CallStatement *c = dynamic_cast<CallStatement *>(bb->getLastStmt(rrit, srit));

        if (c && c->isChildless()) {
            UserProc *dest = (UserProc *)c->getDestProc();

            if (cs->find(dest) != cs->end()) { // Part of the cycle?
                // Yes, set the callee return statement (making it non childless)
                c->setCalleeReturn(dest->getTheReturnStatement());
            }
        }
    }
}


void UserProc::propagateToCollector()
{
    UseCollector::iterator it;

    for (it = m_procUseCollector.begin(); it != m_procUseCollector.end(); ) {
        if (!(*it)->isMemOf()) {
            ++it;
            continue;
        }

        auto        addr = (*it)->getSubExp1();
        LocationSet used;
        addr->addUsedLocs(used);

        for (const SharedExp& v : used) {
            if (!v->isSubscript()) {
                continue;
            }

            auto   r   = v->access<RefExp>();
            Assign *as = (Assign *)r->getDef();

            if ((as == nullptr) || !as->isAssign()) {
                continue;
            }

            bool ch;
            auto res = addr->clone()->searchReplaceAll(*r, as->getRight(), ch);

            if (!ch) {
                continue; // No change
            }

            auto memOfRes = Location::memOf(res)->simplify();

            // First check to see if memOfRes is already in the set
            if (m_procUseCollector.exists(memOfRes)) {
                // Take care not to use an iterator to the newly erased element.
                /* it = */
                m_procUseCollector.remove(it++);            // Already exists; just remove the old one
                continue;
            }
            else {
                LOG_VERBOSE("Propagating %1 to %2 in collector; result %3",
                            r, as->getRight(), memOfRes);
                (*it)->setSubExp1(res); // Change the child of the memof
            }
        }

        ++it; // it is iterated either with the erase, or the continue, or here
    }
}


void UserProc::initialParameters()
{
    LOG_VERBOSE("### Initial parameters for %1", getName());
    m_parameters.clear();

    for (const SharedExp& v : m_procUseCollector) {
        m_parameters.append(new ImplicitAssign(v->clone()));
    }

    if (VERBOSE) {
        QString     tgt;
        QTextStream ost(&tgt);
        printParams(ost);
        LOG << tgt;
    }
}


bool UserProc::inductivePreservation(UserProc * /*topOfCycle*/)
{
    // FIXME: This is not correct in general!! It should work OK for self recursion,
    // but not for general mutual recursion. Not that hard, just not done yet.
    return true;
}


bool UserProc::isLocal(SharedExp e) const
{
    if (!e->isMemOf()) {
        return false; // Don't want say a register
    }

    SymbolMap::const_iterator ff = m_symbolMap.find(e);

    if (ff == m_symbolMap.end()) {
        return false;
    }

    SharedExp mapTo = ff->second;
    return mapTo->isLocal();
}


bool UserProc::isPropagatable(const SharedExp& e) const
{
    if (m_addressEscapedVars.exists(e)) {
        return false;
    }

    return isLocalOrParam(e);
}


bool UserProc::isLocalOrParam(const SharedExp& e) const
{
    if (isLocal(e)) {
        return true;
    }

    return m_parameters.existsOnLeft(e);
}


bool UserProc::isLocalOrParamPattern(const SharedExp& e)
{
    if (!e->isMemOf()) {
        return false; // Don't want say a register
    }

    SharedExp addr = e->getSubExp1();

    if (!m_signature->isPromoted()) {
        return false; // Prevent an assert failure if using -E
    }

    int               sp = m_signature->getStackRegister();
    static const auto initSp(RefExp::get(Location::regOf(sp), nullptr)); // sp{-}

    if (*addr == *initSp) {
        return true; // Accept m[sp{-}]
    }

    if (addr->getArity() != 2) {
        return false; // Require sp +/- K
    }

    OPER op = addr->getOper();

    if ((op != opPlus) && (op != opMinus)) {
        return false;
    }

    SharedExp left = addr->getSubExp1();

    if (!(*left == *initSp)) {
        return false;
    }

    SharedExp right = addr->getSubExp2();
    return right->isIntConst();
}


bool UserProc::doesParamChainToCall(SharedExp param, UserProc *p, ProcSet *visited)
{
    BasicBlock::rtlrit              rrit;
    StatementList::reverse_iterator srit;

    for (BasicBlock *pb : *m_cfg) {
        CallStatement *c = (CallStatement *)pb->getLastStmt(rrit, srit);

        if ((c == nullptr) || !c->isCall()) {
            continue; // Only interested in calls
        }

        UserProc *dest = (UserProc *)c->getDestProc();

        if ((dest == nullptr) || dest->isLib()) {
            continue;    // Only interested in calls to UserProcs
        }

        if (dest == p) { // Pointer comparison is OK here
            // This is a recursive call to p. Check for an argument of the form param{-} FIXME: should be looking for
            // component
            StatementList&          args = c->getArguments();
            StatementList::iterator aa;

            for (aa = args.begin(); aa != args.end(); ++aa) {
                SharedExp rhs = ((Assign *)*aa)->getRight();

                if (rhs && rhs->isSubscript() && rhs->access<RefExp>()->isImplicitDef()) {
                    SharedExp base = rhs->getSubExp1();

                    // Check if this argument location matches loc
                    if (*base == *param) {
                        // We have a call to p that takes param{-} as an argument
                        return true;
                    }
                }
            }
        }
        else {
            if (dest->doesRecurseTo(p)) {
                // We have come to a call that is not to p, but is in the same recursion group as p and this proc.
                visited->insert(this);

                if (visited->find(dest) != visited->end()) {
                    // Recurse to the next proc
                    bool res = dest->doesParamChainToCall(param, p, visited);

                    if (res) {
                        return true;
                    }

                    // TODO: Else consider more calls this proc
                }
            }
        }
    }

    return false;
}


bool UserProc::isRetNonFakeUsed(CallStatement *c, SharedExp retLoc, UserProc *p, ProcSet *visited)
{
    // Ick! This algorithm has to search every statement for uses of the return location retLoc defined at call c that
    // are not arguments of calls to p. If we had def-use information, it would be much more efficient
    StatementList stmts;

    getStatements(stmts);
    StatementList::iterator it;

    for (it = stmts.begin(); it != stmts.end(); it++) {
        Instruction *s = *it;
        LocationSet ls;
        s->addUsedLocs(ls);
        bool found = false;

        for (const SharedExp& ll : ls) {
            if (!ll->isSubscript()) {
                continue;
            }

            Instruction *def = ll->access<RefExp>()->getDef();

            if (def != c) {
                continue; // Not defined at c, ignore
            }

            SharedExp base = ll->getSubExp1();

            if (!(*base == *retLoc)) {
                continue; // Defined at c, but not the right location
            }

            found = true;
            break;
        }

        if (!found) {
            continue;
        }

        if (!s->isCall()) {
            // This non-call uses the return; return true as it is non-fake used
            return true;
        }

        UserProc *dest = (UserProc *)((CallStatement *)s)->getDestProc();

        if (dest == nullptr) {
            // This childless call seems to use the return. Count it as a non-fake use
            return true;
        }

        if (dest == p) {
            // This procedure uses the parameter, but it's a recursive call to p, so ignore it
            continue;
        }

        if (dest->isLib()) {
            // Can't be a recursive call
            return true;
        }

        if (!dest->doesRecurseTo(p)) {
            return true;
        }

        // We have a call that uses the return, but it may well recurse to p
        visited->insert(this);

        if (visited->find(dest) != visited->end()) {
            // We've not found any way for loc to be fake-used. Count it as non-fake
            return true;
        }

        if (!doesParamChainToCall(retLoc, p, visited)) {
            // It is a recursive call, but it doesn't end up passing param as an argument in a call to p
            return true;
        }
    }

    return false;
}


bool UserProc::checkForGainfulUse(SharedExp bparam, ProcSet& visited)
{
    visited.insert(this); // Prevent infinite recursion
    StatementList::iterator pp;
    StatementList           stmts;
    getStatements(stmts);
    StatementList::iterator it;

    for (it = stmts.begin(); it != stmts.end(); it++) {
        Instruction *s = *it;

        // Special checking for recursive calls
        if (s->isCall()) {
            CallStatement *c    = (CallStatement *)s;
            UserProc      *dest = dynamic_cast<UserProc *>(c->getDestProc());

            if (dest && dest->doesRecurseTo(this)) {
                // In the destination expression?
                LocationSet u;
                c->getDest()->addUsedLocs(u);

                if (u.existsImplicit(bparam)) {
                    return true; // Used by the destination expression
                }

                // Else check for arguments of the form lloc := f(bparam{0})
                StatementList&          args = c->getArguments();
                StatementList::iterator aa;

                for (aa = args.begin(); aa != args.end(); ++aa) {
                    SharedExp   rhs = ((Assign *)*aa)->getRight();
                    LocationSet argUses;
                    rhs->addUsedLocs(argUses);

                    if (argUses.existsImplicit(bparam)) {
                        SharedExp lloc = ((Assign *)*aa)->getLeft();

                        if ((visited.find(dest) == visited.end()) && dest->checkForGainfulUse(lloc, visited)) {
                            return true;
                        }
                    }
                }

                // If get to here, then none of the arguments is of this form, and we can ignore this call
                continue;
            }
        }
        else if (s->isReturn()) {
            if (m_cycleGroup && m_cycleGroup->size()) { // If this function is involved in recursion
                continue;                               //  then ignore this return statement
            }
        }
        else if (s->isPhi() && (theReturnStatement != nullptr) && m_cycleGroup && m_cycleGroup->size()) {
            SharedExp                 phiLeft = ((PhiAssign *)s)->getLeft();
            auto                      refPhi  = RefExp::get(phiLeft, s);
            ReturnStatement::iterator rr;
            bool                      foundPhi = false;

            for (rr = theReturnStatement->begin(); rr != theReturnStatement->end(); ++rr) {
                SharedExp   rhs = ((Assign *)*rr)->getRight();
                LocationSet uses;
                rhs->addUsedLocs(uses);

                if (uses.exists(refPhi)) {
                    // s is a phi that defines a component of a recursive return. Ignore it
                    foundPhi = true;
                    break;
                }
            }

            if (foundPhi) {
                continue; // Ignore this phi
            }
        }

        // Otherwise, consider uses in s
        LocationSet uses;
        s->addUsedLocs(uses);

        if (uses.existsImplicit(bparam)) {
            return true; // A gainful use
        }
    }                    // for each statement s

    return false;
}


bool UserProc::removeRedundantParameters()
{
    if (m_signature->isForced()) {
        // Assume that no extra parameters would have been inserted... not sure always valid
        return false;
    }

    bool          ret = false;
    StatementList newParameters;

    Boomerang::get()->alertDecompileDebugPoint(this, "Before removing redundant parameters");

    if (DEBUG_UNUSED) {
        LOG << "%%% removing unused parameters for " << getName() << "\n";
    }

    // Note: this would be far more efficient if we had def-use information
    StatementList::iterator pp;

    for (pp = m_parameters.begin(); pp != m_parameters.end(); ++pp) {
        SharedExp param = ((Assignment *)*pp)->getLeft();
        bool      az;
        SharedExp bparam = param->clone()->removeSubscripts(az); // FIXME: why does main have subscripts on parameters?
        // Memory parameters will be of the form m[sp + K]; convert to m[sp{0} + K] as will be found in uses
        bparam = bparam->expSubscriptAllNull();                  // Now m[sp{-}+K]{-}
        ImplicitConverter ic(m_cfg);
        bparam = bparam->accept(&ic);                            // Now m[sp{0}+K]{0}
        assert(bparam->isSubscript());
        bparam = bparam->access<Exp, 1>();                       // now m[sp{0}+K] (bare parameter)

        ProcSet visited;

        if (checkForGainfulUse(bparam, visited)) {
            newParameters.append(*pp); // Keep this parameter
        }
        else {
            // Remove the parameter
            ret = true;

            if (DEBUG_UNUSED) {
                LOG << " %%% removing unused parameter " << param << " in " << getName() << "\n";
            }

            // Check if it is in the symbol map. If so, delete it; a local will be created later
            SymbolMap::iterator ss = m_symbolMap.find(param);

            if (ss != m_symbolMap.end()) {
                m_symbolMap.erase(ss);           // Kill the symbol
            }

            m_signature->removeParameter(param); // Also remove from the signature
            m_cfg->removeImplicitAssign(param);  // Remove the implicit assignment so it doesn't come back
        }
    }

    m_parameters = newParameters;

    if (DEBUG_UNUSED) {
        LOG << "%%% end removing unused parameters for " << getName() << "\n";
    }

    Boomerang::get()->alertDecompileDebugPoint(this, "after removing redundant parameters");

    return ret;
}


bool UserProc::removeRedundantReturns(std::set<UserProc *>& removeRetSet)
{
    Boomerang::get()->alertDecompiling(this);
    Boomerang::get()->alertDecompileDebugPoint(this, "before removing unused returns");
    // First remove the unused parameters
    bool removedParams = removeRedundantParameters();

    if (theReturnStatement == nullptr) {
        return removedParams;
    }

    if (DEBUG_UNUSED) {
        LOG << "%%% removing unused returns for " << getName() << " %%%\n";
    }

    if (m_signature->isForced()) {
        // Respect the forced signature, but use it to remove returns if necessary
        bool removedRets = false;
        ReturnStatement::iterator rr;

        for (rr = theReturnStatement->begin(); rr != theReturnStatement->end(); ) {
            Assign    *a  = (Assign *)*rr;
            SharedExp lhs = a->getLeft();
            // For each location in the returns, check if in the signature
            bool found = false;

            for (unsigned int i = 0; i < m_signature->getNumReturns(); i++) {
                if (*m_signature->getReturnExp(i) == *lhs) {
                    found = true;
                    break;
                }
            }

            if (found) {
                rr++; // Yes, in signature; OK
            }
            else {
                // This return is not in the signature. Remove it
                rr          = theReturnStatement->erase(rr);
                removedRets = true;

                if (DEBUG_UNUSED) {
                    LOG << "%%%  removing unused return " << a << " from proc " << getName() << " (forced signature)\n";
                }
            }
        }

        if (removedRets) {
            // Still may have effects on calls or now unused statements
            updateForUseChange(removeRetSet);
        }

        return removedRets;
    }

    // FIXME: this needs to be more sensible when we don't decompile down from main! Probably should assume just the
    // first return is valid, for example (presently assume none are valid)
    LocationSet unionOfCallerLiveLocs;

    if (getName() == "main") { // Probably not needed: main is forced so handled above
        // Just insert one return for main. Note: at present, the first parameter is still the stack pointer
        if (m_signature->getNumReturns() <= 1) {
            // handle the case of missing main() signature
            LOG_STREAM_OLD(LogLevel::Warning) << "main signature definition is missing assuming void main()";
        }
        else {
            unionOfCallerLiveLocs.insert(m_signature->getReturnExp(1));
        }
    }
    else {
        // For each caller
        std::set<CallStatement *>& callers = getCallers();

        for (CallStatement *cc : callers) {
#if RECURSION_WIP
            // TODO: prevent function from blocking it's own removals, needs more work
            if (cc->getProc()->doesRecurseTo(this)) {
                continue;
            }
#endif
            // Union in the set of locations live at this call
            UseCollector *useCol = cc->getUseCollector();
            unionOfCallerLiveLocs.makeUnion(useCol->getLocSet());
        }
    }

    // Intersect with the current returns
    bool removedRets = false;
    ReturnStatement::iterator rr;

    for (rr = theReturnStatement->begin(); rr != theReturnStatement->end(); ) {
        Assign *a = (Assign *)*rr;

        if (unionOfCallerLiveLocs.exists(a->getLeft())) {
            ++rr;
            continue;
        }

        if (DEBUG_UNUSED) {
            LOG << "%%%  removing unused return " << a << " from proc " << getName() << "\n";
        }

        // If a component of the RHS referenced a call statement, the liveness used to be killed here.
        // This was wrong; you need to notice the liveness changing inside updateForUseChange() to correctly
        // recurse to callee
        rr          = theReturnStatement->erase(rr);
        removedRets = true;
    }

    if (DEBUG_UNUSED) {
        QString     tgt;
        QTextStream ost(&tgt);
        unionOfCallerLiveLocs.print(ost);
        LOG << "%%%  union of caller live locations for " << getName() << ": " << tgt << "\n";
        LOG << "%%%  final returns for " << getName() << ": " << theReturnStatement->getReturns().prints() << "\n";
    }

    // removing returns might result in params that can be removed, might as well do it now.
    removedParams |= removeRedundantParameters();

    ProcSet updateSet; // Set of procs to update

    if (removedParams || removedRets) {
        // Update the statements that call us
        std::set<CallStatement *>::iterator it;

        for (it = m_callerSet.begin(); it != m_callerSet.end(); it++) {
            (*it)->updateArguments();              // Update caller's arguments
            updateSet.insert((*it)->getProc());    // Make sure we redo the dataflow
            removeRetSet.insert((*it)->getProc()); // Also schedule caller proc for more analysis
        }

        // Now update myself
        updateForUseChange(removeRetSet);

        // Update any other procs that need updating
        updateSet.erase(this); // Already done this proc

        while (updateSet.size()) {
            UserProc *proc = *updateSet.begin();
            updateSet.erase(proc);
            proc->updateForUseChange(removeRetSet);
        }
    }

    if (theReturnStatement->getNumReturns() == 1) {
        Assign *a = (Assign *)*theReturnStatement->getReturns().begin();
        m_signature->setRetType(a->getType());
    }

    Boomerang::get()->alertDecompileDebugPoint(this, "after removing unused and redundant returns");
    return removedRets || removedParams;
}


void UserProc::updateForUseChange(std::set<UserProc *>& removeRetSet)
{
    // We need to remember the parameters, and all the livenesses for all the calls, to see if these are changed
    // by removing returns
    if (DEBUG_UNUSED) {
        LOG << "%%% updating " << getName() << " for changes to uses (returns or arguments)\n";
        LOG << "%%% updating dataflow:\n";
    }

    // Save the old parameters and call liveness
    StatementList oldParameters(m_parameters);
    std::map<CallStatement *, UseCollector> callLiveness;
    BasicBlock::rtlrit              rrit;
    StatementList::reverse_iterator srit;
    BBIterator it;

    for (BasicBlock *bb = m_cfg->getFirstBB(it); bb; bb = m_cfg->getNextBB(it)) {
        CallStatement *c = dynamic_cast<CallStatement *>(bb->getLastStmt(rrit, srit));

        // Note: we may have removed some statements, so there may no longer be a last statement!
        if (c == nullptr) {
            continue;
        }

        UserProc *dest = dynamic_cast<UserProc *>(c->getDestProc());

        // Not interested in unanalysed indirect calls (not sure) or calls to lib procs
        if (dest == nullptr) {
            continue;
        }

        callLiveness[c].makeCloneOf(*c->getUseCollector());
    }

    // Have to redo dataflow to get the liveness at the calls correct
    removeCallLiveness(); // Want to recompute the call livenesses
    doRenameBlockVars(-3, true);

    remUnusedStmtEtc(); // Also redoes parameters

    // Have the parameters changed? If so, then all callers will need to update their arguments, and do similar
    // analysis to the removal of returns
    // findFinalParameters();
    removeRedundantParameters();

    if (m_parameters.size() != oldParameters.size()) {
        if (DEBUG_UNUSED) {
            LOG << "%%%  parameters changed for " << getName() << "\n";
        }

        std::set<CallStatement *>& callers = getCallers();

        for (CallStatement *cc : callers) {
            cc->updateArguments();
            // Schedule the callers for analysis
            removeRetSet.insert(cc->getProc());
        }
    }

    // Check if the liveness of any calls has changed
    std::map<CallStatement *, UseCollector>::iterator ll;

    for (ll = callLiveness.begin(); ll != callLiveness.end(); ++ll) {
        CallStatement *call       = ll->first;
        UseCollector& oldLiveness = ll->second;
        UseCollector& newLiveness = *call->getUseCollector();

        if (!(newLiveness == oldLiveness)) {
            if (DEBUG_UNUSED) {
                LOG << "%%%  liveness for call to " << call->getDestProc()->getName() << " in " << getName()
                    << " changed\n";
            }

            removeRetSet.insert((UserProc *)call->getDestProc());
        }
    }
}


void UserProc::clearUses()
{
    if (VERBOSE) {
        LOG << "### clearing usage for " << getName() << " ###\n";
    }

    m_procUseCollector.clear();
    BBIterator                           it;
    BasicBlock::rtlrit              rrit;
    StatementList::reverse_iterator srit;

    for (it = m_cfg->begin(); it != m_cfg->end(); ++it) {
        CallStatement *c = (CallStatement *)(*it)->getLastStmt(rrit, srit);

        // Note: we may have removed some statements, so there may no longer be a last statement!
        if ((c == nullptr) || !c->isCall()) {
            continue;
        }

        c->clearUseCollector();
    }
}


void UserProc::typeAnalysis()
{
    LOG_VERBOSE("### Type analysis for %1 ###", getName());

    // Now we need to add the implicit assignments. Doing this earlier is extremely problematic, because
    // of all the m[...] that change their sorting order as their arguments get subscripted or propagated into
    // Do this regardless of whether doing dfa-based TA, so things like finding parameters can rely on implicit assigns
    addImplicitAssigns();

    // Data flow based type analysis
    // Want to be after all propagation, but before converting expressions to locals etc
    if (DFA_TYPE_ANALYSIS) {
        if (DEBUG_TA) {
            LOG_VERBOSE("--- start data flow based type analysis for %1 ---", getName());
        }

        bool first = true;

        do {
            if (!first) {
                doRenameBlockVars(-1, true); // Subscript the discovered extra parameters
                // propagateAtDepth(maxDepth);        // Hack: Can sometimes be needed, if call was indirect
                bool convert;
                propagateStatements(convert, 0);
            }

            first = false;
            dfaTypeAnalysis();

            // There used to be a pass here to insert casts. This is best left until global type analysis is complete,
            // so do it just before translating from SSA form (which is the where type information becomes inaccessible)
        } while (ellipsisProcessing());

        simplify(); // In case there are new struct members

        if (DEBUG_TA) {
            LOG_VERBOSE("=== End type analysis for %1 ===", getName());
        }
    }
    else if (CON_TYPE_ANALYSIS) {
        // FIXME: if we want to do comparison
    }

    printXML();
}


RTL *globalRtl = nullptr;


void UserProc::processDecodedICTs()
{
    BBIterator it;

    BasicBlock::rtlrit              rrit;
    StatementList::reverse_iterator srit;

    for (BasicBlock *bb = m_cfg->getFirstBB(it); bb; bb = m_cfg->getNextBB(it)) {
        Instruction *last = bb->getLastStmt(rrit, srit);

        if (last == nullptr) {
            continue; // e.g. a BB with just a NOP in it
        }

        if (!last->isHL_ICT()) {
            continue;
        }

        RTL *rtl = bb->getLastRtl();

        if (DEBUG_SWITCH) {
            LOG_MSG("Saving high level switch statement %1", rtl);
        }

        m_prog->addDecodedRtl(bb->getHiAddr(), rtl);
        // Now decode those new targets, adding out edges as well
        //        if (last->isCase())
        //            bb->processSwitch(this);
    }
}


void UserProc::setImplicitRef(Instruction *s, SharedExp a, SharedType ty)
{
    BasicBlock *bb = s->getBB(); // Get s' enclosing BB

    std::list<RTL *> *rtls = bb->getRTLs();

    for (std::list<RTL *>::iterator rit = rtls->begin(); rit != rtls->end(); rit++) {
        RTL::iterator it, itForS;
        RTL           *rtlForS;

        for (it = (*rit)->begin(); it != (*rit)->end(); it++) {
            if ((*it == s) ||
                // Not the searched for statement. But if it is a call or return statement, it will be the last, and
                // s must be a substatement (e.g. argument, return, define, etc).
                ((*it)->isCall() || (*it)->isReturn())) {
                // Found s. Search preceeding statements for an implicit reference with address a
                itForS  = it;
                rtlForS = *rit;
                bool found             = false;
                bool searchEarlierRtls = true;

                while (it != (*rit)->begin()) {
                    ImpRefStatement *irs = (ImpRefStatement *)*--it;

                    if (!irs->isImpRef()) {
                        searchEarlierRtls = false;
                        break;
                    }

                    if (*irs->getAddressExp() == *a) {
                        found             = true;
                        searchEarlierRtls = false;
                        break;
                    }
                }

                while (searchEarlierRtls && rit != rtls->begin()) {
                    for (std::list<RTL *>::reverse_iterator revit = rtls->rbegin(); revit != rtls->rend(); ++revit) {
                        it = (*revit)->end();

                        while (it != (*revit)->begin()) {
                            ImpRefStatement *irs = (ImpRefStatement *)*--it;

                            if (!irs->isImpRef()) {
                                searchEarlierRtls = false;
                                break;
                            }

                            if (*irs->getAddressExp() == *a) {
                                found             = true;
                                searchEarlierRtls = false;
                                break;
                            }
                        }

                        if (!searchEarlierRtls) {
                            break;
                        }
                    }
                }

                if (found) {
                    ImpRefStatement *irs = (ImpRefStatement *)*it;
                    bool            ch;
                    irs->meetWith(ty, ch);
                }
                else {
                    ImpRefStatement *irs = new ImpRefStatement(ty, a);
                    rtlForS->insert(itForS, irs);
                }

                return;
            }
        }
    }

    assert(0); // Could not find s withing its enclosing BB
}


void UserProc::eliminateDuplicateArgs()
{
    LOG_VERBOSE("### Eliminate duplicate args for %1 ###", getName());

    BBIterator                           it;
    BasicBlock::rtlrit              rrit;
    StatementList::reverse_iterator srit;

    for (BasicBlock *bb : *m_cfg) {
        CallStatement *c = dynamic_cast<CallStatement *>(bb->getLastStmt(rrit, srit));

        // Note: we may have removed some statements, so there may no longer be a last statement!
        if (c == nullptr) {
            continue;
        }

        c->eliminateDuplicateArgs();
    }
}


void UserProc::removeCallLiveness()
{
    LOG_VERBOSE("### Removing call livenesses for %1 ###", getName());

    BBIterator                           it;
    BasicBlock::rtlrit              rrit;
    StatementList::reverse_iterator srit;

    for (it = m_cfg->begin(); it != m_cfg->end(); ++it) {
        CallStatement *c = dynamic_cast<CallStatement *>((*it)->getLastStmt(rrit, srit));

        // Note: we may have removed some statements, so there may no longer be a last statement!
        if (c == nullptr) {
            continue;
        }

        c->removeAllLive();
    }
}


void UserProc::mapTempsToLocals()
{
    StatementList stmts;

    getStatements(stmts);
    StatementList::iterator it;
    TempToLocalMapper       ttlm(this);
    StmtExpVisitor          sv(&ttlm);

    for (it = stmts.begin(); it != stmts.end(); it++) {
        Instruction *s = *it;
        s->accept(&sv);
    }
}


// For debugging:
void dumpProcList(ProcList *pc)
{
    for (ProcList::iterator pi = pc->begin(); pi != pc->end(); ++pi) {
        LOG_STREAM_OLD() << (*pi)->getName() << ", ";
    }

    LOG_STREAM_OLD() << "\n";
}


void dumpProcSet(ProcSet *pc)
{
    ProcSet::iterator pi;

    for (pi = pc->begin(); pi != pc->end(); ++pi) {
        LOG_STREAM_OLD() << (*pi)->getName() << ", ";
    }

    LOG_STREAM_OLD() << "\n";
}


void Function::setProvenTrue(SharedExp fact)
{
    assert(fact->isEquality());
    SharedExp lhs = fact->getSubExp1();
    SharedExp rhs = fact->getSubExp2();
    m_provenTrue[lhs] = rhs;
}


void UserProc::mapLocalsAndParams()
{
    Boomerang::get()->alertDecompileDebugPoint(this, "Before mapping locals from dfa type analysis");

    if (DEBUG_TA) {
        LOG_MSG(" ### mapping expressions to local variables for %1 ###", getName());
    }

    StatementList stmts;
    getStatements(stmts);
    StatementList::iterator it;

    for (it = stmts.begin(); it != stmts.end(); it++) {
        Instruction *s = *it;
        s->dfaMapLocals();
    }

    if (DEBUG_TA) {
        LOG_MSG(" ### End mapping expressions to local variables for %1 ###", getName());
    }
}


void UserProc::makeSymbolsImplicit()
{
    SymbolMap::iterator it;
    SymbolMap           sm2 = m_symbolMap; // Copy the whole map; necessary because the keys (Exps) change
    m_symbolMap.clear();
    ImplicitConverter ic(m_cfg);

    for (it = sm2.begin(); it != sm2.end(); ++it) {
        SharedExp impFrom = std::const_pointer_cast<Exp>(it->first)->accept(&ic);
        mapSymbolTo(impFrom, it->second);
    }
}


void UserProc::makeParamsImplicit()
{
    StatementList::iterator it;
    ImplicitConverter       ic(m_cfg);

    for (it = m_parameters.begin(); it != m_parameters.end(); ++it) {
        SharedExp lhs = ((Assignment *)*it)->getLeft();
        lhs = lhs->accept(&ic);
        ((Assignment *)*it)->setLeft(lhs);
    }
}


void UserProc::findLiveAtDomPhi(LocationSet& usedByDomPhi)
{
    LocationSet usedByDomPhi0;

    std::map<SharedExp, PhiAssign *, lessExpStar> defdByPhi;
    m_df.findLiveAtDomPhi(0, usedByDomPhi, usedByDomPhi0, defdByPhi);
    // Note that the above is not the complete algorithm; it has found the dead phi-functions in the defdAtPhi
    std::map<SharedExp, PhiAssign *, lessExpStar>::iterator it;

    for (it = defdByPhi.begin(); it != defdByPhi.end(); ++it) {
        // For each phi parameter, remove from the final usedByDomPhi set
        for (const auto& v : *it->second) {
            assert(v.second.e);
            auto wrappedParam = RefExp::get(v.second.e, (Instruction *)v.second.def());
            usedByDomPhi.remove(wrappedParam);
        }

        // Now remove the actual phi-function (a PhiAssign Statement)
        // Ick - some problem with return statements not using their returns until more analysis is done
        // removeStatement(it->second);
    }
}


#if USE_DOMINANCE_NUMS
void UserProc::setDominanceNumbers()
{
    int currNum = 1;

    m_df.setDominanceNums(0, currNum);
}
#endif


void UserProc::findPhiUnites(ConnectionGraph& pu)
{
    StatementList stmts;

    getStatements(stmts);

    for (Instruction *insn : stmts) {
        if (!insn->isPhi()) {
            continue;
        }

        PhiAssign *pa   = (PhiAssign *)insn;
        SharedExp lhs   = pa->getLeft();
        auto      reLhs = RefExp::get(lhs, pa);

        for (const auto& v : *pa) {
            assert(v.second.e);
            auto re = RefExp::get(v.second.e, (Instruction *)v.second.def());
            pu.connect(reLhs, re);
        }
    }
}


QString UserProc::getRegName(SharedExp r)
{
    assert(r->isRegOf());

    // assert(r->getSubExp1()->isConst());
    if (r->getSubExp1()->isConst()) {
        int            regNum = r->access<Const, 1>()->getInt();
        const QString& regName(m_prog->getRegName(regNum));
        assert(!regName.isEmpty());

        if (regName[0] == '%') {
            return regName.mid(1); // Skip % if %eax
        }

        return regName;
    }

    LOG_WARN("Will try to build register name from [tmp+X]!");

    // TODO: how to handle register file lookups ?
    // in some cases the form might be r[tmp+value]
    // just return this expression :(
    // WARN: this is a hack to prevent crashing when r->subExp1 is not const
    QString     tgt;
    QTextStream ostr(&tgt);

    r->getSubExp1()->print(ostr);

    return tgt;
}


SharedType UserProc::getTypeForLocation(const SharedConstExp& e)
{
    QString name = e->access<Const, 1>()->getStr();

    if (e->isLocal()) {
        if (m_locals.find(name) != m_locals.end()) {
            return m_locals[name];
        }
    }

    // Sometimes parameters use opLocal, so fall through
    return getParamType(name);
}


const SharedType UserProc::getTypeForLocation(const SharedConstExp& e) const
{
    return const_cast<UserProc *>(this)->getTypeForLocation(e);
}


void UserProc::verifyPHIs()
{
    StatementList stmts;

    getStatements(stmts);

    for (Instruction *st : stmts) {
        if (!st->isPhi()) {
            continue; // Might be able to optimise this a bit
        }

        PhiAssign *pi = (PhiAssign *)st;

        for (const auto& pas : *pi) {
            Q_UNUSED(pas);
            assert(pas.second.def());
        }
    }
}


void UserProc::nameParameterPhis()
{
    StatementList stmts;

    getStatements(stmts);

    for (Instruction *insn : stmts) {
        if (!insn->isPhi()) {
            continue; // Might be able to optimise this a bit
        }

        PhiAssign *pi = static_cast<PhiAssign *>(insn);
        // See if the destination has a symbol already
        SharedExp lhs    = pi->getLeft();
        auto      lhsRef = RefExp::get(lhs, pi);

        if (findFirstSymbol(lhsRef) != nullptr) {
            continue;                         // Already mapped to something
        }

        bool       multiple  = false;         // True if find more than one unique parameter
        QString    firstName = QString::null; // The name for the first parameter found
        SharedType ty        = pi->getType();

        for (const auto& v : *pi) {
            if (v.second.def()->isImplicit()) {
                QString name = lookupSym(RefExp::get(v.second.e, (Instruction *)v.second.def()), ty);

                if (!name.isNull()) {
                    if (!firstName.isNull() && (firstName != name)) {
                        multiple = true;
                        break;
                    }

                    firstName = name; // Remember this candidate
                }
            }
        }

        if (multiple || firstName.isNull()) {
            continue;
        }

        mapSymbolTo(lhsRef, Location::param(firstName, this));
    }
}


bool UserProc::existsLocal(const QString& name) const
{
    return m_locals.find(name) != m_locals.end();
}


void UserProc::checkLocalFor(const std::shared_ptr<RefExp>& r)
{
    if (!lookupSymFromRefAny(r).isNull()) {
        return; // Already have a symbol for r
    }

    Instruction *def = r->getDef();

    if (!def) {
        return; // TODO: should this be logged ?
    }

    SharedExp  base = r->getSubExp1();
    SharedType ty   = def->getTypeFor(base);
    // No, get its name from the front end
    QString locName = nullptr;

    if (base->isRegOf()) {
        locName = getRegName(base);

        // Create a new local, for the base name if it doesn't exist yet, so we don't need several names for the
        // same combination of location and type. However if it does already exist, addLocal will allocate a
        // new name. Example: r8{0}->argc type int, r8->o0 type int, now r8->o0_1 type char*.
        if (existsLocal(locName)) {
            locName = newLocalName(r);
        }
    }
    else {
        locName = newLocalName(r);
    }

    addLocal(ty, locName, base);
}


//    -    -    -    -    -    -    -    -    -

Log& operator<<(Log& out, const UserProc& c)
{
    QString     tgt;
    QTextStream ost(&tgt);

    c.print(ost);
    out << tgt;
    return out;
}
