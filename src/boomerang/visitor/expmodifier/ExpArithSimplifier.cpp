#pragma region License
/*
 * This file is part of the Boomerang Decompiler.
 *
 * See the file "LICENSE.TERMS" for information on usage and
 * redistribution of this file, and for a DISCLAIMER OF ALL
 * WARRANTIES.
 */
#pragma endregion License
#include "ExpArithSimplifier.h"


#include "boomerang/db/exp/Binary.h"
#include "boomerang/db/exp/Const.h"


SharedExp ExpArithSimplifier::preModify(const std::shared_ptr<Unary>& exp, bool& visitChildren)
{
    visitChildren = exp->isAddrOf();
    return exp->shared_from_this();
}


SharedExp ExpArithSimplifier::postModify(const std::shared_ptr<Binary>& exp)
{
    if (exp->getOper() != opPlus && exp->getOper() != opMinus) {
        return exp->shared_from_this();
    }

    // Partition this expression into positive non-integer terms, negative
    // non-integer terms and integer terms.
    std::list<SharedExp> positives;
    std::list<SharedExp> negatives;
    std::vector<int>     integers;
    exp->partitionTerms(positives, negatives, integers, false);

    // Now reduce these lists by cancelling pairs
    // Note: can't improve this algorithm using multisets, since can't instantiate multisets of type Exp (only Exp*).
    // The Exp* in the multisets would be sorted by address, not by value of the expression.
    // So they would be unsorted, same as lists!
    std::list<SharedExp>::iterator pp = positives.begin();
    std::list<SharedExp>::iterator nn = negatives.begin();

    while (pp != positives.end()) {
        bool inc = true;

        while (nn != negatives.end()) {
            if (**pp == **nn) {
                // A positive and a negative that are equal; therefore they cancel
                pp  = positives.erase(pp); // Erase the pointers, not the Exps
                nn  = negatives.erase(nn);
                inc = false;               // Don't increment pp now
                break;
            }

            ++nn;
        }

        if (pp == positives.end()) {
            break;
        }

        if (inc) {
            ++pp;
        }
    }

    // Summarise the set of integers to a single number.
    int sum = std::accumulate(integers.begin(), integers.end(), 0);

    // Now put all these elements back together and return the result
    if (positives.empty()) {
        if (negatives.empty()) {
            return Const::get(sum);
        }
        else {
            // No positives, some negatives. sum - Acc
            return Binary::get(opMinus, Const::get(sum), Exp::accumulate(negatives));
        }
    }

    if (negatives.empty()) {
        // Positives + sum
        if (sum == 0) {
            // Just positives
            return Exp::accumulate(positives);
        }
        else {
            OPER _op = opPlus;

            if (sum < 0) {
                _op = opMinus;
                sum = -sum;
            }

            return Binary::get(_op, Exp::accumulate(positives), Const::get(sum));
        }
    }

    // Some positives, some negatives
    if (sum == 0) {
        // positives - negatives
        return Binary::get(opMinus, Exp::accumulate(positives), Exp::accumulate(negatives));
    }

    // General case: some positives, some negatives, a sum
    OPER _op = opPlus;

    if (sum < 0) {
        _op = opMinus; // Return (pos - negs) - sum
        sum = -sum;
    }

    return Binary::get(_op, Binary::get(opMinus, Exp::accumulate(positives), Exp::accumulate(negatives)),
                       Const::get(sum));
}
