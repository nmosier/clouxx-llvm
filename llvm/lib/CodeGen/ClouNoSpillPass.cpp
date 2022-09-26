#include <cstring>
#include <iostream>
#include <vector>

#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/LiveIntervals.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/InitializePasses.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/Support/raw_os_ostream.h"
#include "llvm/CodeGen/ClouNoSpillAnalysis.h"
#include "llvm/CodeGen/MachineLoopInfo.h"


using namespace llvm;

namespace llvm {
    cl::opt<bool> ClouNoSpill("clou-nospill",
			      cl::desc("Clou's No-spill Register Pass"),
			      cl::init(false)
			      );

  cl::opt<int> ClouNoSpillMax("clou-nospill-max",
			      cl::desc("Maximum register to mark nospill per function (negative number means unlimited)"),
			      cl::init(-1)
			      );

  // TODO: Can remove this when compiling without MinSizeRel.
  raw_os_ostream placeholder (std::cerr);

    class ClouNoSpillPass final: public MachineFunctionPass {
    public:
      static char ID;

      ClouNoSpillPass();

      void getAnalysisUsage(AnalysisUsage& AU) const override {
#if 0	
	AU.addRequired<LiveIntervals>();
	AU.addPreserved<LiveIntervals>();
	AU.setPreservesCFG();
	// AU.setPreservesAll();
	MachineFunctionPass::getAnalysisUsage(AU);
#else
	AU.setPreservesCFG();
	AU.addRequired<LiveIntervals>();
	AU.addPreserved<LiveIntervals>();
	AU.addRequired<SlotIndexes>();
	AU.addPreserved<SlotIndexes>();
	AU.addPreserved<MachineLoopInfo>();
	AU.addPreservedID(MachineDominatorsID);
	MachineFunctionPass::getAnalysisUsage(AU);
#endif
      }

      bool runOnMachineFunction(MachineFunction& MF) override {
	errs() << "Running " << getPassName() << " on " << MF.getName() << "\n";

	LiveIntervals& LIS = getAnalysis<LiveIntervals>();
	SlotIndexes& SIS = getAnalysis<SlotIndexes>();

	bool changed = false;

	std::vector<std::pair<LiveInterval *, MachineInstr *>> nospills;
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
		auto& LI = LIS.getInterval(reg);
		LI.markNotSpillable();
		nospills.emplace_back(&LI, &MI);
	      }
	    }
	  }
	}

	for (const auto& [LI, MI] : nospills) {
	  auto *MBB = MI->getParent();
	  MI->eraseFromBundle();
	  LIS.shrinkToUses(LI);
	  SIS.repairIndexesInRange(MBB, MBB->begin(), MBB->end());
	}

	return !nospills.empty();
      }
    };

  char ClouNoSpillPass::ID = 0;
  char& ClouNoSpillPassID = ClouNoSpillPass::ID;

}


INITIALIZE_PASS_BEGIN(ClouNoSpillPass, "clou-nospill-pass", "Clou's No-spill Register Pass", false, false)
INITIALIZE_PASS_DEPENDENCY(LiveIntervals)
INITIALIZE_PASS_END(ClouNoSpillPass, "clou-nospill-pass", "Clou's No-spill Register Pass", false, false)

namespace llvm {
  ClouNoSpillPass::ClouNoSpillPass(): MachineFunctionPass(ID) {
    initializeClouNoSpillPassPass(*PassRegistry::getPassRegistry());
  }
}
