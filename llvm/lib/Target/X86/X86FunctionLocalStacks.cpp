#include <iostream>

#include "X86.h"
#include "X86InstrInfo.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "X86Subtarget.h"
#include "llvm/IR/Module.h"
#include "llvm/CodeGen/LiveIntervals.h"
#include "llvm/Clou/Clou.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/MachineFrameInfo.h"

#include "llvm/Support/raw_os_ostream.h"

using namespace llvm;

#define DEBUG_TYPE "x86-fps"
STATISTIC(NumMitigations, "Number of mitigations");
STATISTIC(NumInstructions, "Number of instructions inserted");

namespace {

  volatile raw_os_ostream raw_os_ostream_(std::cerr);

  class X86FunctionLocalStacks final: public MachineFunctionPass {
  public:
    static char ID;

    X86FunctionLocalStacks(): MachineFunctionPass(ID) {
      initializeX86FunctionLocalStacksPass(*PassRegistry::getPassRegistry());
    }

    static bool callUsesRegister(MachineInstr& MI, MCPhysReg reg, const TargetRegisterInfo& TRI) {
      return any_of(concat<MachineOperand>(MI.operands(), MI.implicit_operands()), [&] (const MachineOperand& MO) {
	return MO.isReg() && TRI.regsOverlap(reg, MO.getReg());
      });
    }

    static bool shouldSaveSP(MachineInstr& MI, const X86RegisterInfo& TRI) {
      assert(MI.isCall());
      MachineFunction *MF = MI.getParent()->getParent();
      if (TRI.hasBasePointer(*MF))
	return true;

      for (auto& MBB : *MF)
	for (auto& MI : MBB) 
	  if (MI.getOpcode() == X86::ADJCALLSTACKUP64 && MI.getOperand(0).getImm() > 0)
	    return true;

      return false;
    }

    static bool shouldHardenUnusedArgs(MachineInstr& Call) {
      assert(Call.isCall());

      switch (Call.getOpcode()) {
      case X86::CALLpcrel32:
	{
	  const auto& CalleeOp = Call.getOperand(0);
	  assert(CalleeOp.isGlobal());
	  auto *GV = CalleeOp.getGlobal();
	  const Function *CalledF = cast<Function>(GV);
	  if (CalledF->hasLocalLinkage())
	    return false;
	}
	break;
	
      default:
	break;
      }

      return true;
    }

