#pragma once

#include <vector>

#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/Register.h"

namespace llvm {

  class ClouNoSpillAnalysis final : public MachineFunctionPass {
  public:
    static char ID;
    std::vector<Register> nospills;

    ClouNoSpillAnalysis();

    void getAnalysisUsage(AnalysisUsage& AU) const override;
    bool runOnMachineFunction(MachineFunction& MF) override;

  };
  
}
