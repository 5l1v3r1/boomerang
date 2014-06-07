/*
 * Copyright (C) 2001, The University of Queensland
 *
 * See the file "LICENSE.TERMS" for information on usage and
 * redistribution of this file, and for a DISCLAIMER OF ALL
 * WARRANTIES.
 *
 */
#ifndef INSNAMEELEM_H
#define INSNAMEELEM_H

#include "table.h"

#include <QString>
#include <map>
#include <memory>

class InsNameElem {

public:
    InsNameElem(const QString &name);
    virtual ~InsNameElem(void);
    virtual size_t ntokens(void);
    virtual QString getinstruction(void);
    virtual QString getinspattern(void);
    virtual void getrefmap(std::map<QString, InsNameElem *> &m);

    int ninstructions(void);
    void append(std::shared_ptr<InsNameElem> next);
    bool increment(void);
    void reset(void);
    int getvalue(void) const;

protected:
    std::shared_ptr<InsNameElem> nextelem;
    QString elemname;
    size_t value;
};

class InsOptionElem : public InsNameElem {

public:
    InsOptionElem(const char *name);
    virtual size_t ntokens(void) override;
    virtual QString getinstruction(void);
    virtual QString getinspattern(void);
};

class InsListElem : public InsNameElem {

public:
    InsListElem(const char *name, Table *t, const char *idx);
    virtual size_t ntokens(void) override;
    virtual QString getinstruction(void);
    virtual QString getinspattern(void);
    virtual void getrefmap(std::map<QString, InsNameElem *> &m);

    QString getindex(void) const;

protected:
    QString indexname;
    Table *thetable;
};

#endif
