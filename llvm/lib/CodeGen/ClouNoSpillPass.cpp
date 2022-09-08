#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/LiveIntervals.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/InitializePasses.h"

using namespace llvm;

namespace llvm {
    cl::opt<bool> ClouNoSpill("clou-nospill",
			      cl::desc("Clou's No-spill Register Pass"),
			      cl::init(false)
			      );

    class ClouNoSpillPass final: public MachineFunctionPass {
    public:
      static char ID;

      ClouNoSpillPass();

      void getAnalysisUsage(AnalysisUsage& AU) const override {
	AU.addRequired<LiveIntervals>();
	AU.setPreservesAll();
	MachineFunctionPass::getAnalysisUsage(AU);
      }

      bool runOnMachineFunction(MachineFunction& MF) override {
	if (!ClouNoSpill) {
	  return false;
	}

	LiveIntervals& LIS = getAnalysis<LiveIntervals>();

	for (auto& MB : MF) {
	  for (auto& MI : MB) {
	    for (MachineOperand& MOP : MI.uses()) {
	      if (MOP.isReg()) {
		Register reg = MOP.getReg();
		if (reg.isVirtual()) {
		  LiveInterval& LI = LIS.getInterval(reg);
		  LI.markNotSpillable();
		  if (LI.isSpillable()) {
		    errs() << "refused to be nospill\n";
		    abort();
		  }
		}
	      }
	    }
	  }
	}

	return false;
      }
    };

  char ClouNoSpillPass::ID = 0;
  char& ClouNoSpillID = ClouNoSpillPass::ID;

}


INITIALIZE_PASS_BEGIN(ClouNoSpillPass, "clou-nospill", "Clou's No-spill Register Pass", false, false)
INITIALIZE_PASS_DEPENDENCY(LiveIntervals)
INITIALIZE_PASS_END(ClouNoSpillPass, "clou-nospill", "Clou's No-spill Register Pass", false, false)

namespace llvm {
  ClouNoSpillPass::ClouNoSpillPass(): MachineFunctionPass(ID) {
    initializeClouNoSpillPassPass(*PassRegistry::getPassRegistry());
  }
}
