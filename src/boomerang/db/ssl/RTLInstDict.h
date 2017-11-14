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


#include <QString>
#include <QTextStream>

#include <list>
#include <set>

#include <memory>

#include "boomerang/util/Address.h"
#include "boomerang/db/Register.h"
#include "boomerang/db/RTL.h"

class Statement;

class Exp;
class Type;

using SharedExp = std::shared_ptr<Exp>;
using SharedRTL = std::shared_ptr<RTL>;



/**
 * The TableEntry class represents a single instruction - a string/RTL pair.
 */
class TableEntry
{
public:
    TableEntry();
    TableEntry(std::list<QString>& p, RTL& rtl);

    /**
     * Sets the contents of this object with a deepcopy from another TableEntry object.
     * Note that this is different from the semantics of operator= for an RTL
     * which only does a shallow copy!
     *
     * \param other - the object to copy
     * \returns a reference to this object
     */
    TableEntry& operator=(const TableEntry& other);

    /// Set the parameter list.
    void setParam(std::list<QString>& p);

    /// Set the RTL.
    void setRTL(RTL& rtl);

    /**
     * Appends an RTL to an exising TableEntry
     * \param        p reference to list of formal parameters (as strings)
     * \param        rtl reference to RTL with list of Exps to append
     * \returns      0 for success, non-zero for failure
     */
    int appendRTL(std::list<QString>& p, RTL& rtl);

public:
    std::list<QString> m_params;
    RTL m_rtl;

    int m_flags; // aka required capabilities. Init. to 0
};



typedef enum
{
    PARAM_SIMPLE,
    PARAM_ASGN,
    PARAM_LAMBDA,
    PARAM_VARIANT
} ParamKind;


/**
 * The ParamEntry struct represents the details of a single parameter.
 */
struct ParamEntry
{
public:
    std::list<QString> m_params;          ///< PARAM_VARIANT & PARAM_ASGN only */
    std::list<QString> m_funcParams;      ///< PARAM_LAMBDA - late bound params */
    Statement          *m_asgn = nullptr; ///< PARAM_ASGN only */
    bool               m_lhs   = false;   ///< True if this param ever appears on the LHS of an expression */
    ParamKind          m_kind  = PARAM_SIMPLE;
    SharedType         m_regType;         ///< Type of r[this], if any (void otherwise)
    std::set<int>      m_regIdx;          ///< Values this param can take as an r[param]
    int                m_mark = 0;        ///< Traversal mark. (free temporary use, basically)

protected:
    SharedType         m_type;
};


/**
 * The RTLInstDict represents a dictionary that maps instruction names to the
 * parameters they take and a template for the Exp list describing their
 * semantics. It handles both the parsing of the SSL file that fills in
 * the dictionary entries as well as instantiation of an Exp list for a given
 * instruction name and list of actual parameters.
 */
class RTLInstDict
{
    friend class SSLParser;
    friend class NJMCDecoder;

public:
    RTLInstDict();
    ~RTLInstDict();

    /**
     * Read and parse the SSL file, and initialise the expanded instruction dictionary
     * (this object). This also reads and sets up the register map and flag functions.
     *
     * \param sslFileName - the name of the file containing the SSL specification.
     * \returns        true if the file was successfully read
     */
    bool readSSLFile(const QString& sslFileName);

    /**
     * Reset the object to "undo" a readSSLFile()
     * Called from test code if (e.g.) want to call readSSLFile() twice
     */
    void reset();

    /// \returns the name and the number of operands of the instruction wwith name \p name
    std::pair<QString, DWord> getSignature(const char *name);

    /**
     * Returns an instance of a register transfer list for the instruction named 'name' with the actuals
     * given as the second parameter.
     *
     * \param name    the name of the instruction (must correspond to one defined in the SSL file).
     * \param pc      address at which the named instruction is located
     * \param actuals the actual values
     * \returns       the instantiated list of Exps
     */
    std::list<Statement *> *instantiateRTL(const QString& name, Address pc, const std::vector<SharedExp>& actuals);

