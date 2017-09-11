/*
 * Copyright (C) 2002-2006, Trent Waddington
 *
 * See the file "LICENSE.TERMS" for information on usage and
 * redistribution of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 *
 */

/***************************************************************************/ /**
 * \file       CCodeGenerator.cpp
 * \brief   Concrete backend class for the "C" high level language
 *          This class is provides methods which are specific for the C language binding.
 *          I guess this will be the most popular output language unless we do C++.
 ******************************************************************************/

#include "CCodeGenerator.h"

#include "boomerang/core/Boomerang.h"

#include "boomerang/db/BasicBlock.h"
#include "boomerang/db/CFG.h"
#include "boomerang/db/Prog.h"
#include "boomerang/db/RTL.h"
#include "boomerang/db/Signature.h"
#include "boomerang/db/exp/Const.h"
#include "boomerang/db/exp/Ternary.h"
#include "boomerang/db/exp/TypedExp.h"
#include "boomerang/db/exp/Terminal.h"
#include "boomerang/db/exp/Location.h"
#include "boomerang/db/exp/RefExp.h"
#include "boomerang/db/proc/UserProc.h"
#include "boomerang/db/statements/Assign.h"
#include "boomerang/db/statements/CaseStatement.h"
#include "boomerang/db/BinaryImage.h"
#include "boomerang/db/Global.h"

#include "boomerang/type/Type.h"

#include "boomerang/util/Log.h"
#include "boomerang/util/Util.h"

#include <QtCore/QFile>
#include <QtCore/QTextStream>

#include <cassert>
#include <sstream>
#include <cstring>
#include <cstdlib>
#include <memory>



static bool isBareMemof(const Exp& e, UserProc *)
{
#if SYMS_IN_BACK_END
    if (!e.isMemOf()) { return false; }

    // Check if it maps to a symbol
    const char *sym = proc->lookupSym(e);

    if (sym == nullptr) {
        sym = proc->lookupSym(e->getSubExp1());
    }

    return sym == nullptr; // Only a bare memof if it is not a symbol

#else
    return e.isMemOf();
#endif
}


CCodeGenerator::CCodeGenerator()
    : m_indent(0)
    , m_proc(nullptr)
{
}


CCodeGenerator::~CCodeGenerator()
{
}


void CCodeGenerator::generateCode(const Prog* prog, QTextStream& os)
{
    for (Global *glob : prog->getGlobals()) {
        // Check for an initial value
        auto e = glob->getInitialValue(prog);

        if (e) {
            addGlobal(glob->getName(), glob->getType(), e);
        }
    }

    print(os);

    for (Module *module : prog->getModuleList()) {
        for (Function *function : *module) {
            if (function->isLib()) {
                continue;
            }

            UserProc *userProc = (UserProc *)function;

            if (!userProc->isDecoded()) {
                continue;
            }

            userProc->getCFG()->compressCfg();

            generateCode(userProc);
            print(os);
        }
    }
}


void CCodeGenerator::generateCode(const Prog* prog, Module *cluster, UserProc *proc, bool /*intermixRTL*/)
{
    // QString basedir = m_rootCluster->makeDirs();
    QTextStream *os = nullptr;

    if (cluster) {
        cluster->openStream("c");
        cluster->closeStreams();
    }

    const bool generate_all   = cluster == nullptr || cluster == prog->getRootModule();
    bool all_procedures = (proc == nullptr);

    if (generate_all) {
        prog->getRootModule()->openStream("c");
        os = &prog->getRootModule()->getStream();

        if (proc == nullptr) {
            bool global = false;

            if (SETTING(noDecompile)) {
                const char *sections[] = { "rodata", "data", "data1", nullptr };

                for (int j = 0; sections[j]; j++) {
                    QString str = ".";
                    str += sections[j];
                    IBinarySection *info = Boomerang::get()->getImage()->getSectionByName(str);

                    if (info) {
                        generateDataSectionCode(Boomerang::get()->getImage(), sections[j], info->getSourceAddr(), info->getSize());
                    }
                    else {
                        generateDataSectionCode(Boomerang::get()->getImage(), sections[j], Address::INVALID, 0);
                    }
                }

                addGlobal("source_endianness", IntegerType::get(STD_SIZE),
                                Const::get(prog->getFrontEndId() != Platform::PENTIUM));
                (*os) << "#include \"boomerang.h\"\n\n";
                global = true;
            }

            for (Global *elem : prog->getGlobals()) {
                // Check for an initial value
                SharedExp e = elem->getInitialValue(prog);
                // if (e) {
                addGlobal(elem->getName(), elem->getType(), e);
                global = true;
            }

            if (global) {
                print(*os); // Avoid blank line if no globals
            }
        }
    }

    // First declare prototypes
    for (Module *module : prog->getModuleList()) {
        for (Function *func : *module) {
            if (func->isLib()) {
                continue;
            }

            UserProc *_proc   = (UserProc *)func;
            addPrototype(_proc); // May be the wrong signature if up has ellipsis
        }
    }

    if (generate_all) {
        appendLine(""); // Separate prototype(s) from first proc
        print(*os);
    }

    for (Module *module : prog->getModuleList()) {
        if (!generate_all && (cluster != module)) {
            continue;
        }

        module->openStream("c");

        for (Function *func : *module) {
            if (func->isLib()) {
                continue;
            }

            UserProc *_proc = (UserProc *)func;

            if (!_proc->isDecoded()) {
                continue;
            }

            if (!all_procedures && (proc != _proc)) {
                continue;
            }

            _proc->getCFG()->compressCfg();
            _proc->getCFG()->removeOrphanBBs();

            generateCode(_proc);
            print(module->getStream());
        }
    }

    for (Module *module : prog->getModuleList()) {
        module->closeStreams();
    }
}



void CCodeGenerator::addAssignmentStatement(Assign* asgn)
{
    // Gerard: shouldn't these  3 types of statements be removed earlier?
    if (asgn->getLeft()->getOper() == opPC) {
        return; // Never want to see assignments to %PC
    }

    SharedExp result;

    if (asgn->getRight()->search(Terminal(opPC), result)) { // Gerard: what's this?
        return;
    }

    // ok I want this now
    // if (asgn->getLeft()->isFlags())
    //    return;

    QString tgt;
    QTextStream s(&tgt);
    indent(s, m_indent);

    SharedType asgnType = asgn->getType();
    SharedExp lhs       = asgn->getLeft();
    SharedExp rhs       = asgn->getRight();
    UserProc *proc      = asgn->getProc();

    if (*lhs == *rhs) {
        return; // never want to see a = a;
    }

    if (SETTING(noDecompile) && isBareMemof(*rhs, proc) && (lhs->getOper() == opRegOf) &&
        (m_proc->getProg()->getFrontEndId() == Platform::SPARC)) {
        int wdth = lhs->access<Const, 1>()->getInt();

        // add some fsize hints to rhs
        if ((wdth >= 32) && (wdth <= 63)) {
            rhs = std::make_shared<Ternary>(opFsize, Const::get(32), Const::get(32), rhs);
        }
        else if ((wdth >= 64) && (wdth <= 87)) {
            rhs = std::make_shared<Ternary>(opFsize, Const::get(64), Const::get(64), rhs);
        }
    }

    if (SETTING(noDecompile) && isBareMemof(*lhs, proc)) {
        if (asgnType && asgnType->isFloat()) {
            if (asgnType->as<FloatType>()->getSize() == 32) {
                s << "FLOAT_";
            }
            else {
                s << "DOUBLE_";
            }
        }
        else if (rhs->getOper() == opFsize) {
            if (rhs->access<Const, 2>()->getInt() == 32) {
                s << "FLOAT_";
            }
            else {
                s << "DOUBLE_";
            }
        }
        else if ((rhs->getOper() == opRegOf) && (m_proc->getProg()->getFrontEndId() == Platform::SPARC)) {
            // yes, this is a hack
            int wdth = rhs->access<Const, 2>()->getInt();

            if ((wdth >= 32) && (wdth <= 63)) {
                s << "FLOAT_";
            }
            else if ((wdth >= 64) && (wdth <= 87)) {
                s << "DOUBLE_";
            }
        }

        s << "MEMASSIGN(";
        appendExp(s, *lhs->getSubExp1(), PREC_UNARY);
        s << ", ";
        appendExp(s, *rhs, PREC_UNARY);
        s << ");";
        appendLine(tgt);
        return;
    }

    if (isBareMemof(*lhs, proc) && asgnType && !asgnType->isVoid()) {
        appendExp(s, TypedExp(asgnType, lhs), PREC_ASSIGN);
    }
    else if ((lhs->getOper() == opGlobal) && asgn->getType()->isArray()) {
        appendExp(s, Binary(opArrayIndex, lhs, Const::get(0)), PREC_ASSIGN);
    }
    else if ((lhs->getOper() == opAt) && lhs->getSubExp2()->isIntConst() &&
             lhs->getSubExp3()->isIntConst()) {
        // exp1@[n:m] := rhs -> exp1 = exp1 & mask | rhs << m  where mask = ~((1 << m-n+1)-1)
        SharedExp exp1 = lhs->getSubExp1();
        int n          = lhs->access<Const, 2>()->getInt();
        int m          = lhs->access<Const, 3>()->getInt();
        appendExp(s, *exp1, PREC_ASSIGN);
        s << " = ";
        int mask = ~(((1 << (m - n + 1)) - 1) << m); // MSVC winges without most of these parentheses
        rhs = Binary::get(opBitAnd, exp1,
                          Binary::get(opBitOr, Const::get(mask), Binary::get(opShiftL, rhs, Const::get(m))));
        rhs = rhs->simplify();
        appendExp(s, *rhs, PREC_ASSIGN);
        s << ";";
        appendLine(tgt);
        return;
    }
    else {
        appendExp(s, *lhs, PREC_ASSIGN); // Ordinary LHS
    }

    if ((rhs->getOper() == opPlus) && (*rhs->getSubExp1() == *lhs)) {
        // C has special syntax for this, eg += and ++
        // however it's not always acceptable for assigns to m[] (?)
        if (rhs->getSubExp2()->isIntConst() &&
            ((rhs->access<Const, 2>()->getInt() == 1) || (asgn->getType()->isPointer() &&
                                                          (asgn->getType()->as<PointerType>()->getPointsTo()->getSize() ==
                                                           (unsigned)rhs->access<Const, 2>()->getInt() * 8)))) {
            s << "++";
        }
        else {
            s << " += ";
            appendExp(s, *rhs->getSubExp2(), PREC_ASSIGN);
        }
    }
    else {
        s << " = ";
        appendExp(s, *rhs, PREC_ASSIGN);
    }

    s << ";";
    appendLine(tgt);
}


