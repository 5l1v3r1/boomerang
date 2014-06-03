#ifndef MACHINEINSTRUCTION_H
#define MACHINEINSTRUCTION_H

#include "types.h"
#include <QString>
#include <list>

class Instruction;
class RTLInstDict;
class Exp;
class MachineOperand {

};
struct MachineInstruction
{
    QString opcode;
    ADDRESS location;
    std::vector<Exp *> actuals;
    MachineInstruction(QString op,ADDRESS pc,std::vector<Exp *> &&acts) : opcode(op),
        location(pc),
        actuals(acts) {
    }
};

class MachineSemantics {
public:
    virtual Exp *convertOperand(MachineOperand *Operand)=0;
    virtual std::list<Instruction *> *convertInstruction(MachineInstruction *Insn)=0;
};
#endif // MACHINEINSTRUCTION_H