    /**
     * Returns an instance of a register transfer list for the parameterized rtlist with the given formals
     * replaced with the actuals given as the third parameter.
     *
     * \param   rtls    a register transfer list
     * \param   pc      address at which the named instruction is located
     * \param   params  a list of formal parameters
     * \param   actuals the actual parameter values
     * \returns the instantiated list of Exps
     */
    std::list<Statement *> *instantiateRTL(RTL& rtls, Address pc, std::list<QString>& params,
                                           const std::vector<SharedExp>& actuals);

private:
    /**
     * Appends one RTL to the dictionary, or adds it to idict if an
     * entry does not already exist.
     *
     * \param name name of the instruction to add to
     * \param parameters list of formal parameters (as strings) for the RTL to add
     * \param rtl reference to the RTL to add
     * \returns zero for success, non-zero for failure
     */
    int insert(const QString& name, std::list<QString>& parameters, RTL& rtl);

    /**
     * Transform an RTL to eliminate any uses of post-variables by either
     * adding temporaries or just removing them where possible.
     *
     * Note that the algorithm used expects to deal with simple expressions
     * as post vars, ie r[22], m[r[1]], generally things which aren't parameterized
     * at a higher level. This is OK for the translator (we do substitution
     * first anyway), but may miss some optimizations for the emulator.
     * For the emulator, if parameters are detected within a postvar,
     * we just force the temporary, which is always safe to do. (The parameter
     * \p optimize is set to false for the emulator to achieve this).
     *
     * \param rts the list of statements
     * \param optimize - try to remove temporary registers
     */
    void transformPostVars(std::list<Statement *>& rts, bool optimize);

    /// Print a textual representation of the dictionary.
    void print(QTextStream& os);

    /**
     * Add a new register definition to the dictionary
     * \param name register's name
     * \param size - register size in bits
     * \param flt  - is float register?
     */
    void addRegister(const QString& name, int id, int size, bool flt);

    /**
     * Scan the Exp* pointed to by exp; if its top level operator indicates even a partial type, then set
     * the expression's type, and return true
     * \note This version only inspects one expression
     *
     * \param  exp - points to a Exp* to be scanned
     * \param  ty - ref to a Type object to put the partial type into
     * \returns true if a partial type is found
     */
    bool partialType(Exp *exp, Type& ty);

    /**
     * Runs after the ssl file is parsed to fix up variant params
     * where the arms are lambdas.
     */
    void fixupParams();

    void fixupParamsSub(const QString& s, std::list<QString>& funcParams, bool& haveCount, int mark);

    /// An RTL describing the machine's basic fetch-execute cycle
    SharedRTL fetchExecCycle;

    /// A map from the symbolic representation of a register (e.g. "%g0") to its index within an array of registers.
    std::map<QString, int, std::less<QString> > RegMap;

    /// Similar to r_map but stores more info about a register such as its size, its addresss etc (see register.h).
    std::map<int, Register, std::less<int> > DetRegMap;


    /// A set of parameter names, to make sure they are declared (?).
    /// Was map from string to SemTable index
    std::set<QString> ParamSet;

    /// Parameter (instruction operand, more like addressing mode) details (where given)
    QMap<QString, ParamEntry> DetParamMap;

    /// The maps which summarise the semantics (.ssl) file
    std::map<QString, SharedExp> FlagFuncs;
    /// Map from ordinary instruction to fast pseudo instruction, for use with -f (fast but not as exact) switch
    std::map<QString, QString> fastMap;

    bool m_bigEndian; // True if this source is big endian

    /// A map from symbolic representation of a special (non-addressable) register to a Register object
    std::map<QString, Register, std::less<QString> > SpecialRegMap;

    std::map<QString, std::pair<int, void *> *> DefMap;

    std::map<int, SharedExp> AliasMap;

    /// The actual dictionary.
    std::map<QString, TableEntry, std::less<QString> > idict;
};