void CCodeGenerator::addCallStatement(Function *proc, const QString& name, StatementList& args,
                                      StatementList *results)
{
    QString tgt;
    QTextStream s(&tgt);

    indent(s, m_indent);

    if (!results->empty()) {
        // FIXME: Needs changing if more than one real result (return a struct)
        SharedExp firstRet = ((Assignment *)*results->begin())->getLeft();
        appendExp(s, *firstRet, PREC_ASSIGN);
        s << " = ";
    }

    s << name << "(";
    StatementList::iterator ss;
    bool first = true;
    int n      = 0;

    for (ss = args.begin(); ss != args.end(); ++ss, ++n) {
        if (first) {
            first = false;
        }
        else {
            s << ", ";
        }

        Assignment *arg_assign = dynamic_cast<Assignment *>(*ss);
        assert(arg_assign != nullptr);
        SharedType t   = arg_assign->getType();
        auto as_arg    = arg_assign->getRight();
        auto const_arg = std::dynamic_pointer_cast<const Const>(as_arg);
        bool ok        = true;

        if (t && t->isPointer() && std::static_pointer_cast<PointerType>(t)->getPointsTo()->isFunc() && const_arg->isIntConst()) {
            Function *p = proc->getProg()->findProc(const_arg->getAddr());

            if (p) {
                s << p->getName();
                ok = false;
            }
        }

        if (ok) {
            bool needclose = false;

            if (SETTING(noDecompile) && proc->getSignature()->getParamType(n) &&
                proc->getSignature()->getParamType(n)->isPointer()) {
                s << "ADDR(";
                needclose = true;
            }

            appendExp(s, *as_arg, PREC_COMMA);

            if (needclose) {
                s << ")";
            }
        }
    }

    s << ");";

    if (results->size() > 1) {
        first = true;
        s << " /* Warning: also results in ";

        for (ss = ++results->begin(); ss != results->end(); ++ss) {
            if (first) {
                first = false;
            }
            else {
                s << ", ";
            }

            appendExp(s, *((Assignment *)*ss)->getLeft(), PREC_COMMA);
        }

        s << " */";
    }

    appendLine(tgt);
}


void CCodeGenerator::addIndCallStatement(const SharedExp& exp, StatementList& args, StatementList *results)
{
    Q_UNUSED(results);
    //    FIXME: Need to use 'results', since we can infer some defines...
    QString tgt;
    QTextStream s(&tgt);
    indent(s, m_indent);
    s << "(*";
    appendExp(s, *exp, PREC_NONE);
    s << ")(";
    QStringList arg_strings;
    QString arg_tgt;

    for (Statement *ss : args) {
        QTextStream arg_str(&arg_tgt);
        SharedExp arg = ((Assign *)ss)->getRight();
        appendExp(arg_str, *arg, PREC_COMMA);
        arg_strings << arg_tgt;
        arg_tgt.clear();
    }

    s << arg_strings.join(", ") << ");";
    appendLine(tgt);
}


void CCodeGenerator::addReturnStatement(StatementList *rets)
{
    // FIXME: should be returning a struct of more than one real return */
    // The stack pointer is wanted as a define in calls, and so appears in returns, but needs to be removed here
    StatementList::iterator rr;
    QString tgt;
    QTextStream s(&tgt);
    indent(s, m_indent);
    s << "return";
    size_t n = rets->size();

    if ((n == 0) && SETTING(noDecompile) && (m_proc->getSignature()->getNumReturns() > 0)) {
        s << " eax";
    }

    if (n >= 1) {
        s << " ";
        appendExp(s, *((Assign *)*rets->begin())->getRight(), PREC_NONE);
    }

    s << ";";

    if (n > 0) {
        if (n > 1) {
            s << " /* WARNING: Also returning: ";
        }

        bool first = true;

        for (rr = ++rets->begin(); rr != rets->end(); ++rr) {
            if (first) {
                first = false;
            }
            else {
                s << ", ";
            }

            appendExp(s, *((Assign *)*rr)->getLeft(), PREC_NONE);
            s << " := ";
            appendExp(s, *((Assign *)*rr)->getRight(), PREC_NONE);
        }

        if (n > 1) {
            s << " */";
        }
    }

    appendLine(tgt);
}


void CCodeGenerator::removeUnusedLabels(int)
{
    for (QStringList::iterator it = m_lines.begin(); it != m_lines.end(); ) {
        if (it->startsWith('L') && it->contains(':')) {
            QStringRef sxr = it->midRef(1, it->indexOf(':') - 1);
            int n          = sxr.toInt();

            if (m_usedLabels.find(n) == m_usedLabels.end()) {
                it = m_lines.erase(it);
                continue;
            }
        }

        it++;
    }
}


void CCodeGenerator::addPrototype(UserProc *proc)
{
    m_proc = proc;
    addProcDec(proc, false);
}


void CCodeGenerator::generateCode(UserProc* proc)
{
    m_lines.clear();
    m_proc = proc;

    assert(proc->getCFG());
    assert(proc->getEntryBB());

    proc->getCFG()->structure();
    proc->removeUnusedLocals();

    // Note: don't try to remove unused statements here; that requires the
    // RefExps, which are all gone now (transformed out of SSA form)!

    if (SETTING(printRtl)) {
        LOG_VERBOSE("%1", proc->toString());
    }

    // Start generating code for this procedure.
    this->addProcStart(proc);

    // Local variables; print everything in the locals map
    std::map<QString, SharedType>::const_iterator last = proc->getLocals().end();

    if (!proc->getLocals().empty()) {
        last = std::prev(last);
    }

    for (auto it = proc->getLocals().begin(); it != proc->getLocals().end(); it++) {
        SharedType locType = it->second;

        if ((locType == nullptr) || locType->isVoid()) {
            locType = IntegerType::get(STD_SIZE);
        }

        addLocal(it->first, locType, it == last);
    }

    if (SETTING(noDecompile) && (proc->getName() == "main")) {
        StatementList args, results;

        if (proc->getProg()->getFrontEndId() == Platform::PENTIUM) {
            addCallStatement(nullptr, "PENTIUMSETUP", args, &results);
        }
        else if (proc->getProg()->getFrontEndId() == Platform::SPARC) {
            addCallStatement(nullptr, "SPARCSETUP", args, &results);
        }
    }

    // Start generating "real" code
    std::list<BasicBlock *> followSet, gotoSet;
    generateCode(proc->getEntryBB(), nullptr, followSet, gotoSet, proc);

    addProcEnd();

    if (!SETTING(noRemoveLabels)) {
        proc->getCFG()->removeUnneededLabels(this);
    }

    proc->setStatus(PROC_CODE_GENERATED);
}


void CCodeGenerator::generateDataSectionCode(IBinaryImage* image, QString section_name, Address section_start, uint32_t size)
{
    addGlobal("start_" + section_name, IntegerType::get(32, -1), Const::get(section_start));
    addGlobal(section_name + "_size", IntegerType::get(32, -1), Const::get(size ? size : (unsigned int)-1));
    auto l = Terminal::get(opNil);

    for (unsigned int i = 0; i < size; i++) {
        int n = image->readNative1(section_start + size - 1 - i);

        l = Binary::get(opList, Const::get(n & 0xFF), l);
    }

    addGlobal(section_name, ArrayType::get(IntegerType::get(8, -1), size), l);
}


void CCodeGenerator::addProcDec(UserProc *proc, bool open)
{
    QString tgt;
    QTextStream s(&tgt);
    ReturnStatement *returns = proc->getTheReturnStatement();
    SharedType retType;

    if (proc->getSignature()->isForced()) {
        if (proc->getSignature()->getNumReturns() == 0) {
            s << "void ";
        }
        else {
            unsigned int n = 0;
            SharedExp e    = proc->getSignature()->getReturnExp(0);

            if (e->isRegN(Signature::getStackRegister(proc->getProg()))) {
                n = 1;
            }

            if (n < proc->getSignature()->getNumReturns()) {
                retType = proc->getSignature()->getReturnType(n);
            }

            if (retType == nullptr) {
                s << "void ";
            }
        }
    }
    else if ((returns == nullptr) || (returns->getNumReturns() == 0)) {
        s << "void ";
    }
    else {
        Assign *firstRet = (Assign *)*returns->begin();
        retType = firstRet->getType();

        if ((retType == nullptr) || retType->isVoid()) {
            // There is a real return; make it integer (Remove with AD HOC type analysis)
            retType = IntegerType::get(STD_SIZE, 0);
        }
    }

    if (retType) {
        appendType(s, retType);

        if (!retType->isPointer()) { // NOTE: assumes type *proc( style
            s << " ";
        }
    }

    s << proc->getName() << "(";
    StatementList& parameters = proc->getParameters();

    if ((parameters.size() > 10) && open) {
        LOG_WARN("Proc %1 has %2 parameters", proc->getName(), parameters.size());
    }

    bool first = true;

    for (auto pp = parameters.begin(); pp != parameters.end(); ++pp) {
        if (first) {
            first = false;
        }
        else {
            s << ", ";
        }

        Assignment *as = (Assignment *)*pp;
        SharedExp left = as->getLeft();
        SharedType ty  = as->getType();

        if (ty == nullptr) {
            if (VERBOSE) {
                LOG_ERROR("No type for parameter %1!", left);
            }

            ty = IntegerType::get(STD_SIZE, 0);
        }

        QString name;

        if (left->isParam()) {
            name = left->access<Const, 1>()->getStr();
        }
        else {
            LOG_ERROR("Parameter %1 is not opParam!", left);
            name = "??";
        }

        if (ty->isPointer() && std::static_pointer_cast<PointerType>(ty)->getPointsTo()->isArray()) {
            // C does this by default when you pass an array, i.e. you pass &array meaning array
            // Replace all m[param] with foo, param with foo, then foo with param
            ty = std::static_pointer_cast<PointerType>(ty)->getPointsTo();

            SharedExp foo = Const::get("foo123412341234");
            m_proc->searchAndReplace(*Location::memOf(left, nullptr), foo);
            m_proc->searchAndReplace(*left, foo);
            m_proc->searchAndReplace(*foo, left);
        }

        appendTypeIdent(s, ty, name);
    }

    s << ")";

    if (open) {
        appendLine(tgt);
        appendLine("{");
        m_indent++;
    }
    else {
        s << ";";
        appendLine(tgt);
    }
}


