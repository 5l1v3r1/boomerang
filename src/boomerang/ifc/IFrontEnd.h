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

    /// \returns the name of the register with index \p regIdx
    virtual QString getRegName(int regIdx) const = 0;

    /// \returns the size (in bits) of the register with index \p regIdx
    virtual int getRegSize(int regIdx) const = 0;

    virtual bool addSymbolsFromSymbolFile(const QString& filename) = 0;

    /// lookup a library signature by name
    virtual std::shared_ptr<Signature> getLibSignature(const QString& name) = 0;

    virtual std::vector<SharedExp>& getDefaultParams() = 0;
    virtual std::vector<SharedExp>& getDefaultReturns() = 0;

    /// Add a "hint" that an instruction at the given address references a named global
    virtual void addRefHint(Address addr, const QString& name) = 0;

    /// read from default catalog
    virtual void readLibraryCatalog() = 0;

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
     * This is the main function for decoding a procedure. It is usually overridden in the derived
     * class to do source machine specific things. If \p frag is set, we are decoding just a fragment of the proc
     * (e.g. each arm of a switch statement is decoded). If \p spec is set, this is a speculative decode.
     *
     * \param addr the address at which the procedure starts
     * \param proc the procedure object
     * \param os   the output stream for .rtl output
     * \param frag if true, this is just a fragment of a procedure
     * \param spec if true, this is a speculative decode
     *
     * \note This is a sort of generic front end. For many processors, this will be overridden
     *  in the FrontEnd derived class, sometimes calling this function to do most of the work.
     *
     * \returns true for a good decode (no illegal instructions)
     */
    virtual bool processProc(Address addr, UserProc *proc, QTextStream& os, bool frag = false, bool spec = false) = 0;

public:
    /**
     * Given the dest of a call, determine if this is a machine specific helper function with special semantics.
     * If so, return true and set the semantics in lrtl.
     *
     * param addr the native address of the call instruction
     */
    virtual bool isHelperFunc(Address dest, Address addr, RTLList& rtls) = 0;

    /// Locate the entry address of "main", returning a native address
    virtual Address getMainEntryPoint(bool& gotMain) = 0;

    /// Returns a list of all available entrypoints.
    virtual std::vector<Address> getEntryPoints() = 0;

    /**
     * Create a Return or a Oneway BB if a return statement already exists.
     * \param proc      pointer to enclosing UserProc
     * \param BB_rtls   list of RTLs for the current BB (not including \p returnRTL)
     * \param returnRTL pointer to the current RTL with the semantics for the return statement
     *                  (including a ReturnStatement as the last statement)
     * \returns  Pointer to the newly created BB
     */
    virtual BasicBlock *createReturnBlock(UserProc *proc,
        std::unique_ptr<RTLList> bb_rtls, std::unique_ptr<RTL> returnRTL) = 0;

    /**
     * Add a synthetic return instruction and basic block (or a branch to the existing return instruction).
     *
     * \note the call BB should be created with one out edge (the return or branch BB)
     * \param callBB  the call BB that will be followed by the return or jump
     * \param proc    the enclosing UserProc
     * \param callRTL the current RTL with the call instruction
     */
    virtual void appendSyntheticReturn(BasicBlock *callBB, UserProc *proc, RTL *callRTL) = 0;

    /**
     * Add an RTL to the map from native address to previously-decoded-RTLs. Used to restore case statements and
     * decoded indirect call statements in a new decode following analysis of such instructions. The CFG is
     * incomplete in these cases, and needs to be restarted from scratch
     */
    virtual void saveDecodedRTL(Address a, RTL *rtl) = 0;
    virtual void preprocessProcGoto(std::list<Statement *>::iterator ss, Address dest, const std::list<Statement *>& sl, RTL *originalRTL) = 0;
    virtual void checkEntryPoint(std::vector<Address>& entrypoints, Address addr, const char *type) = 0;
};
