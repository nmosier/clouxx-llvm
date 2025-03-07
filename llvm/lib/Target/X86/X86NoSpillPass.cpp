#include "X86.h"
#include "X86InstrInfo.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/LiveIntervals.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/IR/Instructions.h"
#include "llvm/CodeGen/MachineFrameInfo.h"

#include <stack>

using namespace llvm;

namespace llvm {
    cl::opt<bool> enable_no_spill_pass("clou-no-spill",
				       cl::desc("Clou's No-spill Register Pass"),
				       cl::init(false)
				       );

  namespace {


    class X86NoSpillPass final: public MachineFunctionPass {
    public:
      inline static char ID = 0;
      X86NoSpillPass(): MachineFunctionPass(ID) {
	initializeX86NoSpillPassPass(*PassRegistry::getPassRegistry());
      }

      void getAnalysisUsage(AnalysisUsage& AU) const override {
	AU.addRequired<LiveIntervals>();
	AU.setPreservesAll();
	MachineFunctionPass::getAnalysisUsage(AU);
      }

      enum AccessKind {
	AK_CA,
	AK_NCA,
	AK_NO,
      };

      AccessKind classifyAccess(const MachineInstr& MI) {
	if (!(MI.mayLoad() || MI.mayStore()))
	  return AK_NO;

	// Check if we marked it as CA at the IR level.
	for (const MachineMemOperand *MMO : MI.memoperands()) {
	  if (const Value *PtrV = MMO->getValue()) {
	    for (const User *PtrUser : PtrV->users()) {
	      if (const auto *PtrStore = dyn_cast<StoreInst>(PtrUser)) {
		if (PtrStore->getPointerOperand() == PtrV) {
		  if (PtrStore->hasMetadata("llsct.ca")) {
		    return AK_CA;
		  }
		}
	      }
	    }
	  }
	}
	
	// Check if we have a memory operand.
	const MCInstrDesc& Desc = MI.getDesc();
	int MemRefBeginIdx = X86II::getMemoryOperandNo(Desc.TSFlags);
	if (MemRefBeginIdx < 0) {
	  WithColor::warning() << " instruction may access memory but has no X86 memory operand: " << MI << "\n";
	  return AK_NO;
	}
	MemRefBeginIdx += X86II::getOperandBias(Desc);
	const MachineOperand& BaseReg = MI.getOperand(MemRefBeginIdx + X86::AddrBaseReg);
	const MachineOperand& IndexReg = MI.getOperand(MemRefBeginIdx + X86::AddrIndexReg);
	[[maybe_unused]] const MachineOperand& Disp = MI.getOperand(MemRefBeginIdx + X86::AddrDisp);
	[[maybe_unused]] const MachineOperand& SegmentReg = MI.getOperand(MemRefBeginIdx + X86::AddrSegmentReg);


	// NCA if we have an index register.
	assert(IndexReg.isReg());
	if (IndexReg.getReg() != X86::NoRegister)
	  return AK_NCA;
	
	// Check if global.
	if (BaseReg.isReg() && BaseReg.getReg() == X86::RIP)
	  return AK_CA;

	// Check if stack.
	assert(!(BaseReg.isReg() && BaseReg.getReg() == X86::RSP));
	const auto& MFI = MI.getParent()->getParent()->getFrameInfo();
	if (BaseReg.isFI())
	  return MFI.isFixedObjectIndex(BaseReg.getIndex()) ? AK_CA : AK_NCA;

	// Otherwise, assume NCA.
	if (BaseReg.isReg())
	  return AK_NCA;

	WithColor::error() << ":" __FILE__ << ":" << __LINE__ << ": unhandled machine instruction: " << MI << "\n";
	llvm_unreachable("unhandled machine instruction");
      }

      bool runOnMachineFunction(MachineFunction& MF) override {
	if (!enable_no_spill_pass) {
	  return false;
	}

	errs() << "Running " << getPassName() << " on " << MF.getName() << "\n";

	// Compute the set of register defs reached by nca store.
	std::stack<MachineInstr *> todo;
	for (auto& MBB : MF)
	  for (auto& MI : MBB)
	    if (MI.mayStore() && classifyAccess(MI) == AK_NCA)
	      todo.push(&MI);
	std::set<MachineInstr *> seen;
	std::set<MachineInstr *> tainted_defs;
	while (!todo.empty()) {
	  auto *MI = todo.top();
	  todo.pop();
	  if (!seen.insert(MI).second)
	    continue;
	  if (MI->getOpcode() == X86::LFENCE)
	    continue;
	  assert(!MI->isCall() && "NCA stores should never reach a call without encountering an LFENCE!");

	  // Add as tainted.
	  tainted_defs.insert(MI);

	  // Successors.
	  if (auto *Succ = MI->getNextNode()) {
	    todo.push(Succ);
	  } else {
	    for (auto *Succ : MI->getParent()->successors())
	      todo.push(&Succ->front());
	  }
	}

	// Now mark the tainted ca loads as nospill.
	LiveIntervals& LIS = getAnalysis<LiveIntervals>();
	for (auto *MI : tainted_defs) {
	  for (MachineOperand& Def : MI->defs()) {
	    const Register Reg = Def.getReg();
	    if (Reg.isVirtual()) {
	      LiveInterval& LI = LIS.getInterval(Reg);
	      LI.markNotSpillable();
	      assert(!LI.isSpillable() && "This shouldn't still be spillable!");
	    }
	  }
	}

#if 0
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
#endif

	return false;
      }
    };

  }
  
}
    
INITIALIZE_PASS(X86NoSpillPass, "clou-nospill", "Clou's No-spill Register Pass", false, false);

namespace llvm {
  FunctionPass *createX86NoSpillPassPass() { return new X86NoSpillPass(); }
}