void CCodeGenerator::addPretestedLoopHeader(const SharedExp& cond)
{
    QString tgt;
    QTextStream s(&tgt);

    indent(s, m_indent);
    s << "while (";
    appendExp(s, *cond, PREC_NONE);
    s << ") {";
    appendLine(tgt);

    m_indent++;
}


void CCodeGenerator::addPretestedLoopEnd()
{
    m_indent--;

    QString tgt;
    QTextStream s(&tgt);

    indent(s, m_indent);
    s << "}";
    appendLine(tgt);
}


void CCodeGenerator::addEndlessLoopHeader()
{
    QString tgt;
    QTextStream s(&tgt);

    indent(s, m_indent);
    s << "for(;;) {";
    appendLine(tgt);

    m_indent++;
}


void CCodeGenerator::addEndlessLoopEnd()
{
    m_indent--;
    QString tgt;
    QTextStream s(&tgt);

    indent(s, m_indent);
    s << "}";
    appendLine(tgt);
}


void CCodeGenerator::addPostTestedLoopHeader()
{
    QString tgt;
    QTextStream s(&tgt);

    indent(s, m_indent);
    s << "do {";
    appendLine(tgt);
    m_indent++;
}


void CCodeGenerator::addPostTestedLoopEnd(const SharedExp& cond)
{
    m_indent--;

    QString tgt;
    QTextStream s(&tgt);

    indent(s, m_indent);
    s << "} while (";
    appendExp(s, *cond, PREC_NONE);
    s << ");";
    appendLine(tgt);
}


void CCodeGenerator::addCaseCondHeader(const SharedExp& cond)
{
    QString tgt;
    QTextStream s(&tgt);

    indent(s, m_indent);
    s << "switch(";
    appendExp(s, *cond, PREC_NONE);
    s << ") {";
    appendLine(tgt);

    m_indent++;
}


void CCodeGenerator::addCaseCondOption(Exp& opt)
{
    m_indent--;

    QString tgt;
    QTextStream s(&tgt);

    indent(s, m_indent);
    s << "case ";
    appendExp(s, opt, PREC_NONE);
    s << ":";
    appendLine(tgt);

    m_indent++;
}


void CCodeGenerator::addCaseCondOptionEnd()
{
    QString tgt;
    QTextStream s(&tgt);

    indent(s, m_indent);
    s << "break;";
    appendLine(tgt);
}


void CCodeGenerator::addCaseCondElse()
{
    m_indent--;
    QString tgt;
    QTextStream s(&tgt);

    indent(s, m_indent);
    s << "default:";
    appendLine(tgt);

    m_indent++;
}


void CCodeGenerator::addCaseCondEnd()
{
    m_indent--;

    QString tgt;
    QTextStream s(&tgt);

    indent(s, m_indent);
    s << "}";
    appendLine(tgt);
}


void CCodeGenerator::addIfCondHeader(const SharedExp& cond)
{
    QString tgt;
    QTextStream s(&tgt);

    indent(s, m_indent);
    s << "if (";
    appendExp(s, *cond, PREC_NONE);
    s << ") {";
    appendLine(tgt);

    m_indent++;
}


void CCodeGenerator::addIfCondEnd()
{
    m_indent--;

    QString tgt;
    QTextStream s(&tgt);

    indent(s, m_indent);
    s << "}";
    appendLine(tgt);
}


void CCodeGenerator::addIfElseCondHeader(const SharedExp& cond)
{
    QString tgt;
    QTextStream s(&tgt);

    indent(s, m_indent);
    s << "if (";
    appendExp(s, *cond, PREC_NONE);
    s << ") {";
    appendLine(tgt);

    m_indent++;
}


void CCodeGenerator::addIfElseCondOption()
{
    m_indent--;

    QString tgt;
    QTextStream s(&tgt);

    indent(s, m_indent);
    s << "}";
    appendLine(tgt);

    tgt = "";
    indent(s, m_indent);
    s << "else {";
    appendLine(tgt);

    m_indent++;
}


void CCodeGenerator::addIfElseCondEnd()
{
    m_indent--;

    QString tgt;
    QTextStream s(&tgt);

    indent(s, m_indent);
    s << "}";
    appendLine(tgt);
}


void CCodeGenerator::addGoto(int ord)
{
    QString tgt;
    QTextStream s(&tgt);

    indent(s, m_indent);
    s << "goto L" << ord << ";";
    appendLine(tgt);
    m_usedLabels.insert(ord);
}


void CCodeGenerator::addContinue()
{
    QString tgt;
    QTextStream s(&tgt);

    indent(s, m_indent);
    s << "continue;";
    appendLine(tgt);
}


void CCodeGenerator::addBreak()
{
    QString tgt;
    QTextStream s(&tgt);

    indent(s, m_indent);
    s << "break;";
    appendLine(tgt);
}


void CCodeGenerator::addLabel(int ord)
{
    QString tgt;
    QTextStream s(&tgt);

    s << "L" << ord << ":";
    appendLine(tgt);
}


void CCodeGenerator::removeLabel(int ord)
{
    QString tgt;
    QTextStream s(&tgt);

    s << "L" << ord << ":";
    m_lines.removeAll(tgt);
}



void CCodeGenerator::addProcStart(UserProc *proc)
{
    QString tgt;
    QTextStream s(&tgt);

    s << "/** address: " << proc->getEntryAddress() << " */";
    appendLine(tgt);
    addProcDec(proc, true);
}


void CCodeGenerator::addProcEnd()
{
    m_indent--;
    appendLine("}");
    appendLine("");
}


void CCodeGenerator::addLocal(const QString& name, SharedType type, bool last)
{
    QString tgt;
    QTextStream s(&tgt);

    indent(s, 1);
    appendTypeIdent(s, type, name);
    SharedConstExp e = m_proc->expFromSymbol(name);

    if (e) {
        // ? Should never see subscripts in the back end!
        if ((e->getOper() == opSubscript) && ((const RefExp *)e.get())->isImplicitDef() &&
            ((e->getSubExp1()->getOper() == opParam) || (e->getSubExp1()->getOper() == opGlobal))) {
            s << " = ";
            appendExp(s, *e->getSubExp1(), PREC_NONE);
            s << ";";
        }
        else {
            s << "; \t\t// ";
            e->print(s);
        }
    }
    else {
        s << ";";
    }

    appendLine(tgt);
    m_locals[name] = type->clone();

    if (last) {
        appendLine("");
    }
}


void CCodeGenerator::addGlobal(const QString& name, SharedType type, const SharedExp& init)
{
    QString tgt;
    QTextStream s(&tgt);

    // Check for array types. These are declared differently in C than
    // they are printed
    if (type->isArray()) {
        // Get the component type
        SharedType base = std::static_pointer_cast<ArrayType>(type)->getBaseType();
        appendType(s, base);
        s << " " << name << "[" << std::static_pointer_cast<ArrayType>(type)->getLength() << "]";
    }
    else if (type->isPointer() && std::static_pointer_cast<PointerType>(type)->getPointsTo()->resolvesToFunc()) {
        // These are even more different to declare than to print. Example:
        // void (void)* global0 = foo__1B;     ->
        // void (*global0)(void) = foo__1B;
        auto pt = std::static_pointer_cast<PointerType>(type);
        std::shared_ptr<FuncType> ft = std::static_pointer_cast<FuncType>(pt->getPointsTo());
        QString ret, param;
        ft->getReturnAndParam(ret, param);
        s << ret << "(*" << name << ")" << param;
    }
    else {
        appendType(s, type);
        s << " " << name;
    }

    if (init && !init->isNil()) {
        s << " = ";
        SharedType base_type = type->isArray() ? type->as<ArrayType>()->getBaseType() : type;
        appendExp(s, *init, PREC_ASSIGN, base_type->isInteger() ? !base_type->as<IntegerType>()->isSigned() : false);
    }

    s << ";";

    if (type->isSize()) {
        s << "// " << type->getSize() / 8 << " bytes";
    }

    appendLine(tgt);
}


void CCodeGenerator::addLineComment(const QString& cmt)
{
    appendLine(QString("/* %1 */").arg(cmt));
}


