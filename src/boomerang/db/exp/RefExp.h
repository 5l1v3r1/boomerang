#pragma once

#include "boomerang/db/exp/Unary.h"

/***************************************************************************/ /**
* RefExp is a subclass of Unary, holding an ordinary Exp pointer, and a pointer to a defining statement (could be a
* phi assignment).  This is used for subscripting SSA variables. Example:
*   m[1000] becomes m[1000]{3} if defined at statement 3
* The integer is really a pointer to the defining statement, printed as the statement number for compactness.
******************************************************************************/
class RefExp : public Unary
{
private:
    Instruction *m_def; // The defining statement
    // Constructor with expression (e) and statement defining it (def)

public:
    RefExp(SharedExp e, Instruction *def);
    virtual ~RefExp()
    {
        m_def = nullptr;
    }

    static std::shared_ptr<RefExp> get(SharedExp e, Instruction *def);
    SharedExp clone() const override;
    bool operator==(const Exp& o) const override;
    bool operator<(const Exp& o) const override;
    bool operator*=(const Exp& o) const override;

    virtual void print(QTextStream& os, bool html = false) const override;
    virtual void printx(int ind) const override;

    Instruction *getDef() const { return m_def; } // Ugh was called getRef()
    SharedExp addSubscript(Instruction *_def)
    {
        m_def = _def;
        return shared_from_this();
    }

    void setDef(Instruction *_def)   /*assert(_def);*/
    {
        m_def = _def;
    }

    SharedExp genConstraints(SharedExp restrictTo) override;

    bool references(const Instruction *s) const { return m_def == s; }
    virtual SharedExp polySimplify(bool& bMod) override;
    virtual SharedExp match(const SharedConstExp& pattern) override;
    virtual bool match(const QString& pattern, std::map<QString, SharedConstExp>& bindings) override;

    // Before type analysis, implicit definitions are nullptr.
    // During and after TA, they point to an implicit assignment statement.
    bool isImplicitDef() const;

    // Visitation
    virtual bool accept(ExpVisitor *v) override;
    virtual SharedExp accept(ExpModifier *v) override;

    virtual SharedType ascendType() override;
    virtual void descendType(SharedType parentType, bool& ch, Instruction *s) override;

protected:
    RefExp()
    : Unary(opSubscript)
    , m_def(nullptr) {}
};
