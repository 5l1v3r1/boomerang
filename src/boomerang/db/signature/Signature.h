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


#include "boomerang/type/type/Type.h"
#include "boomerang/db/statements/Assign.h"
#include "boomerang/db/exp/Operator.h"
#include "boomerang/frontend/SigEnum.h"   // For enums platform and cc

#include "boomerang/db/signature/Parameter.h"
#include "boomerang/db/signature/Return.h"


class Statement;
class StatementList;
class BinaryFile;
class Exp;


/**
 * \note Trent had the idea of "promoting" to signatures with known behaviour (e.g. conforms to ABI).
 * However, it seems more general to only assume ABI behaviour for library functions,
 * and derive the signature information from child procedures in all user procedures.
 * At present, this promotion is basically disabled (promotion always succeeds,
 * but not much is assumed by the process of promotion). The role of the Signature classes is still being
 * considered.
 *    - MVE Jun 2005.
 */
class Signature : public std::enable_shared_from_this<Signature>
{
public:
    Signature(const QString& name);
    Signature(const Signature& other) = default;
    Signature(Signature&& other) = default;

    virtual ~Signature();

    Signature& operator=(const Signature& other) = default;
    Signature& operator=(Signature&& other) = default;

public:
    /// Create a new signature for a function named \p name
    static std::shared_ptr<Signature> instantiate(Platform plat, CallConv cc, const QString& name);

    /// Check if parameters, returns and name match
    virtual bool operator==(const Signature& other) const;

    /// clone this signature
    virtual std::shared_ptr<Signature> clone() const;

    bool isUnknown() const { return m_unknown; }

    /// \returns true if the signature cannot be changed by analysis code.
    bool isForced()  const { return m_forced; }

    void setUnknown(bool b) { m_unknown = b; }

    /// If \p forced is true, don't change the signature by analysis code.
    void setForced(bool forced) { m_forced = forced; }

    /// Add a return to this signature.
    /// \param exp The value of the expression thar is returned.
    virtual void addReturn(SharedType type, SharedExp exp = nullptr);

    /// \deprecated Deprecated. Use the above version.
    virtual void addReturn(SharedExp e);

    virtual void addReturn(std::shared_ptr<Return> ret) { m_returns.emplace_back(ret); }

    virtual SharedExp getReturnExp(size_t n) const { return m_returns[n]->getExp(); }
    virtual SharedType getReturnType(size_t n) const { return m_returns[n]->getType(); }
    virtual size_t getNumReturns() const { return m_returns.size(); }

    int findReturn(SharedExp e) const;

    void setRetType(SharedType t) { m_rettype = t; }

    SharedType getTypeFor(SharedExp e) const;

    // get/set the name
    virtual QString getName() const;
    virtual void setName(const QString& name);

    // get/set the signature file
    const QString& getSigFilePath() const { return m_sigFile; }
    void setSigFile(const QString& name) { m_sigFile = name; }

    // add a new parameter to this signature
    virtual void addParameter(const char *name = nullptr);
    virtual void addParameter(SharedType type, const QString& name = QString::null, const SharedExp& e = nullptr,
                              const QString& boundMax = "");
    virtual void addParameter(const SharedExp& e, SharedType ty);
    virtual void addParameter(std::shared_ptr<Parameter> param);

    virtual void removeParameter(const SharedExp& e);
    virtual void removeParameter(size_t i);

    // set the number of parameters using defaults
    virtual void setNumParams(size_t n);

    // accessors for parameters
    virtual size_t getNumParams() const { return m_params.size(); }

    const std::vector<std::shared_ptr<Parameter>>& getParameters() const { return m_params; }

    virtual const QString& getParamName(size_t n) const;
    virtual SharedExp getParamExp(int n) const;
    virtual SharedType getParamType(int n) const;
    virtual QString getParamBoundMax(int n) const;

    virtual void setParamType(int n, SharedType ty);
    virtual void setParamType(const char *name, SharedType ty);
    virtual void setParamType(const SharedExp& e, SharedType ty);
    virtual void setParamName(int n, const char *name);
    virtual void setParamExp(int n, SharedExp e);

    // Return the index for the given expression, or -1 if not found
    virtual int findParam(const SharedExp& e) const;
    virtual int findParam(const QString& name) const;

    // accessor for argument expressions
    virtual SharedExp getArgumentExp(int n) const;

    void setHasEllipsis(bool yesno)  { m_ellipsis = yesno; }
    virtual bool hasEllipsis() const { return m_ellipsis; }

    void renameParam(const QString& oldName, const char *newName);

    /// any signature can be promoted to a higher level signature, if available
    virtual std::shared_ptr<Signature> promote(UserProc *p);

    void print(QTextStream& out, bool = false) const;
    char *prints() const; // For debugging
    void printToLog() const;

    // Special for Mike: find the location that conventionally holds the first outgoing (actual) parameter
    // MVE: Use the below now
    // Special for Mike: find the location where the first outgoing (actual) parameter is conventionally held
    SharedExp getFirstArgLoc(Prog *prog) const;

