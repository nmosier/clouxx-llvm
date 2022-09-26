#include "llvm/CodeGen/ClouNoSpillAnalysis.h"

#include <vector>

#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/InitializePasses.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/LiveIntervals.h"

using namespace llvm;

namespace llvm {

  char ClouNoSpillAnalysis::ID = 0;
  char& ClouNoSpillAnalysisID = ClouNoSpillAnalysis::ID;

  void ClouNoSpillAnalysis::getAnalysisUsage(AnalysisUsage& AU) const {
    AU.setPreservesCFG();
    AU.addPreserved<LiveIntervals>();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  bool ClouNoSpillAnalysis::runOnMachineFunction(MachineFunction& MF) {
    return false;
    errs() << "Running " << getPassName() << " on " << MF.getName() << "\n";
    
    nospills.clear();

    bool changed = false;

    std::vector<MachineInstr *> dels;
    for (auto& MB : MF) {
      for (auto& MI : MB) {
	if (MI.isInlineAsm()) {
	  auto& asmstr = MI.getOperand(0);
	  if (std::strcmp(asmstr.getSymbolName(), "clou.nospill") == 0) {
	    assert(MI.getNumOperands() > 0);
	    assert(asmstr.isSymbol());
	    const auto op = MI.getOperand(3);
	    assert(op.isReg());
	    const Register reg = op.getReg();
	    assert(reg.isVirtual());
	    nospills.push_back(reg);
	    dels.push_back(&MI);
	  }
	}
      }
    }

    for (auto *MI : dels) {
      MI->eraseFromBundle();
      changed = true;
    }

    return changed;
  }
  
}

INITIALIZE_PASS(ClouNoSpillAnalysis, "clou-nospill-analysis", "Clou's No-Spill Register Analysis", false, false);

namespace llvm {
  ClouNoSpillAnalysis::ClouNoSpillAnalysis(): MachineFunctionPass(ID) {
    initializeClouNoSpillAnalysisPass(*PassRegistry::getPassRegistry());
  }
}
