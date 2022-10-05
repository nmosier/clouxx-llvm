#include "llvm/Clou/Clou.h"

#include "llvm/Support/CommandLine.h"
#include "llvm/Support/WithColor.h"

namespace clou {

  StackMitigationMode stack_mitigation_mode;
  namespace {
    llvm::cl::opt<StackMitigationMode, true> stack_mitigation_mode_opt {
      "clou-stack-mitigation",
      llvm::cl::location(stack_mitigation_mode),
      llvm::cl::desc("Stack mitigation mode"),
      llvm::cl::values(clEnumValN(StackMitigationMode::Lfence, "lfence", "Mitigate stacks with lfence"),
		       clEnumValN(StackMitigationMode::FunctionPrivateStacks, "function-private-stacks", "Mitigate stacks with function-private stacks"),
		       clEnumValN(StackMitigationMode::FunctionPrivateStacks, "fps", "Mitigation stacks with function-private stacks")
		       ),
      llvm::cl::init(StackMitigationMode::Lfence),
      
    };
  }

  bool ClouNoSpill;
  namespace {
    llvm::cl::opt<bool, true> ClouNoSpillFlag("clou-nospill",
					      llvm::cl::desc("Clou's No-spill Register Pass"),
					      llvm::cl::location(ClouNoSpill),
					      llvm::cl::init(false)
					      );
  }

  bool ClouSpectreRSB;
  namespace {
    llvm::cl::opt<bool, true> ClouSpectreRSBFlag("clou-rsb",
						 llvm::cl::desc("Protect against Spectre RSB"),
						 llvm::cl::location(ClouSpectreRSB),
						 llvm::cl::init(false),
						 llvm::cl::callback([] (const bool& value) {
						   if (value && stack_mitigation_mode != StackMitigationMode::FunctionPrivateStacks) {
						     llvm::WithColor::error() << "--clou-rsb flag passed without first enabling function-private stacks "
									      << "with --clou-stack-mitigation=fps\n";
						     std::exit(EXIT_FAILURE);
						   }
						 })
						 );
  }
  
}
