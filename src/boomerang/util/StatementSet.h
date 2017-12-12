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

#include <set>
#include <memory>


class Statement;
class QTextStream;

using SharedExp = std::shared_ptr<class Exp>;


/**
 * A class to implement sets of statements
 */
class InstructionSet : public std::set<Statement *>
{
public:
    void makeUnion(const InstructionSet& other);       ///< Set union
    void makeDiff(const InstructionSet& other);        ///< Set difference

    /// Make this set the intersection of itself and other
    void makeIsect(const InstructionSet& other);       ///< Set intersection

    /// Check for the subset relation, i.e. are all my elements also in the set
    /// other. Effectively (this intersect other) == this
    bool isSubSetOf(const InstructionSet& other);      ///< Subset relation

    /// Remove this Statement. Return false if it was not found
    bool remove(Statement *s);                   ///< Removal; rets false if not found

    /// Search for s in this Statement set. Return true if found
    bool exists(Statement *s);                 ///< Search; returns false if !found

    /// Find a definition for loc in this Statement set. Return true if found
    bool definesLoc(SharedExp loc);              ///< Search; returns true if any

    /// statement defines loc
    bool operator<(const InstructionSet& o) const; ///< Compare if less
    void print(QTextStream& os) const;             ///< Print to os

    /// Print just the numbers to stream os
    void printNums(QTextStream& os);               ///< Print statements as numbers

    /// Print to a string, for debugging
    const char *prints();                          ///< Print to string (for debug)
    void dump();                                   ///< Print to standard error for debugging
};
