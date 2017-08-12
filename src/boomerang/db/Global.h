#pragma once

#include "boomerang/util/Address.h"
#include "boomerang/util/Util.h"
#include "boomerang/type/Type.h"

class Prog;

/**
 *
 */
class Global : public Printable
{
public:
    Global(SharedType type, Address addr, const QString& name, Prog *prog);
    virtual ~Global() {}

    /// @copydoc Printable::toString
    QString toString() const override;

    SharedType getType() const { return m_type; }
    void setType(SharedType ty) { m_type = ty; }
    void meetType(SharedType ty);

    Address getAddress()     const { return m_addr; }
    const QString& getName() const { return m_name; }

    /// return true if \p address is contained within this global.
    bool containsAddress(Address addr) const;

    /// Get the initial value as an expression (or nullptr if not initialised)
    SharedExp getInitialValue(const Prog* prog) const;

protected:
    Global();

private:
    SharedType m_type;
    Address m_addr;
    QString m_name;
    Prog *m_program;
};