    bool runOnMachineFunction(MachineFunction& MF) override {
      if (!clou::enabled.fps && !clou::enabled.prech && !clou::enabled.postch && !clou::enabled.stackinit && false) {
	return false;
      }
      
      const X86Subtarget& STI = MF.getSubtarget<X86Subtarget>();
      const TargetInstrInfo *TII = STI.getInstrInfo();
      const bool Is64Bit = STI.is64Bit();
      const unsigned AddrBytes = Is64Bit ? 8 : 4;
      const Align AddrAlign(AddrBytes);
      const Function& F = MF.getFunction();
      const GlobalValue *stack = nullptr;
      const GlobalValue *sp = nullptr;
      const bool mayRecurse = !F.doesNotRecurse();
      const Module& M = *F.getParent();
      if (clou::enabled.fps) {
	if (mayRecurse || true) {
	  sp = M.getNamedValue((F.getName() + "_sp").str());
	  assert(sp && "Stack pointer not found for function that may recurse!");
	}
	stack = M.getNamedValue((F.getName() + "_stack").str());
	assert(stack && "Stack not found for function!");
      }
      const auto& TRI = *MF.getSubtarget<X86Subtarget>().getRegisterInfo();

      for (auto& MBB : MF) {
	for (auto MBBI = MBB.begin(); MBBI != MBB.end(); ++MBBI) {
	  if (MBBI->isCall()) {
	    const auto& DL = MBBI->getDebugLoc();

	    if (clou::enabled.fps) {
	      if (shouldSaveSP(*MBBI, TRI)) {
		BuildMI(MBB, MBBI, DL, TII->get(Is64Bit ? X86::MOV64mr : X86::MOV32mr))
		  .addReg(Is64Bit ? X86::RIP : X86::NoRegister)
		  .addImm(1)
		  .addReg(0)
		  .addGlobalAddress(sp, 0)
		  .addReg(0)
		  .addReg(Is64Bit ? X86::RSP : X86::ESP)
		  .addMemOperand(MBB.getParent()->getMachineMemOperand(MachinePointerInfo(sp),
								       MachineMemOperand::MOStore,
								       AddrBytes,
								       AddrAlign));
		++NumInstructions;
	      }
	    }

	    if (clou::enabled.prech && Is64Bit && shouldHardenUnusedArgs(*MBBI)) {
	    
	      // Zero out any unused parameter-passing registers
	      for (const auto param_gpr : std::array<MCPhysReg, 6> {X86::EDI, X86::ESI, X86::EDX,
								    X86::ECX, X86::R8D, X86::R9D}) {
		if (!callUsesRegister(*MBBI, param_gpr, TRI)) {
		  // zero it out
		  BuildMI(MBB, MBBI, DL, TII->get(X86::XOR32rr), param_gpr)
		  .addReg(param_gpr)
		  .addReg(param_gpr);
		  ++NumInstructions;
		}
	      }

	      std::array<MCPhysReg, 8> param_fps = {X86::XMM0, X86::XMM1, X86::XMM2, X86::XMM3,
						    X86::XMM4, X86::XMM5, X86::XMM6, X86::XMM7};
	      if (any_of(param_fps, [&] (const MCPhysReg& reg) {
		return callUsesRegister(*MBBI, reg, TRI);
	      })) {
		for (const auto reg : param_fps) {
		  if (!callUsesRegister(*MBBI, reg, TRI)) {
		    BuildMI(MBB, MBBI, DL, TII->get(X86::XORPSrr), reg)
		      .addReg(reg)
		      .addReg(reg);
		    ++NumInstructions;
		  }
		}
	      } else {
#if 0
		BuildMI(MBB, MBBI, DL, TII->get(X86::VZEROALL));
#endif
		++NumInstructions;
	      }

	      ++NumMitigations;

	      if (clou::InsertTrapAfterMitigations)
		BuildMI(MBB, MBBI, DL, TII->get(X86::MFENCE));

	    }

	    if (clou::enabled.postch && false) {
	    
	      const auto MBBI_next = std::next(MBBI);
	      if (MBBI_next != MBB.end()) {
		// Check if SP is correct -- Spectre RSB hardening
		// xor rdi, rdi // rdi is dead, clobbered register
		// cmp rsp, [func_sp]
		// cmovne rsp, rdi
		// cmovne rbp, rdi
		// cmovne rbx, rdi
		const MCRegister ZeroReg = Is64Bit ? X86::RDI : X86::EDX;
		const MCRegister ZeroReg32 = Is64Bit ? X86::EDI : X86::EDX;
		BuildMI(MBB, MBBI_next, DL, TII->get(X86::XOR32rr), ZeroReg32)
		  .addReg(ZeroReg32)
		  .addReg(ZeroReg32);
		++NumInstructions;

		// cmp rsp, [func_sp]
		BuildMI(MBB, MBBI_next, DL, TII->get(Is64Bit ? X86::CMP64rm : X86::CMP32rm))
		  .addReg(Is64Bit ? X86::RSP : X86::ESP)
		  .addReg(Is64Bit ? X86::RIP : X86::NoRegister)
		  .addImm(1)
		  .addReg(0)
		  .addGlobalAddress(sp, 0)
		  .addReg(0)
		  .addMemOperand(MBB.getParent()->getMachineMemOperand(MachinePointerInfo(sp),
								       MachineMemOperand::MOStore,
								       AddrBytes,
								       AddrAlign));
		++NumInstructions;
		
		if (clou::enabled.fps) {
		  static const MCRegister CalleePreserved32[3] = {X86::ESP, X86::EBP, X86::EBX};
		  static const MCRegister CalleePreserved64[3] = {X86::RSP, X86::RBP, X86::RBX};
		  const MCRegister (*CalleePreserved)[3] = Is64Bit ? &CalleePreserved64 : &CalleePreserved32;
		  for (const auto reg : *CalleePreserved) {
		    BuildMI(MBB, MBBI_next, DL, TII->get(Is64Bit ? X86::CMOV64rr : X86::CMOV32rr), reg)
		      .addReg(reg)
		      .addReg(ZeroReg)
		      .addImm(X86::COND_NE);
		    ++NumInstructions;
		  }
		}

		++NumMitigations;

		if (clou::InsertTrapAfterMitigations)
		  BuildMI(MBB, MBBI_next, DL, TII->get(X86::MFENCE));
	      
	      }
	      
	    } else if (clou::enabled.postch) {
	      // Just reload the stack pointer.
	      // FIXME: Should be able to do this eventually.
	      if (F.hasFnAttribute(clou::FnAttr_fps_usestack) && false) {
		BuildMI(MBB, std::next(MBBI), DL, TII->get(X86::LEA64r), X86::RSP)
		  .addReg(X86::RIP)
		  .addImm(1)
		  .addReg(X86::NoRegister)
		  .addGlobalAddress(stack, clou::StackSize)
		  .addReg(X86::NoRegister);
	      } else {
		BuildMI(MBB, std::next(MBBI), DL, TII->get(X86::MOV64rm), X86::RSP)
		.addReg(X86::RIP)
		.addImm(1)
		.addReg(X86::NoRegister)
		.addGlobalAddress(sp, 0)
		.addReg(X86::NoRegister)
		.addMemOperand(MBB.getParent()->getMachineMemOperand(MachinePointerInfo(sp),
								     MachineMemOperand::MOLoad,
								     AddrBytes,
								     AddrAlign));
	      }
	      ++NumInstructions;
	      ++NumMitigations;
	    }
	  }
	}
      }

      if (clou::enabled.ncsrs) {

	// well, save the base pointer at least
	const MCPhysReg csrs[] = {Is64Bit ? X86::RBX : X86::EBX};
	MF.getRegInfo().setCalleeSavedRegs(csrs);
	
      }

      if (clou::enabled.stackinit)
	initializeStackSlots(MF);
      
      return true;
    }

