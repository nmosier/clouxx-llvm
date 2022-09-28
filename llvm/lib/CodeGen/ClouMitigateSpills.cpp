#include "llvm/CodeGen/ClouMitigateSpills.h"

#include <cassert>

#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/InitializePasses.h"
#include "llvm/CodeGen/MachineFrameInfo.h"

using namespace llvm;

namespace llvm {

  cl::opt<bool> ClouMitigateSpillsFlag("clou-mitigate-spills-flag", // TODO: Fix this
				   cl::desc("Clou's Mitigate-Spills Pass"),
				   cl::init(false)
				   );  

  class ClouMitigateSpills final : public MachineFunctionPass {
  public:
    static char ID;

    ClouMitigateSpills();

    bool runOnMachineFunction(MachineFunction& MF) override {
#if 0
      errs() << "Running " << getPassName() << " on " << MF.getName() << "\n";
      MF.dump();
      const auto& MFI = MF.getFrameInfo();
      MFI.dump(MF);
      
      for (int i = MFI.getObjectIndexBegin(); i < MFI.getObjectIndexEnd(); ++i) {
	if (MFI.isSpillSlotObjectIndex(i)) {
	  errs() << "isSpillSlotObjectIndex: " << i << "\n";
	}
	if (MFI.isStatepointSpillSlotObjectIndex(i)) {
	  errs() << "isStatepointSpillSlotObjecetIndex: " << i << "\n";
	}
      }
      
      const TargetSubtargetInfo& STI = MF.getSubtarget();
      const TargetInstrInfo *TII = STI.getInstrInfo();

      bool changed = false;

      auto& entry_B = MF.front();
      for (int i = MFI.getObjectIndexBegin(); i < MFI.getObjectIndexEnd(); ++i) {
	if (i >= 0 && MFI.isSpillSlotObjectIndex(i)) {
	  // auto *I = BuildMI(entry_B, entry_B.begin(), entry_B.begin()->getDebugLoc(), TII->get(TargetOpcode::COPY), stack_slot).addImm(0).getInstr();
	  errs() << getPassName() << ": added instruction: " << *I << "\n";
	  changed = true;
	}
      }

      return changed;
#else
      return false;
#endif
    }
  };

  char ClouMitigateSpills::ID = 0;
  char& ClouMitigateSpillsID = ClouMitigateSpills::ID;

}

INITIALIZE_PASS(ClouMitigateSpills, "clou-mitigate-spills", "Clou's Stack Spill Mitigation Pass", false, false);

namespace llvm {
  ClouMitigateSpills::ClouMitigateSpills(): MachineFunctionPass(ID) {
    initializeClouMitigateSpillsPass(*PassRegistry::getPassRegistry());
  }
}
