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


#include "boomerang/frontend/SigEnum.h"
#include "boomerang/util/Address.h"

#include <list>
#include <memory>
#include <vector>


class IDecoder;
class DecodeResult;
class QString;
class Exp;
class RTL;
class CallStatement;
class Statement;
class Signature;
class UserProc;
class BasicBlock;

using SharedExp      = std::shared_ptr<Exp>;
using SharedConstExp = std::shared_ptr<const Exp>;
using RTLList        = std::list<std::unique_ptr<RTL>>;


class IFrontEnd
{
public:
    virtual ~IFrontEnd() = default;

public:
    /// Determines whether the proc with name \p procName returns or not (like abort)
    virtual bool isNoReturnCallDest(const QString& procName) const = 0;

    virtual IDecoder *getDecoder() = 0;
    virtual const IDecoder *getDecoder() const = 0;

    /// Add a "hint" that an instruction at \p addr references a named global
    virtual void addRefHint(Address addr, const QString& name) = 0;

     // decoding related
public:
    virtual bool decodeInstruction(Address addr, DecodeResult& result) = 0;

    /// Do extra processing of call instructions.
    virtual void extraProcessCall(CallStatement *call, const RTLList& bbRTLs) = 0;

    /// Decode all undecoded procedures and return a new program containing them.
    virtual bool decodeEntryPointsRecursive(bool decodeMain = true) = 0;

    /// Decode all procs starting at a given address
    virtual bool decodeRecursive(Address addr) = 0;

    /// Decode all undecoded functions.
    virtual bool decodeUndecoded() = 0;

    /// Decode one proc starting at a given address
    /// \p addr should be the entry address of an UserProc
    virtual bool decodeOnly(Address addr) = 0;

    /// Decode a fragment of a procedure, e.g. for each destination of a switch statement
    virtual bool decodeFragment(UserProc *proc, Address addr) = 0;

    /**
     * Process a procedure, given a native (source machine) address.
     * This is the main function for decoding a procedure.
     *
     * \param proc the procedure object
     * \param addr the entry address of \p proc
     *
     * \returns true for a good decode (no illegal instructions)
     */
    virtual bool processProc(UserProc *proc, Address addr) = 0;

public:
    /**
     * Given the dest of a call, determine if this is a machine specific helper function with special semantics.
     * If so, return true and set the semantics in lrtl.
     *
     * \param addr the native address of the call instruction
     */
    virtual bool isHelperFunc(Address dest, Address addr, RTLList& rtls) = 0;

    /// Locate the entry address of "main", returning a native address
    virtual Address getMainEntryPoint(bool& gotMain) = 0;

    /// Returns a list of all available entrypoints.
    virtual std::vector<Address> findEntryPoints() = 0;

    /**
     * Add an RTL to the map from native address to previously-decoded-RTLs. Used to restore case statements and
     * decoded indirect call statements in a new decode following analysis of such instructions. The CFG is
     * incomplete in these cases, and needs to be restarted from scratch
     */
    virtual void saveDecodedRTL(Address a, RTL *rtl) = 0;
};