void CCodeGenerator::appendExp(QTextStream& str, const Exp& exp, PREC curPrec, bool uns /* = false */)
{
    OPER op = exp.getOper();

#if SYMS_IN_BACK_END                 // Should no longer be any unmapped symbols by the back end
    // Check if it's mapped to a symbol
    if (m_proc && !exp->isTypedExp()) { // Beware: lookupSym will match (cast)r24 to local0, stripping the cast!
        const char *sym = m_proc->lookupSym(exp);

        if (sym) {
            str << sym;
            return;
        }
    }
#endif

    const Const&   c(static_cast<const Const&>(exp));
    const Unary&   u(static_cast<const Unary&>(exp));
    const Binary&  b(static_cast<const Binary&>(exp));
    const Ternary& t(static_cast<const Ternary&>(exp));

    switch (op)
    {
    case opIntConst:
        {
            int K = exp.access<Const>()->getInt();

            if (uns && (K < 0)) {
                // An unsigned constant. Use some heuristics
                unsigned rem = (unsigned)K % 100;

                if ((rem == 0) || (rem == 99) || (K > -128)) {
                    // A multiple of 100, or one less; use 4000000000U style
                    char num[16];
                    sprintf(num, "%u", (unsigned int)K);
                    str << num << "U";
                }
                else {
                    // Output it in 0xF0000000 style
                    str << "0x" << QString::number(uint32_t(K), 16);
                }
            }
            else {
                if (c.getType() && c.getType()->isChar()) {
                    switch (K)
                    {
                    case '\a':
                        str << "'\\a'";
                        break;

                    case '\b':
                        str << "'\\b'";
                        break;

                    case '\f':
                        str << "'\\f'";
                        break;

                    case '\n':
                        str << "'\\n'";
                        break;

                    case '\r':
                        str << "'\\r'";
                        break;

                    case '\t':
                        str << "'\\t'";
                        break;

                    case '\v':
                        str << "'\\v'";
                        break;

                    case '\\':
                        str << "'\\\\'";
                        break;

                    case '\?':
                        str << "'\\?'";
                        break;

                    case '\'':
                        str << "'\\''";
                        break;

                    case '\"':
                        str << "'\\\"'";
                        break;

                    default:
                        str << "'" << (char)K << "'";
                    }
                }
                else {
                    // More heuristics
                    if ((-2048 < K) && (K < 2048)) {
                        str << K; // Just a plain vanilla int
                    }
                    else {
                        str << "0x" << QString::number(uint32_t(K), 16); // 0x2000 style
                    }
                }
            }

            break;
        }

    case opLongConst:

        // str << std::dec << c->getLong() << "LL"; break;
        if (((long long)c.getLong() < -1000LL) || ((long long)c.getLong() > 1000LL)) {
            str << "0x" << QString::number(c.getLong(), 16) << "LL";
        }
        else {
            str << c.getLong() << "LL";
        }

        break;

    case opFltConst:
        {
            // str.precision(4);     // What to do with precision here? Would be nice to avoid 1.00000 or 0.99999
            QString flt_val = QString::number(c.getFlt(), 'g', 8);

            if (!flt_val.contains('.')) {
                flt_val += '.';
            }

            str << flt_val;
            break;
        }

    case opStrConst:
        // escape string:
        str << "\"" << Util::escapeStr(c.getStr()) << "\"";
        break;

    case opFuncConst:
        str << c.getFuncName();
        break;

    case opAddrOf:
        {
            SharedConstExp sub = u.getSubExp1();

            if (sub->isGlobal()) {
                Prog *prog = m_proc->getProg();

                auto       con = std::static_pointer_cast<const Const>(sub->getSubExp1());
                SharedType gt  = prog->getGlobalType(con->getStr());

                if (gt && (gt->isArray() || (gt->isPointer() && gt->as<PointerType>()->getPointsTo()->isChar()))) {
                    // Special C requirement: don't emit "&" for address of an array or char*
                    appendExp(str, *sub, curPrec);
                    break;
                }
            }

#if SYMS_IN_BACK_END
            if (sub->isMemOf() && (m_proc->lookupSym(sub) == nullptr)) { // }
#else
            if (sub->isMemOf()) {
#endif

                // Avoid &*(type*)sub, just emit sub
                appendExp(str, *sub->getSubExp1(), PREC_UNARY);
            }
            else {
                openParen(str, curPrec, PREC_UNARY);
                str << "&";
                appendExp(str, *sub, PREC_UNARY);
                closeParen(str, curPrec, PREC_UNARY);
            }

            break;
        }

    case opParam:
    case opGlobal:
    case opLocal:
        {
            auto c1 = std::dynamic_pointer_cast<const Const>(u.getSubExp1());
            assert(c1 && c1->getOper() == opStrConst);
            str << c1->getStr();
        }
        break;

    case opEquals:
        {
            openParen(str, curPrec, PREC_EQUAL);
            appendExp(str, *b.getSubExp1(), PREC_EQUAL);
            str << " == ";
            appendExp(str, *b.getSubExp2(), PREC_EQUAL);
            closeParen(str, curPrec, PREC_EQUAL);
        }
        break;

    case opNotEqual:
        {
            openParen(str, curPrec, PREC_EQUAL);
            appendExp(str, *b.getSubExp1(), PREC_EQUAL);
            str << " != ";
            appendExp(str, *b.getSubExp2(), PREC_EQUAL);
            closeParen(str, curPrec, PREC_EQUAL);
        }
        break;

    case opLess:
    case opLessUns:
        openParen(str, curPrec, PREC_REL);
        appendExp(str, *b.getSubExp1(), PREC_REL, op == opLessUns);
        str << " < ";
        appendExp(str, *b.getSubExp2(), PREC_REL, op == opLessUns);
        closeParen(str, curPrec, PREC_REL);
        break;

    case opGtr:
    case opGtrUns:
        openParen(str, curPrec, PREC_REL);
        appendExp(str, *b.getSubExp1(), PREC_REL, op == opGtrUns);
        str << " > ";
        appendExp(str, *b.getSubExp2(), PREC_REL, op == opGtrUns);
        closeParen(str, curPrec, PREC_REL);
        break;

    case opLessEq:
    case opLessEqUns:
        openParen(str, curPrec, PREC_REL);
        appendExp(str, *b.getSubExp1(), PREC_REL, op == opLessEqUns);
        str << " <= ";
        appendExp(str, *b.getSubExp2(), PREC_REL, op == opLessEqUns);
        closeParen(str, curPrec, PREC_REL);
        break;

    case opGtrEq:
    case opGtrEqUns:
        openParen(str, curPrec, PREC_REL);
        appendExp(str, *b.getSubExp1(), PREC_REL, op == opGtrEqUns);
        str << " >= ";
        appendExp(str, *b.getSubExp2(), PREC_REL, op == opGtrEqUns);
        closeParen(str, curPrec, PREC_REL);
        break;

    case opAnd:
        openParen(str, curPrec, PREC_LOG_AND);
        appendExp(str, *b.getSubExp1(), PREC_LOG_AND);
        str << " && ";
        appendExp(str, *b.getSubExp2(), PREC_LOG_AND);
        closeParen(str, curPrec, PREC_LOG_AND);
        break;

    case opOr:
        openParen(str, curPrec, PREC_LOG_OR);
        appendExp(str, *b.getSubExp1(), PREC_LOG_OR);
        str << " || ";
        appendExp(str, *b.getSubExp2(), PREC_LOG_OR);
        closeParen(str, curPrec, PREC_LOG_OR);
        break;

    case opBitAnd:
        openParen(str, curPrec, PREC_BIT_AND);
        appendExp(str, *b.getSubExp1(), PREC_BIT_AND);
        str << " & ";

        if (b.getSubExp2()->getOper() == opIntConst) {
            // print it 0x2000 style
            uint32_t val     = uint32_t(std::static_pointer_cast<const Const>(b.getSubExp2())->getInt());
            QString  vanilla = QString("0x") + QString::number(val, 16);
            QString  negated = QString("~0x") + QString::number(~val, 16);

            if (negated.size() < vanilla.size()) {
                str << negated;
            }
            else {
                str << vanilla;
            }
        }
        else {
            appendExp(str, *b.getSubExp2(), PREC_BIT_AND);
        }

        closeParen(str, curPrec, PREC_BIT_AND);
        break;

    case opBitOr:
        openParen(str, curPrec, PREC_BIT_IOR);
        appendExp(str, *b.getSubExp1(), PREC_BIT_IOR);
        str << " | ";
        appendExp(str, *b.getSubExp2(), PREC_BIT_IOR);
        closeParen(str, curPrec, PREC_BIT_IOR);
        break;

    case opBitXor:
        openParen(str, curPrec, PREC_BIT_XOR);
        appendExp(str, *b.getSubExp1(), PREC_BIT_XOR);
        str << " ^ ";
        appendExp(str, *b.getSubExp2(), PREC_BIT_XOR);
        closeParen(str, curPrec, PREC_BIT_XOR);
        break;

    case opNot:
        openParen(str, curPrec, PREC_UNARY);
        str << " ~";
        appendExp(str, *u.getSubExp1(), PREC_UNARY);
        closeParen(str, curPrec, PREC_UNARY);
        break;

    case opLNot:
        openParen(str, curPrec, PREC_UNARY);
        str << " !";
        appendExp(str, *u.getSubExp1(), PREC_UNARY);
        closeParen(str, curPrec, PREC_UNARY);
        break;

    case opNeg:
    case opFNeg:
        openParen(str, curPrec, PREC_UNARY);
        str << " -";
        appendExp(str, *u.getSubExp1(), PREC_UNARY);
        closeParen(str, curPrec, PREC_UNARY);
        break;

    case opAt:
        {
            // I guess that most people will find this easier to read
            // s1 >> last & 0xMASK
            openParen(str, curPrec, PREC_BIT_AND);
            appendExp(str, *t.getSubExp1(), PREC_BIT_SHIFT);
            auto first = std::static_pointer_cast<const Const>(t.getSubExp2());
            auto last  = std::static_pointer_cast<const Const>(t.getSubExp3());
            str << " >> ";
            appendExp(str, *last, PREC_BIT_SHIFT);
            str << " & ";

            unsigned int mask = (1 << (first->getInt() - last->getInt() + 1)) - 1;

            // print 0x3 as 3
            if (mask < 10) {
                str << mask;
            }
            else {
                str << "0x" << QString::number(mask, 16);
            }

            closeParen(str, curPrec, PREC_BIT_AND);
            break;
        }

    case opPlus:
        openParen(str, curPrec, PREC_ADD);
        appendExp(str, *b.getSubExp1(), PREC_ADD);
        str << " + ";
        appendExp(str, *b.getSubExp2(), PREC_ADD);
        closeParen(str, curPrec, PREC_ADD);
        break;

    case opMinus:
        openParen(str, curPrec, PREC_ADD);
        appendExp(str, *b.getSubExp1(), PREC_ADD);
        str << " - ";
        appendExp(str, *b.getSubExp2(), PREC_ADD);
        closeParen(str, curPrec, PREC_ADD);
        break;

    case opMemOf:
        if (SETTING(noDecompile)) {
            str << "MEMOF(";
            appendExp(str, *u.getSubExp1(), PREC_NONE);
            str << ")";
            break;
        }

        openParen(str, curPrec, PREC_UNARY);
        // annotateMemofs should have added a cast if it was needed
        str << "*";
        appendExp(str, *u.getSubExp1(), PREC_UNARY);
        closeParen(str, curPrec, PREC_UNARY);
        break;

    case opRegOf:
        {
            // MVE: this can likely go
            LOG_VERBOSE("Case opRegOf is deprecated");

            if (u.getSubExp1()->getOper() == opTemp) {
                // The great debate: r[tmpb] vs tmpb
                str << "tmp";
                break;
            }

            assert(u.getSubExp1()->getOper() == opIntConst);
            QString n(m_proc->getProg()->getRegName(std::static_pointer_cast<const Const>(u.getSubExp1())->getInt()));

            if (n.isEmpty()) {
                if (n[0] == '%') {
                    str << n + 1;
                }
                else {
                    str << n;
                }
            }
            else {
                // What is this doing in the back end???
                str << "r[";
                appendExp(str, *u.getSubExp1(), PREC_NONE);
                str << "]";
            }
        }
        break;

    case opTemp:
        // Should never see this; temps should be mapped to locals now so that they get declared
        LOG_VERBOSE("Case opTemp is deprecated");
        // Emit the temp name, e.g. "tmp1"
        str << u.access<Const, 1>()->getStr();
        break;

    case opItof:
        // TODO: MVE: needs work: float/double/long double.
        str << "(float)";
        openParen(str, curPrec, PREC_UNARY);
        appendExp(str, *t.getSubExp3(), PREC_UNARY);
        closeParen(str, curPrec, PREC_UNARY);
        break;

    case opFsize:

        // TODO: needs work!
        if (t.getSubExp3()->isMemOf()) {
            assert(t.getSubExp1()->isIntConst());
            int float_bits = t.access<Const, 1>()->getInt();

            if (SETTING(noDecompile)) {
                assert(t.getSubExp1()->isIntConst());

                if (float_bits == 32) {
                    str << "FLOAT_MEMOF(";
                }
                else {
                    str << "DOUBLE_MEMOF(";
                }

                appendExp(str, *t.getSubExp3()->getSubExp1(), PREC_NONE);
                str << ")";
            }
            else {
                switch (float_bits)
                {
                case 32:
                    str << "*((float *)&";
                    break;

                case 64:
                    str << "*((double *)&";
                    break;

                case 80:
                    str << "*((long double*)&";
                    break;
                }

                openParen(str, curPrec, curPrec);
                appendExp(str, *t.getSubExp3(), curPrec);
                closeParen(str, curPrec, curPrec);
                str << ")";
            }
        }
        else {
            appendExp(str, *t.getSubExp3(), curPrec);
        }

        break;

    case opMult:
    case opMults: // FIXME: check types
        openParen(str, curPrec, PREC_MULT);
        appendExp(str, *b.getSubExp1(), PREC_MULT);
        str << " * ";
        appendExp(str, *b.getSubExp2(), PREC_MULT);
        closeParen(str, curPrec, PREC_MULT);
        break;

    case opDiv:
    case opDivs: // FIXME: check types
        openParen(str, curPrec, PREC_MULT);
        appendExp(str, *b.getSubExp1(), PREC_MULT);
        str << " / ";
        appendExp(str, *b.getSubExp2(), PREC_MULT);
        closeParen(str, curPrec, PREC_MULT);
        break;

    case opMod:
    case opMods: // Fixme: check types
        openParen(str, curPrec, PREC_MULT);
        appendExp(str, *b.getSubExp1(), PREC_MULT);
        str << " % ";
        appendExp(str, *b.getSubExp2(), PREC_MULT);
        closeParen(str, curPrec, PREC_MULT);
        break;

    case opShiftL:
        openParen(str, curPrec, PREC_BIT_SHIFT);
        appendExp(str, *b.getSubExp1(), PREC_BIT_SHIFT);
        str << " << ";
        appendExp(str, *b.getSubExp2(), PREC_BIT_SHIFT);
        closeParen(str, curPrec, PREC_BIT_SHIFT);
        break;

    case opShiftR:
    case opShiftRA:
        openParen(str, curPrec, PREC_BIT_SHIFT);
        appendExp(str, *b.getSubExp1(), PREC_BIT_SHIFT);
        str << " >> ";
        appendExp(str, *b.getSubExp2(), PREC_BIT_SHIFT);
        closeParen(str, curPrec, PREC_BIT_SHIFT);
        break;

    case opTern:
        openParen(str, curPrec, PREC_COND);
        str << " (";
        appendExp(str, *t.getSubExp1(), PREC_NONE);
        str << ") ? ";
        appendExp(str, *t.getSubExp2(), PREC_COND);
        str << " : ";
        appendExp(str, *t.getSubExp3(), PREC_COND);
        closeParen(str, curPrec, PREC_COND);
        break;

    case opFPlus:
    case opFPlusd:
    case opFPlusq:
        openParen(str, curPrec, PREC_ADD);
        appendExp(str, *b.getSubExp1(), PREC_ADD);
        str << " + ";
        appendExp(str, *b.getSubExp2(), PREC_ADD);
        closeParen(str, curPrec, PREC_ADD);
        break;

    case opFMinus:
    case opFMinusd:
    case opFMinusq:
        openParen(str, curPrec, PREC_ADD);
        appendExp(str, *b.getSubExp1(), PREC_ADD);
        str << " - ";
        appendExp(str, *b.getSubExp2(), PREC_ADD);
        closeParen(str, curPrec, PREC_ADD);
        break;

    case opFMult:
    case opFMultd:
    case opFMultq:
        openParen(str, curPrec, PREC_MULT);
        appendExp(str, *b.getSubExp1(), PREC_MULT);
        str << " * ";
        appendExp(str, *b.getSubExp2(), PREC_MULT);
        closeParen(str, curPrec, PREC_MULT);
        break;

    case opFDiv:
    case opFDivd:
    case opFDivq:
        openParen(str, curPrec, PREC_MULT);
        appendExp(str, *b.getSubExp1(), PREC_MULT);
        str << " / ";
        appendExp(str, *b.getSubExp2(), PREC_MULT);
        closeParen(str, curPrec, PREC_MULT);
        break;

    case opFround:
        // Note: we need roundf or roundl depending on size of operands
        str << "round("; // Note: math.h required
        appendExp(str, *u.getSubExp1(), PREC_NONE);
        str << ")";
        break;

    case opFtrunc:
        // Note: we need truncf or truncl depending on size of operands
        str << "trunc("; // Note: math.h required
        appendExp(str, *u.getSubExp1(), PREC_NONE);
        str << ")";
        break;

    case opFabs:
        str << "fabs(";
        appendExp(str, *u.getSubExp1(), PREC_NONE);
        str << ")";
        break;

    case opFtoi:
        // Should check size!
        str << "(int)";
        appendExp(str, *u.getSubExp3(), PREC_UNARY);
        break;

    case opRotateL:
        str << "ROTL(";
        appendExp(str, *u.getSubExp1(), PREC_UNARY);
        str << ")";
        break;

    case opRotateR:
        str << "ROTR(";
        appendExp(str, *u.getSubExp1(), PREC_UNARY);
        str << ")";
        break;

    case opRotateLC:
        str << "ROTLC(";
        appendExp(str, *u.getSubExp1(), PREC_UNARY);
        str << ")";
        break;

    case opRotateRC:
        str << "ROTRC(";
        appendExp(str, *u.getSubExp1(), PREC_UNARY);
        str << ")";
        break;

    case opSize:

//         SharedType ty = new IntegerType(((Const*)b.getSubExp1())->getInt(), 1);
//         str << "*(" << ty->getCtype(true) << " *)";
//         appendExp(str, new Unary(opAddrOf, b.getSubExp2()), PREC_UNARY);
        appendExp(str, *b.getSubExp2(), PREC_UNARY);
        break;

    case opFMultsd:
    case opFMultdq:
    case opSQRTs:
    case opSQRTd:
    case opSQRTq:
    case opSignExt:
    case opTargetInst:
    case opNamedExp:
    case opGuard:
    case opVar:
    case opArg:
    case opExpand:
    case opCastIntStar:
    case opPostVar:
    case opForceInt:
    case opForceFlt:
    case opFpush:
    case opFpop:
    case opLoge:
    case opExecute:
    case opAFP:
    case opAGP:
        // not implemented
        LOG_WARN("Case %1 not implemented", exp.getOperName());
        // assert(false);
        break;

    case opFlagCall:
        {
            assert(b.getSubExp1()->getOper() == opStrConst);
            str << b.access<Const, 1>()->getStr();
            str << "(";
            auto l = b.getSubExp2();

            for ( ; l && l->getOper() == opList; l = l->getSubExp2()) {
                appendExp(str, *l->getSubExp1(), PREC_NONE);

                if (l->getSubExp2()->getOper() == opList) {
                    str << ", ";
                }
            }

            str << ")";
        }
        break;

    case opList:
        {
            int            elems_on_line = 0; // try to limit line lengths
            SharedConstExp b2            = b.shared_from_this();
            SharedConstExp e2            = b.getSubExp2();
            str << "{ ";

            if (b.getSubExp1()->getOper() == opList) {
                str << "\n ";
            }

            while (e2->getOper() == opList) {
                appendExp(str, *b2->getSubExp1(), PREC_NONE, uns);
                ++elems_on_line;

                if ((b2->getSubExp1()->getOper() == opList) ||
                    (elems_on_line >= 16) /* completely arbitrary, but better than nothing*/) {
                    str << ",\n ";
                    elems_on_line = 0;
                }
                else {
                    str << ", ";
                }

                b2 = e2;
                e2 = b2->getSubExp2();
            }

            appendExp(str, *b2->getSubExp1(), PREC_NONE, uns);
            str << " }";
        }
        break;

    case opFlags:
        str << "flags";
        break;

    case opPC:
        str << "pc";
        break;

    case opZfill:

        // MVE: this is a temporary hack... needs cast?
        // sprintf(s, "/* zfill %d->%d */ ",
        //    ((Const*)t.getSubExp1())->getInt(),
        //    ((Const*)t.getSubExp2())->getInt());
        // strcat(str, s); */
        if (t.getSubExp3()->isMemOf() && t.getSubExp1()->isIntConst() && t.getSubExp2()->isIntConst() &&
            (t.access<Const, 2>()->getInt() == 32)) {
            unsigned sz = (unsigned)t.access<Const, 1>()->getInt();

            if ((sz == 8) || (sz == 16)) {
                bool close = false;
                str << "*";
                str << "(unsigned ";

                if (sz == 8) {
                    str << "char";
                }
                else {
                    str << "short";
                }

                str << "*)";
                openParen(str, curPrec, PREC_UNARY);
                close = true;
                appendExp(str, *t.getSubExp3()->getSubExp1(), PREC_UNARY);

                if (close) {
                    closeParen(str, curPrec, PREC_UNARY);
                }

                break;
            }
        }

        LOG_VERBOSE("Case opZfill is deprecated");
        str << "(";
        appendExp(str, *t.getSubExp3(), PREC_NONE);
        str << ")";
        break;

    case opTypedExp:
        {
#if SYMS_IN_BACK_END
            Exp        *b   = u.getSubExp1();         // Base expression
            const char *sym = m_proc->lookupSym(exp); // Check for (cast)sym

            if (sym) {
                str << "(";
                appendType(str, ((TypedExp *)u)->getType());
                str << ")" << sym;
                break;
            }
#endif

            if ((u.getSubExp1()->getOper() == opTypedExp) &&
                (*((const TypedExp&)u).getType() == *u.access<TypedExp, 1>()->getType())) {
                // We have (type)(type)x: recurse with type(x)
                appendExp(str, *u.getSubExp1(), curPrec);
            }
            else if (u.getSubExp1()->getOper() == opMemOf) {
                // We have (tt)m[x]
                PointerType *pty = nullptr;

                // pty = T(x)
                SharedType tt = ((const TypedExp&)u).getType();

                if ((pty != nullptr) &&
                    ((*pty->getPointsTo() == *tt) || (tt->isSize() && (pty->getPointsTo()->getSize() == tt->getSize())))) {
                    str << "*";
                }
                else {
                    if (SETTING(noDecompile)) {
                        if (tt && tt->isFloat()) {
                            if (tt->as<FloatType>()->getSize() == 32) {
                                str << "FLOAT_MEMOF";
                            }
                            else {
                                str << "DOUBLE_MEMOF";
                            }
                        }
                        else {
                            str << "MEMOF";
                        }
                    }
                    else {
                        str << "*(";
                        appendType(str, tt);
                        str << "*)";
                    }
                }

                openParen(str, curPrec, PREC_UNARY);
                // Emit x
                // was : ((Location*)((TypedExp&)u).getSubExp1())->getSubExp1()
                appendExp(str, *u.getSubExp1()->getSubExp1(), PREC_UNARY);
                closeParen(str, curPrec, PREC_UNARY);
            }
            else {
                // Check for (tt)b where tt is a pointer; could be &local
                SharedType tt = ((TypedExp&)u).getType();

                if (std::dynamic_pointer_cast<PointerType>(tt)) {
#if SYMS_IN_BACK_END
                    const char *sym = m_proc->lookupSym(Location::memOf(b));

                    if (sym) {
                        openParen(str, curPrec, PREC_UNARY);
                        str << "&" << sym;
                        closeParen(str, curPrec, PREC_UNARY);
                        break;
                    }
#endif
                }

                // Otherwise, fall back to (tt)b
                str << "(";
                appendType(str, tt);
                str << ")";
                openParen(str, curPrec, PREC_UNARY);
                appendExp(str, *u.getSubExp1(), PREC_UNARY);
                closeParen(str, curPrec, PREC_UNARY);
            }

            break;
        }

    case opSgnEx:
    case opTruncs:
        {
            SharedConstExp s      = t.getSubExp3();
            int            toSize = t.access<Const, 2>()->getInt();

            switch (toSize)
            {
            case 8:
                str << "(char) ";
                break;

            case 16:
                str << "(short) ";
                break;

            case 64:
                str << "(long long) ";
                break;

            default:
                str << "(int) ";
                break;
            }

            appendExp(str, *s, curPrec);
            break;
        }

    case opTruncu:
        {
            SharedConstExp s      = t.getSubExp3();
            int            toSize = t.access<Const, 2>()->getInt();

            switch (toSize)
            {
            case 8:
                str << "(unsigned char) ";
                break;

            case 16:
                str << "(unsigned short) ";
                break;

            case 64:
                str << "(unsigned long long) ";
                break;

            default:
                str << "(unsigned int) ";
                break;
            }

            appendExp(str, *s, curPrec);
            break;
        }

    case opMachFtr:
        {
            str << "/* machine specific */ (int) ";
            auto sub = u.access<Const, 1>();
            assert(sub->isStrConst());
            QString s = sub->getStr();

            if (s[0] == '%') {   // e.g. %Y
                str << s.mid(1); // Just use Y
            }
            else {
                str << s;
            }

            break;
        }

    case opFflags:
        str << "/* Fflags() */ ";
        break;

    case opPow:
        str << "pow(";
        appendExp(str, *b.getSubExp1(), PREC_COMMA);
        str << ", ";
        appendExp(str, *b.getSubExp2(), PREC_COMMA);
        str << ")";
        break;

    case opLog2:
        str << "log2(";
        appendExp(str, *u.getSubExp1(), PREC_NONE);
        str << ")";
        break;

    case opLog10:
        str << "log10(";
        appendExp(str, *u.getSubExp1(), PREC_NONE);
        str << ")";
        break;

    case opSin:
        str << "sin(";
        appendExp(str, *u.getSubExp1(), PREC_NONE);
        str << ")";
        break;

    case opCos:
        str << "cos(";
        appendExp(str, *u.getSubExp1(), PREC_NONE);
        str << ")";
        break;

    case opSqrt:
        str << "sqrt(";
        appendExp(str, *u.getSubExp1(), PREC_NONE);
        str << ")";
        break;

    case opTan:
        str << "tan(";
        appendExp(str, *u.getSubExp1(), PREC_NONE);
        str << ")";
        break;

    case opArcTan:
        str << "atan(";
        appendExp(str, *u.getSubExp1(), PREC_NONE);
        str << ")";
        break;

    case opSubscript:
        appendExp(str, *u.getSubExp1(), curPrec);
        LOG_ERROR("Subscript in code generation of proc %1, exp (without subscript): %2", m_proc->getName(), str.readAll());
        break;

    case opMemberAccess:
        {
            SharedType ty = nullptr;

            if (ty == nullptr) {
                LOG_MSG("Type failure: no type for subexp1 of %1", b.shared_from_this());

                // ty = b.getSubExp1()->getType();
                // No idea why this is hitting! - trentw
                // str << "/* type failure */ ";
                // break;
            }

            // Trent: what were you thinking here? Fails for things like
            // local11.lhHeight (where local11 is a register)
            // Mike: it shouldn't!  local11 should have a compound type
            // assert(ty->resolvesToCompound());
            if (b.getSubExp1()->getOper() == opMemOf) {
                appendExp(str, *b.getSubExp1()->getSubExp1(), PREC_PRIM);
                str << "->";
            }
            else {
                appendExp(str, *b.getSubExp1(), PREC_PRIM);
                str << ".";
            }

            str << b.access<const Const, 2>()->getStr();
        }
        break;

    case opArrayIndex:
        openParen(str, curPrec, PREC_PRIM);

        if (b.getSubExp1()->isMemOf()) {
            SharedType ty = nullptr;

            if (ty && ty->resolvesToPointer() && ty->as<PointerType>()->getPointsTo()->resolvesToArray()) {
                // a pointer to an array is automatically dereferenced in C
                appendExp(str, *b.getSubExp1()->getSubExp1(), PREC_PRIM);
            }
            else {
                appendExp(str, *b.getSubExp1(), PREC_PRIM);
            }
        }
        else {
            appendExp(str, *b.getSubExp1(), PREC_PRIM);
        }

        closeParen(str, curPrec, PREC_PRIM);
        str << "[";
        appendExp(str, *b.getSubExp2(), PREC_PRIM);
        str << "]";
        break;

    case opDefineAll:
        str << "<all>";
        LOG_ERROR("Should not see opDefineAll in codegen");
        break;

    default:
        // others
        OPER other_op = exp.getOper();

        if (other_op >= opZF) {
            // Machine flags; can occasionally be manipulated individually
            // Chop off the "op" part
            str << exp.getOperName() + 2;
            break;
        }

        LOG_ERROR("case %1 not implemented", exp.getOperName());
    }
}


void CCodeGenerator::appendType(QTextStream& str, SharedType typ)
{
    if (!typ) {
        str << "int"; // Default type for C
        return;
    }

    if (typ->resolvesToPointer() && typ->as<PointerType>()->getPointsTo()->resolvesToArray()) {
        // C programmers prefer to see pointers to arrays as pointers
        // to the first element of the array.  They then use syntactic
        // sugar to access a pointer as if it were an array.
        typ = PointerType::get(typ->as<PointerType>()->getPointsTo()->as<ArrayType>()->getBaseType());
    }

    str << typ->getCtype(true);
}


void CCodeGenerator::appendTypeIdent(QTextStream& str, SharedType typ, QString ident)
{
    if (typ == nullptr) {
        return;
    }

    if (typ->isPointer() && typ->as<PointerType>()->getPointsTo()->isArray()) {
        appendType(str, typ->as<PointerType>()->getPointsTo()->as<ArrayType>()->getBaseType());
        str << " *" << ident;
    }
    else if (typ->isPointer()) {
        appendType(str, typ);
        str << ident;
    }
    else if (typ->isArray()) {
        auto a = typ->as<ArrayType>();
        appendTypeIdent(str, a->getBaseType(), ident);
        str << "[";

        if (!a->isUnbounded()) {
            str << a->getLength();
        }

        str << "]";
    }
    else if (typ->isVoid()) { // Can happen in e.g. twoproc, where really need global parameter and return analysis
        // TMN: Stop crashes by this workaround
        if (ident.isEmpty()) {
            ident = "unknownVoidType";
        }

        LOG_WARN("Declaring type void as int for %1", ident);
        str << "int " << ident;
    }
    else {
        appendType(str, typ);
        str << " " << (!ident.isEmpty() ? ident : "<null>");
    }
}


void CCodeGenerator::openParen(QTextStream& str, PREC outer, PREC inner)
{
    if (inner < outer) {
        str << "(";
    }
}


void CCodeGenerator::closeParen(QTextStream& str, PREC outer, PREC inner)
{
    if (inner < outer) {
        str << ")";
    }
}


void CCodeGenerator::generateCode(BasicBlock* bb, BasicBlock *latch, std::list<BasicBlock *>& followSet,
                              std::list<BasicBlock *>& gotoSet, UserProc *proc)
{
    // If this is the follow for the most nested enclosing conditional, then don't generate anything. Otherwise if it is
    // in the follow set generate a goto to the follow
    BasicBlock *enclFollow = followSet.empty() ? nullptr : followSet.back();

    if (Util::isIn(gotoSet, bb) && !bb->isLatchNode() &&
        ((latch && latch->getLoopHead() && (bb == latch->getLoopHead()->getLoopFollow())) || !bb->allParentsGenerated())) {
        emitGotoAndLabel(bb, bb);
        return;
    }
    else if (Util::isIn(followSet, bb)) {
        if (bb != enclFollow) {
            emitGotoAndLabel(bb, bb);
        }
        return;
    }

    // Has this node already been generated?
    if (bb->getTravType() == TravType::DFS_Codegen) {
        // this should only occur for a loop over a single block
        // FIXME: is this true? Perl_list (0x8068028) in the SPEC 2000 perlbmk seems to have a case with sType = Cond,
        // lType == PreTested, and latchNod == 0
        // assert(bb->sType == Loop && bb->lType == PostTested && latchNode == bb);
        return;
    }
    else {
        bb->setTravType(TravType::DFS_Codegen);
    }

    //
    // if this is a latchNode, there are 2 possibilities:
    // 1) The current indentation level is the same as the indentation of the first node of a loop.
    //    Just write out the BB.
    // 2) The indentation level is different.
    //    Can happen (?) when processing a "parent continue" of a double for loop, e.g.
    //    \code
    //        for (...) {
    //            for (...) {
    //               if (condition) {
    //                   goto label;
    //               }
    //               /* code ... */
    //            }
    //            /* code ... */
    //        label:
    //        }
    //    \endcode
    //
    if (bb->isLatchNode()) {
        // FIXME
//         if (latch && latch->getLoopHead() &&
//             (m_indent == latch->getLoopHead()->m_indentLevel + ((latch->m_loopHead->m_loopHeaderType == LoopType::PreTested) ? 1 : 0))) {
//             bb->WriteBB(this);
//         }
//         else {
//             // unset its traversed flag
//             bb->m_traversed = TravType::Untraversed;
//             emitGotoAndLabel(this, bb);
//         }
        writeBB(bb);
        return;
    }

    BasicBlock *child = nullptr;

    switch (bb->getStructType())
    {
    case StructType::Loop:
    case StructType::LoopCond:
        generateCode_Loop(bb, gotoSet, proc, latch, followSet);
        break;

    case StructType::Cond:
        {
            // reset this back to LoopCond if it was originally of this type
            if (bb->getLatchNode()) {
                bb->setStructType(StructType::LoopCond);
            }

            // for 2 way conditional headers that are effectively jumps into
            // or out of a loop or case body, we will need a new follow node
            BasicBlock *tmpCondFollow = nullptr;

            // keep track of how many nodes were added to the goto set so that
            // the correct number are removed
            int gotoTotal = 0;

            // add the follow to the follow set if this is a case header
            if (bb->getCondType() == CondType::Case) {
                followSet.push_back(bb->getCondFollow());
            }
            else if ((bb->getCondType() != CondType::Case) && bb->getCondFollow()) {
                // For a structured two conditional header,
                // its follow is added to the follow set
                // myLoopHead = (sType == LoopCond ? this : loopHead);

                if (bb->getUnstructType() == UnstructType::Structured) {
                    followSet.push_back(bb->getCondFollow());
                }

                // Otherwise, for a jump into/outof a loop body, the follow is added to the goto set.
                // The temporary follow is set for any unstructured conditional header branch that is within the
                // same loop and case.
                else {
                    if (bb->getUnstructType() == UnstructType::JumpInOutLoop) {
                        // define the loop header to be compared against
                        BasicBlock *myLoopHead = (bb->getStructType() == StructType::LoopCond ? bb : bb->getLoopHead());
                        gotoSet.push_back(bb->getCondFollow());
                        gotoTotal++;

                        // also add the current latch node, and the loop header of the follow if they exist
                        if (latch) {
                            gotoSet.push_back(latch);
                            gotoTotal++;
                        }

                        if (bb->getCondFollow()->getLoopHead() && (bb->getCondFollow()->getLoopHead() != myLoopHead)) {
                            gotoSet.push_back(bb->getCondFollow()->getLoopHead());
                            gotoTotal++;
                        }
                    }

                    tmpCondFollow = bb->getOutEdge((bb->getCondType() == CondType::IfThen) ? BELSE : BTHEN);

                    // for a jump into a case, the temp follow is added to the follow set
                    if (bb->getUnstructType() == UnstructType::JumpIntoCase) {
                        followSet.push_back(tmpCondFollow);
                    }
                }
            }

            // write the body of the block (excluding the predicate)
            writeBB(bb);

            // write the conditional header
            SWITCH_INFO *psi = nullptr; // Init to nullptr to suppress a warning

            if (bb->getCondType() == CondType::Case) {
                // The CaseStatement will be in the last RTL this BB
                RTL           *last = bb->getRTLList()->back();
                CaseStatement *cs   = (CaseStatement *)last->getHlStmt();
                psi = cs->getSwitchInfo();

                // Write the switch header (i.e. "switch(var) {")
                addCaseCondHeader(psi->pSwitchVar);
            }
            else {
                SharedExp cond = bb->getCond();

                if (!cond) {
                    cond = Const::get(Address(0xfeedface)); // hack, but better than a crash
                }

                if (bb->getCondType() == CondType::IfElse) {
                    cond = Unary::get(opNot, cond->clone());
                    cond = cond->simplify();
                }

                if (bb->getCondType() == CondType::IfThenElse) {
                    addIfElseCondHeader(cond);
                }
                else {
                    addIfCondHeader(cond);
                }
            }

            // write code for the body of the conditional
            if (bb->getCondType() != CondType::Case) {
                BasicBlock *succ = bb->getOutEdge((bb->getCondType() == CondType::IfElse) ? BELSE : BTHEN);

                // emit a goto statement if the first clause has already been
                // generated or it is the follow of this node's enclosing loop
                if ((succ->getTravType() == TravType::DFS_Codegen) || (bb->getLoopHead() && (succ == bb->getLoopHead()->getLoopFollow()))) {
                    emitGotoAndLabel(bb, succ);
                }
                else {
                    generateCode(succ, latch, followSet, gotoSet, proc);
                }

                // generate the else clause if necessary
                if (bb->getCondType() == CondType::IfThenElse) {
                    // generate the 'else' keyword and matching brackets
                    addIfElseCondOption();

                    succ = bb->getOutEdge(BELSE);

                    // emit a goto statement if the second clause has already
                    // been generated
                    if (succ->getTravType() == TravType::DFS_Codegen) {
                        emitGotoAndLabel(bb, succ);
                    }
                    else {
                        generateCode(succ, latch, followSet, gotoSet, proc);
                    }

                    // generate the closing bracket
                    addIfElseCondEnd();
                }
                else {
                    // generate the closing bracket
                    addIfCondEnd();
                }
            }
            else { // case header
                // TODO: linearly emitting each branch of the switch does not result in optimal fall-through.
                // generate code for each out branch
                for (unsigned int i = 0; i < bb->getOutEdges().size(); i++) {
                    // emit a case label
                    // FIXME: Not valid for all switch types
                    Const caseVal(0);

                    if (psi->chForm == 'F') {                            // "Fortran" style?
                        caseVal.setInt(((int *)psi->uTable.value())[i]); // Yes, use the table value itself
                    }
                    // Note that uTable has the address of an int array
                    else {
                        caseVal.setInt((int)(psi->iLower + i));
                    }

                    addCaseCondOption(caseVal);

                    // generate code for the current out-edge
                    BasicBlock *succ = bb->getOutEdge(i);

                    // assert(succ->caseHead == this || succ == condFollow || HasBackEdgeTo(succ));
                    if (succ->getTravType() == TravType::DFS_Codegen) {
                        emitGotoAndLabel(bb, succ);
                    }
                    else {
                        generateCode(succ, latch, followSet, gotoSet, proc);
                    }
                }

                // generate the closing bracket
                addCaseCondEnd();
            }

            // do all the follow stuff if this conditional had one
            if (bb->getCondFollow()) {
                // remove the original follow from the follow set if it was
                // added by this header
                if ((bb->getUnstructType() == UnstructType::Structured) || (bb->getUnstructType() == UnstructType::JumpIntoCase)) {
                    assert(gotoTotal == 0);
                    followSet.resize(followSet.size() - 1);
                }
                else { // remove all the nodes added to the goto set
                    gotoSet.resize(std::max((int)gotoSet.size() - gotoTotal, 0));
                }

                // do the code generation (or goto emitting) for the new conditional follow if it exists, otherwise do
                // it for the original follow
                if (!tmpCondFollow) {
                    tmpCondFollow = bb->getCondFollow();
                }

                if (tmpCondFollow->getTravType() == TravType::DFS_Codegen) {
                    emitGotoAndLabel(bb, tmpCondFollow);
                }
                else {
                    generateCode(tmpCondFollow, latch, followSet, gotoSet, proc);
                }
            }

            break;
        }

    case StructType::Seq:
        // generate code for the body of this block
        writeBB(bb);

        // return if this is the 'return' block (i.e. has no out edges) after emmitting a 'return' statement
        if (bb->getType() == BBType::Ret) {
            // This should be emitted now, like a normal statement
            // addReturnStatement(getReturnVal());
            return;
        }

        // return if this doesn't have any out edges (emit a warning)
        if (bb->getOutEdges().empty()) {
            LOG_WARN("No out edge for BB at address %1, in proc %2", bb->getLowAddr(), proc->getName());

            if (bb->getType() == BBType::CompJump) {
                assert(!bb->getRTLList()->empty());
                RTL *lastRTL = bb->getRTLList()->back();
                assert(!lastRTL->empty());
                GotoStatement *gs = (GotoStatement *)lastRTL->back();

                QString     dat;
                QTextStream ost(&dat);

                ost << "goto " << gs->getDest();
                addLineComment(dat);
            }

            return;
        }

        child = bb->getOutEdge(0);

        if (bb->getOutEdges().size() > 1) {
            BasicBlock *other = bb->getOutEdge(1);
            LOG_MSG("Found seq with more than one outedge!");
            auto const_dest = std::static_pointer_cast<Const>(bb->getDest());

            if (const_dest->isIntConst() && (const_dest->getAddr() == child->getLowAddr())) {
                other = child;
                child = bb->getOutEdge(1);
                LOG_MSG("Taken branch is first out edge");
            }

            try {
                addIfCondHeader(bb->getCond());

                if (other->getTravType() == TravType::DFS_Codegen) {
                    emitGotoAndLabel(bb, other);
                }
                else {
                    generateCode(other, latch, followSet, gotoSet, proc);
                }

                addIfCondEnd();
            }
            catch (BasicBlock::LastStatementNotABranchError&) {
                LOG_ERROR("Last statement is not a cond, don't know what to do with this.");
            }
        }

        // generate code for its successor if it hasn't already been visited and is in the same loop/case and is not
        // the latch for the current most enclosing loop.     The only exception for generating it when it is not in
        // the same loop is when it is only reached from this node
        if ((child->getTravType() == TravType::DFS_Codegen) ||
            ((child->getLoopHead() != bb->getLoopHead()) && (!child->allParentsGenerated() ||
            Util::isIn(followSet, child))) ||
            (latch && latch->getLoopHead() && (latch->getLoopHead()->getLoopFollow() == child)) ||
            !((bb->getCaseHead() == child->getCaseHead()) || (bb->getCaseHead() && (child == bb->getCaseHead()->getCondFollow())))) {
                emitGotoAndLabel(bb, child);
        }
        else {
            if (bb->getCaseHead() && (child == bb->getCaseHead()->getCondFollow())) {
                // generate the 'break' statement
                addCaseCondOptionEnd();
            }
            else if ((bb->getCaseHead() == nullptr) || (bb->getCaseHead() != child->getCaseHead()) || !child->isCaseOption()) {
                generateCode(child, latch, followSet, gotoSet, proc);
            }
        }

        break;

    default:
        LOG_ERROR("Unhandled structuring type %1", (int)bb->getStructType());
    }
}

void CCodeGenerator::generateCode_Loop(BasicBlock *bb, std::list<BasicBlock *>& gotoSet, UserProc *proc,
                                   BasicBlock *latch, std::list<BasicBlock *>& followSet)
{
    // add the follow of the loop (if it exists) to the follow set
    if (bb->getLoopFollow()) {
        followSet.push_back(bb->getLoopFollow());
    }

    if (bb->getLoopType() == LoopType::PreTested) {
        assert(bb->getLatchNode()->getOutEdges().size() == 1);

        // write the body of the block (excluding the predicate)
        writeBB(bb);

        // write the 'while' predicate
        SharedExp cond = bb->getCond();

        if (bb->getOutEdge(BTHEN) == bb->getLoopFollow()) {
            cond = Unary::get(opNot, cond);
            cond = cond->simplify();
        }

        addPretestedLoopHeader(cond);

        // write the code for the body of the loop
        BasicBlock *loopBody = (bb->getOutEdge(BELSE) == bb->getLoopFollow()) ? bb->getOutEdge(BTHEN) : bb->getOutEdge(BELSE);
        generateCode(loopBody, bb->getLatchNode(), followSet, gotoSet, proc);

        // if code has not been generated for the latch node, generate it now
        if (bb->getLatchNode()->getTravType() != TravType::DFS_Codegen) {
            bb->getLatchNode()->setTravType(TravType::DFS_Codegen);
            writeBB(bb->getLatchNode());
        }

        // rewrite the body of the block (excluding the predicate) at the next nesting level after making sure
        // another label won't be generated
        writeBB(bb);

        // write the loop tail
        addPretestedLoopEnd();
    }
    else {
        // write the loop header
        if (bb->getLoopType() == LoopType::Endless) {
            addEndlessLoopHeader();
        }
        else {
            addPostTestedLoopHeader();
        }

        // if this is also a conditional header, then generate code for the conditional. Otherwise generate code
        // for the loop body.
        if (bb->getStructType() == StructType::LoopCond) {
            // set the necessary flags so that generateCode can successfully be called again on this node
            bb->setStructType(StructType::Cond);
            bb->setTravType(TravType::Untraversed);
            generateCode(bb, bb->getLatchNode(), followSet, gotoSet, proc);
        }
        else {
            writeBB(bb);

            // write the code for the body of the loop
            generateCode(bb->getOutEdge(0), bb->getLatchNode(), followSet, gotoSet, proc);
        }

        if (bb->getLoopType() == LoopType::PostTested) {
            // if code has not been generated for the latch node, generate it now
            if (bb->getLatchNode()->getTravType() != TravType::DFS_Codegen) {
                bb->getLatchNode()->setTravType(TravType::DFS_Codegen);
                writeBB(bb->getLatchNode());
            }

            // addPosttestedLoopEnd(getCond());
            // MVE: the above seems to fail when there is a call in the middle of the loop (so loop is 2 BBs)
            // Just a wild stab:
            addPostTestedLoopEnd(bb->getLatchNode()->getCond());
        }
        else {
            assert(bb->getLoopType() == LoopType::Endless);

            // if code has not been generated for the latch node, generate it now
            if (bb->getLatchNode()->getTravType() != TravType::DFS_Codegen) {
                bb->getLatchNode()->setTravType(TravType::DFS_Codegen);
                writeBB(bb->getLatchNode());
            }

            // write the closing bracket for an endless loop
            addEndlessLoopEnd();
        }
    }

    // write the code for the follow of the loop (if it exists)
    if (bb->getLoopFollow()) {
        // remove the follow from the follow set
        followSet.pop_back();

        if (bb->getLoopFollow()->getTravType() != TravType::DFS_Codegen) {
            generateCode(bb->getLoopFollow(), latch, followSet, gotoSet, proc);
        }
        else {
            emitGotoAndLabel(bb, bb->getLoopFollow());
        }
    }
}



void CCodeGenerator::emitGotoAndLabel(BasicBlock* bb, BasicBlock *dest)
{
    if (bb->getLoopHead() && ((bb->getLoopHead() == dest) || (bb->getLoopHead()->getLoopFollow() == dest))) {
        if (bb->getLoopHead() == dest) {
            addContinue();
        }
        else {
            addBreak();
        }
    }
    else {
        addGoto(dest->getOrdering());
    }
}


void CCodeGenerator::writeBB(BasicBlock* bb)
{
    if (DEBUG_GEN) {
        LOG_MSG("Generating code for BB at address %1", bb->getLowAddr());
    }

    // Allocate space for a label to be generated for this node and add this to the generated code. The actual label can
    // then be generated now or back patched later
    addLabel(bb->getOrdering());

    if (bb->getRTLList()) {
        for (RTL *rtl : *(bb->getRTLList())) {
            if (DEBUG_GEN) {
                LOG_MSG("%1", rtl->getAddress());
            }

            for (Statement *st : *rtl) {
                st->generateCode(this, bb);
            }
        }
    }
}


void CCodeGenerator::print(QTextStream& os)
{
    os << m_lines.join('\n') << '\n';
}


void CCodeGenerator::indent(QTextStream& str, int indLevel)
{
    // Can probably do more efficiently
    for (int i = 0; i < indLevel; i++) {
        str << "    ";
    }
}


void CCodeGenerator::appendLine(const QString& s)
{
    m_lines.push_back(s);
}