    /// Get the expected argument location, based solely on the machine of the input program
    ///
    /// This is like getParamLoc, except that it works before Signature::analyse is called.  It is used only to order
    /// parameters correctly, for the common case where the proc will end up using a standard calling convention
    SharedExp getEarlyParamExp(int n, Prog *prog) const;

    // Get a wildcard to find stack locations
    virtual SharedExp getStackWildcard() const { return nullptr; }
    class StackRegisterNotDefinedException : public std::exception
    {
    public:
        StackRegisterNotDefinedException() {}
    };

    /// Needed before the signature is promoted
    virtual int getStackRegister() const noexcept (false);
    static int getStackRegister(Prog *prog) noexcept (false);

    /**
     * Does expression e represent a local stack-based variable?
     * Result can be ABI specific, e.g. sparc has locals in the parent's stack frame, at POSITIVE offsets from the
     * stack pointer register
     * Also, I believe that the PA/RISC stack grows away from 0
     */
    bool isStackLocal(Prog *prog, SharedExp e) const;

    // Similar to the above, but checks for address of a local (i.e. sp{0} -/+ K)
    virtual bool isAddrOfStackLocal(Prog *prog, const SharedExp& e) const;

    // For most machines, local variables are always NEGATIVE offsets from sp
    virtual bool isLocalOffsetNegative() const { return true; }

    // For most machines, local variables are not POSITIVE offsets from sp
    virtual bool isLocalOffsetPositive() const { return false; }

    // Is this operator (between the stack pointer and a constant) compatible with a stack local pattern?
    bool isOpCompatStackLocal(OPER op) const;

    /// \todo remove quick and dirty hack
    /// A bit of a cludge. Problem is that we can't call the polymorphic getReturnExp() until signature promotion has
    /// happened. For the switch logic, that happens way too late. So for now, we have this cludge.
    /// This is very very hacky! (trent)
    static SharedExp getReturnExp2(BinaryFile *binaryFile);
    static StatementList& getStdRetStmt(Prog *prog);

    // get anything that can be proven as a result of the signature
    virtual SharedExp getProven(SharedExp /*left*/) const { return nullptr; }
    virtual bool isPreserved(SharedExp /*e*/) const { return false; }     // Return whether e is preserved by this proc

    virtual void getLibraryDefines(StatementList& /*defs*/) {} // Set the locations defined by library calls

    /// Not very satisfying to do things this way. Problem is that the polymorphic CallingConvention objects are set up
    /// very late in the decompilation. Get the set of registers that are not saved in library functions (or any
    /// procedures that follow the calling convention)
    static void getABIDefines(Prog *prog, StatementList& defs);

    // Return true if this is a known machine (e.g. SparcSignature as opposed to Signature)
    virtual bool isPromoted() const { return false; }

    // ascii versions of platform, calling convention name
    static QString getPlatformName(Platform plat);
    static QString getConventionName(CallConv cc);

    virtual Platform getPlatform()   const { return Platform::GENERIC; }
    virtual CallConv getConvention() const { return CallConv::INVALID; }

    // prefered format
    void setPreferredReturn(SharedType ty) { m_preferredReturn = ty; }
    void setPreferredName(const QString& name) { m_preferredName = name; }
    void addPreferredParameter(int n) { m_preferredParams.push_back(n); }
    SharedType getPreferredReturn()   { return m_preferredReturn; }
    const QString& getPreferredName() { return m_preferredName; }
    size_t getNumPreferredParams()    { return m_preferredParams.size(); }
    int getPreferredParam(size_t n)   { return m_preferredParams[n]; }

    // A compare function for arguments and returns. Used for sorting returns in calcReturn() etc

    /// \returns \p a < \p b
    virtual bool argumentCompare(const Assignment& a, const Assignment& b) const;

    /// \returns \p a < \p b
    virtual bool returnCompare(const Assignment& a, const Assignment& b) const;

    bool isNoReturn() const { return false; }

protected:
    QString m_name;                                    ///< name of procedure
    QString m_sigFile;                                 ///< signature file this signature was read from (for libprocs)

    std::vector<std::shared_ptr<Parameter> > m_params; ///< \todo unique_ptr ?
    std::vector<std::shared_ptr<Return> > m_returns;
    SharedType m_rettype;
    bool m_ellipsis;
    bool m_unknown;
    bool m_forced;
    SharedType m_preferredReturn;
    QString m_preferredName;
    std::vector<int> m_preferredParams;
};


class CustomSignature : public Signature
{
public:
    CustomSignature(const QString& name);
    virtual ~CustomSignature() override = default;

public:
    virtual bool isPromoted() const override { return true; }
    virtual std::shared_ptr<Signature> clone() const override;

    void setSP(int spReg);

    virtual int getStackRegister() const noexcept (false)override { return m_spReg; }

protected:
    int m_spReg;
};
