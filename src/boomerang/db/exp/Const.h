#pragma region License
/*
 * This file is part of the Boomerang Decompiler.
 *
 * See the file "LICENSE.TERMS" for information on usage and
 * redistribution of this file, and for a DISCLAIMER OF ALL
 * WARRANTIES.
 */
#pragma endregion License
#pragma once


#include "boomerang/util/Types.h"
#include "boomerang/db/proc/Proc.h"
#include "boomerang/db/exp/Exp.h"


/**
 * Const is a subclass of Exp, and holds either an integer,
 * floating point, string, or address constant
 */
class Const : public Exp
{
private:
    union Data
    {
        int    i;      ///< Integer
        QWord  ll;     ///< 64 bit integer / address / pointer
        double d;      ///< Double precision float

        /// Don't store string: function could be renamed
        Function *pp;  ///< Pointer to function (e.g. global function pointers)
    };

public:
    // Special constructors overloaded for the various constants
    Const(uint32_t i);
    Const(int i);
    Const(QWord ll);

    /// \remark This is bad. We need a way of constructing true unsigned constants
    Const(Address a);
    Const(double d);
    Const(const QString& p);
    Const(Function *p);

    Const(const Const& other);
    Const(Const&& other) = default;

    /// Nothing to destruct: Don't deallocate the string passed to constructor
    virtual ~Const() override = default;

    Const& operator=(const Const&) = default;
    Const& operator=(Const&&) = default;

public:
    /// \copydoc Exp::clone
    virtual SharedExp clone() const override;

    template<class T>
    static std::shared_ptr<Const> get(T i) { return std::make_shared<Const>(i); }

    /// \copydoc Exp::operator==
    virtual bool operator==(const Exp& o) const override;

    /// \copydoc Exp::operator<
    virtual bool operator<(const Exp& o) const override;

    /// \copydoc Exp::operator*=
    virtual bool operator*=(const Exp& o) const override;

    // Get the constant
    int getInt() const { return m_value.i; }
    QWord getLong() const { return m_value.ll; }
    double getFlt() const { return m_value.d; }
    QString getStr() const { return m_string; }
    Address getAddr() const { return Address((Address::value_type)m_value.ll); }
    QString getFuncName() const;

    // Set the constant
    void setInt(int i) { m_value.i = i; }
    void setLong(QWord ll) { m_value.ll = ll; }
    void setFlt(double d) { m_value.d = d; }
    void setStr(const QString& p) { m_string = p; }
    void setAddr(Address a) { m_value.ll = a.value(); }

    /// \returns the type of the constant
    SharedType getType() { return m_type; }
    const SharedType getType() const { return m_type; }

    /// Changes the type of this constant
    void setType(SharedType ty) { m_type = ty; }

    /// \copydoc Exp::print
    virtual void print(QTextStream& os, bool = false) const override;

    /// \copydoc Exp::printx
    virtual void printx(int ind) const override;

    /// Print "recursive" (extra parens not wanted at outer levels)
    void printNoQuotes(QTextStream& os) const;

    /// \copydoc Exp:;:appendDotFile
    virtual void appendDotFile(QTextStream& of) override;

    /// \copydoc Exp::accept
    virtual bool accept(ExpVisitor *v) override;

    /// \copydoc Exp::accept
    virtual SharedExp accept(ExpModifier *v) override;

    /// \copydoc Exp::ascendType
    virtual SharedType ascendType() override;

    /// \copydoc Exp::descendType
    virtual void descendType(SharedType parentType, bool& ch, Statement *s) override;

    int getConscript() const { return m_conscript; }
    void setConscript(int cs) { m_conscript = cs; }

private:
    Data m_value;      ///< The value of this constant
    QString m_string;  ///< The string value of this constant
    int m_conscript;   ///< like a subscript for constants
    SharedType m_type; ///< Constants need types during type analysis
};
