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


#include <memory>
#include <set>

class QTextStream;
class Assign;

using SharedExp = std::shared_ptr<class Exp>;


/**
 * Like \ref StatementSet, but the Statements are known to be Assigns,
 * and are sorted sensibly
 */
class AssignSet
{
    struct lessAssign
    {
        bool operator() (const Assign *x, const Assign *y) const;
    };

    typedef std::set<Assign *, lessAssign> Set;

public:
    typedef Set::iterator iterator;
    typedef Set::const_iterator const_iterator;
    typedef Set::reverse_iterator reverse_iterator;
    typedef Set::const_reverse_iterator const_reverse_iterator;

public:
    iterator begin() { return m_set.begin(); }
    iterator end()   { return m_set.end();   }

    const_iterator begin() const { return m_set.begin(); }
    const_iterator end()   const { return m_set.end();   }

    reverse_iterator rbegin() { return m_set.rbegin(); }
    reverse_iterator rend()   { return m_set.rend();   }

    const_reverse_iterator rbegin() const { return m_set.rbegin(); }
    const_reverse_iterator rend()   const { return m_set.rend();   }

public:
    void clear();

    bool empty() const;

    size_t size() const;

    void insert(Assign *assign);

    /// \returns true if removed, false if it was not found.
    bool remove(Assign *asgn);

    bool contains(Assign *asgn) const;

    /// Set union.
    /// *this = *this union other
    void makeUnion(const AssignSet& other);

    /// Set difference.
    /// *this = *this - other
    void makeDiff(const AssignSet& other);

    /// Set intersection.
    /// *this = *this isect other
    void makeIsect(const AssignSet& other);

    /// \returns true if all elements of this set are in \p other
    bool isSubSetOf(const AssignSet& other);

    /// \returns true if any assignment in this set defines \p loc
    bool definesLoc(SharedExp loc) const;

    /// Find a definition for \p loc on the LHS in this Assign set.
    /// If found, return pointer to the Assign with that LHS (else return nullptr)
    Assign *lookupLoc(SharedExp loc);

private:
    Set m_set;
};
