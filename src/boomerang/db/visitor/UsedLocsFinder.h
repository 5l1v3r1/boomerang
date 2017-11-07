#pragma region License
/*
 * This file is part of the Boomerang Decompiler.
 *
 * See the file "LICENSE.TERMS" for information on usage and
 * redistribution of this file, and for a DISCLAIMER OF ALL
 * WARRANTIES.
 */
#pragma endregion License


#include "boomerang/db/visitor/ExpVisitor.h"

class LocationSet;

/**
 *
 */
class UsedLocsFinder : public ExpVisitor
{
public:
    UsedLocsFinder(LocationSet& used, bool memOnly);
    ~UsedLocsFinder() override = default;

    LocationSet *getLocSet() { return m_used; }

    bool isMemOnly() { return m_memOnly; }
    void setMemOnly(bool b) { m_memOnly = b; }

    /// \copydoc ExpVisitor::visit
    bool visit(const std::shared_ptr<RefExp>& exp, bool& dontVisitChildren) override;

    /// \copydoc ExpVisitor::visit
    bool visit(const std::shared_ptr<Location>& exp, bool& dontVisitChildren) override;

    /// \copydoc ExpVisitor::visit
    bool visit(const std::shared_ptr<Terminal>& exp) override;

private:
    LocationSet *m_used; ///< Set of Exps
    bool m_memOnly;      ///< If true, only look inside m[...]
};
