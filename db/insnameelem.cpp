/*
 * Copyright (C) 2001, The University of Queensland
 * Copyright (C) 2002, Trent Waddington
 *
 * See the file "LICENSE.TERMS" for information on usage and
 * redistribution of this file, and for a DISCLAIMER OF ALL
 * WARRANTIES.
 *
 */

/**
 * \file insnameelem.cpp
 *
 * an element of an instruction name - contains definition of class InsNameElem
 *
 */
/* Changelog:
 * 19 Feb 01 - Simon: created
 * 01 May 02 - Mike: Mods for boomerang
 */

#include "types.h"
#include "insnameelem.h"

#include <cassert>
#include <string>
#include <map>

InsNameElem::InsNameElem(const QString &name) {
    elemname = name;
    value = 0;
    nextelem = nullptr;
}

InsNameElem::~InsNameElem(void) {
    //      delete nextelem;
}

size_t InsNameElem::ntokens(void) { return 1; }

QString InsNameElem::getinstruction(void) {
    return (nextelem != nullptr) ? (elemname + nextelem->getinstruction()) : elemname;
}

QString InsNameElem::getinspattern(void) {
    return (nextelem != nullptr) ? (elemname + nextelem->getinspattern()) : elemname;
}

void InsNameElem::getrefmap(std::map<QString, InsNameElem *> &m) {
    if (nextelem != nullptr)
        nextelem->getrefmap(m);
    else
        m.clear();
}

int InsNameElem::ninstructions(void) {
    return (nextelem != nullptr) ? (nextelem->ninstructions() * ntokens()) : ntokens();
}

void InsNameElem::append(std::shared_ptr<InsNameElem> next) {
    if (nextelem == nullptr)
        nextelem = next;
    else
        nextelem->append(next);
}

bool InsNameElem::increment(void) {
    if ((nextelem == nullptr) || nextelem->increment())
        value++;
    if (value >= ntokens()) {
        value = 0;
        return true;
    }
    return false;
}

void InsNameElem::reset(void) {
    value = 0;
    if (nextelem != nullptr)
        nextelem->reset();
}

int InsNameElem::getvalue(void) const { return value; }

InsOptionElem::InsOptionElem(const QString &name) : InsNameElem(name) {}

size_t InsOptionElem::ntokens(void) { return 2; }

QString InsOptionElem::getinstruction(void) {
    QString s = (nextelem != nullptr)
                        ? ((getvalue() == 0) ? (elemname + nextelem->getinstruction()) : nextelem->getinstruction())
                        : ((getvalue() == 0) ? elemname : "");
    return s;
}

QString InsOptionElem::getinspattern(void) {
    return (nextelem != nullptr) ? ('\'' + elemname + '\'' + nextelem->getinspattern()) : ('\'' + elemname + '\'');
}

InsListElem::InsListElem(const QString &name, Table *t, const QString &idx) : InsNameElem(name) {
    indexname = idx;
    thetable = t;
}

size_t InsListElem::ntokens(void) { return thetable->Records.size(); }

QString InsListElem::getinstruction(void) {
    return (nextelem != nullptr) ? (thetable->Records[getvalue()] + nextelem->getinstruction())
                                 : thetable->Records[getvalue()];
}

QString InsListElem::getinspattern(void) {
    return (nextelem != nullptr) ? (elemname + '[' + indexname + ']' + nextelem->getinspattern())
                                 : (elemname + '[' + indexname + ']');
}

void InsListElem::getrefmap(std::map<QString, InsNameElem *> &m) {
    if (nextelem != nullptr)
        nextelem->getrefmap(m);
    else
        m.clear();
    m[indexname] = this;
    // of course, we're assuming that we've already checked (try in the parser)
    // that indexname hasn't been used more than once on this line ..
}

QString InsListElem::getindex(void) const { return indexname; }
