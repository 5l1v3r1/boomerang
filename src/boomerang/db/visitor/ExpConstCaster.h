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


#include "boomerang/db/visitor/ExpModifier.h"

using SharedType = std::shared_ptr<class Type>;


class ExpConstCaster : public ExpModifier
{
public:
    ExpConstCaster(int _num, SharedType _ty);

    virtual ~ExpConstCaster() override = default;

    bool isChanged() const { return m_changed; }

    SharedExp preVisit(const std::shared_ptr<Const>& c) override;

private:
    int m_num;
    SharedType m_ty;
    bool m_changed;
};
