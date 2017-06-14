#pragma once

#include "db/statements/typingstatement.h"

// An implicit reference has only an expression. It holds the type information that results from taking the address
// of a location. Note that dataflow can't decide which local variable (in the decompiled output) is being taken,
// if there is more than one local variable sharing the same memory address (separated then by type).
class ImpRefStatement : public TypingStatement
{
	SharedExp m_addressExp; // The expression representing the address of the location referenced

public:
	// Constructor, subexpression
	ImpRefStatement(SharedType ty, SharedExp a);

	SharedExp getAddressExp() const { return m_addressExp; }
	SharedType getType() const { return m_type; }
	void meetWith(SharedType ty, bool& ch); // Meet the internal type with ty. Set ch if a change

	// Virtuals
	virtual Instruction *clone() const override;
	virtual bool accept(StmtVisitor *) override;
	virtual bool accept(StmtExpVisitor *) override;
	virtual bool accept(StmtModifier *) override;
	virtual bool accept(StmtPartModifier *) override;

	virtual bool isDefinition() const override { return false; }
	virtual bool usesExp(const Exp&) const override { return false; }
	virtual bool search(const Exp&, SharedExp&) const override;
	virtual bool searchAll(const Exp&, std::list<SharedExp, std::allocator<SharedExp> >&) const override;

	virtual bool searchAndReplace(const Exp&, SharedExp, bool cc = false) override;
	virtual void generateCode(HLLCode *, BasicBlock *, int)  override {}
	virtual void simplify() override;

	// NOTE: ImpRefStatement not yet used
	virtual void print(QTextStream& os, bool html = false) const override;
};
