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


#include "boomerang/db/signature/Signature.h"


namespace CallingConvention
{
namespace StdC
{

class BOOMERANG_API SPARCSignature : public Signature
{
public:
    explicit SPARCSignature(const QString& name);
    explicit SPARCSignature(Signature& old);
    virtual ~SPARCSignature() override = default;

public:
    virtual std::shared_ptr<Signature> clone() const override;
    virtual bool operator==(const Signature& other) const override;
    static bool qualified(UserProc *p, Signature&);

    virtual void addReturn(SharedType type, SharedExp e = nullptr) override;
    virtual void addParameter(const QString& name, const SharedExp& e,
                              SharedType type = VoidType::get(), const QString& boundMax = "") override;
    virtual SharedExp getArgumentExp(int n) const override;

    virtual std::shared_ptr<Signature> promote(UserProc *) override;

    virtual int getStackRegister() const override { return 14; }
    virtual SharedExp getProven(SharedExp left) const override;
    virtual bool isPreserved(SharedExp e) const override;         // Return whether e is preserved by this proc

    /// Return a list of locations defined by library calls
    virtual void getLibraryDefines(StatementList& defs) override;

    /// Stack offsets can be negative (inherited) or positive:
    virtual bool isLocalOffsetPositive() const override { return true; }

    /// An override for testing locals
    /// An override for the SPARC: [sp+0] .. [sp+88] are local variables (effectively), but [sp + >=92] are memory parameters
    virtual bool isAddrOfStackLocal(int spIndex, const SharedConstExp& e) const override;

    virtual bool isPromoted() const override { return true; }
    virtual CallConv getConvention() const override { return CallConv::C; }
    virtual bool returnCompare(const Assignment& a, const Assignment& b) const override;
    virtual bool argumentCompare(const Assignment& a, const Assignment& b) const override;
};


class SPARCLibSignature : public SPARCSignature
{
public:
    explicit SPARCLibSignature(const QString& name)
        : SPARCSignature(name) {}

    virtual std::shared_ptr<Signature> clone() const override;
    virtual SharedExp getProven(SharedExp left) const override;
};

}
}