    bool initializeStackSlots(MachineFunction& MF) {
      // const MachineFrameInfo& MFI = MF.getFrameInfo();
      const auto& TII = MF.getSubtarget().getInstrInfo();

      // Find CA stack loads that are reachable without seeing a same-address CA stack store.
      // Or just check for LEAs too?
      struct FrameAccess {
	int FrameIdx;
	int64_t Offset;
	unsigned Size;

	auto tuple() const {
	  return std::make_tuple(FrameIdx, Offset, Size);
	}
	
	bool operator<(const FrameAccess& o) const {
	  return tuple() < o.tuple();
	}

	bool operator==(const FrameAccess& o) const {
	  return tuple() == o.tuple();
	}
      };
      std::vector<FrameAccess> FIs;

      // Collect the frame indices of all CA stack loads.
      for (MachineBasicBlock& MBB : MF) {
	for (MachineInstr& MI : MBB) {
	  if (MI.mayLoad() || MI.getOpcode() == X86::LEA64r) {
	    const MCInstrDesc& Desc = MI.getDesc();
	    int MemRefBeginIdx = X86II::getMemoryOperandNo(Desc.TSFlags);
	    if (MemRefBeginIdx < 0)
	      continue;
	    MemRefBeginIdx += X86II::getOperandBias(Desc);
	    const MachineOperand& BaseMO = MI.getOperand(MemRefBeginIdx + X86::AddrBaseReg);
	    if (BaseMO.isFI()) {
	      const int FI = BaseMO.getIndex();
	      if (FI >= 0) {
		const MachineOperand& DispMO = MI.getOperand(MemRefBeginIdx + X86::AddrDisp);
		assert(DispMO.isImm() && "Expected AddrDisp to be immediate in CA stack load!");
		const int64_t Offset = DispMO.getImm();
		if (Offset != 0 && false)
		  continue;

		// get memory operand
		const auto it = llvm::find_if(MI.memoperands(), [] (const MachineMemOperand *MMO) {
		  return MMO->isLoad();
		});
		if (it == MI.memoperands_end())
		  continue;
		const auto *MMO = *it;
		
		// FIXME: Currently hard-coding size since I don't know how to find the byte size of the operand.
		FIs.push_back({.FrameIdx = FI, .Offset = Offset, .Size = static_cast<unsigned>(MMO->getSize())});
	      }
	    } else if (BaseMO.isReg()) {
	    } else {
	      llvm_unreachable("address base operand is neither frame index nor register!");
	    }
	  }
	}
      }

      // Break up accesses of > 8 bytes into <= bytes.
      for (unsigned i = 0; i < FIs.size(); ) {
	auto it = FIs.begin() + i;
	if (it->Size > 8) {
	  assert(it->Size % 2 == 0 && "Size must be divisible by 2!");
	  it->Size /= 2;
	  auto newit = FIs.insert(it, *it);
	  newit->Offset += newit->Size;
	} else {
	  ++i;
	}
      }

      llvm::sort(FIs);
      FIs.resize(std::unique(FIs.begin(), FIs.end()) - FIs.begin());
      
      for (const auto& Access : FIs) {
	// const int64_t Size = MFI.getObjectSize(Access.FrameIdx);
	static const std::map<int, int> OpcodeMap = {
	  {1, X86::MOV8mi},
	  {2, X86::MOV16mi},
	  {4, X86::MOV32mi},
	  {8, X86::MOV64mi32},
	};
	const int Opcode = OpcodeMap.at(Access.Size);
	auto& MBB = MF.front();
	BuildMI(MBB, MBB.begin(), DebugLoc(), TII->get(Opcode))
	  .addFrameIndex(Access.FrameIdx)
	  .addImm(1)
	  .addReg(X86::NoRegister)
	  .addImm(0)
	  .addReg(X86::NoRegister)
	  .addImm(0)
	  .addMemOperand(MF.getMachineMemOperand(MachinePointerInfo::getFixedStack(MF, Access.FrameIdx,
										   Access.Offset),
						 MachineMemOperand::MOStore,
						 Access.Size,
						 Align(Access.Size)));
      }

      return !FIs.empty();
    }
  };

  char X86FunctionLocalStacks::ID = 0;

}

INITIALIZE_PASS(X86FunctionLocalStacks, "x86-function-local-stacks", "X86 Function Local Stacks Pass", false, false)

namespace llvm {

  FunctionPass *createX86FunctionLocalStacksPass() { return new X86FunctionLocalStacks(); }
  
}
