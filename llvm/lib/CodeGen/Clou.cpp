#include "llvm/Clou/Clou.h"

#include <string>
#include <map>
#include <cstdlib>

#include "llvm/Support/CommandLine.h"
#include "llvm/Support/WithColor.h"
#include "llvm/ADT/APInt.h"

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

  bool ClouLog = false;
  std::string ClouLogDir;
  namespace {
    llvm::cl::opt<std::string, true> ClouLogDirOpt("clou-log",
						    llvm::cl::desc("Clou's Logging Directory"),
						    llvm::cl::location(ClouLogDir),
						    llvm::cl::callback([] (const std::string& s) {
						      ClouLog = !s.empty();
						    }));
  }


  Subcomponents::Subcomponents(): udt(false), oobs(false), fps(false), prech(false), postch(false) {}
  Subcomponents::Subcomponents(llvm::StringRef s): Subcomponents() {
    if (s == "" || s == "all") {
      udt = oobs = fps = prech = postch = true;
    } else if (s == "none") {
      // ignore
    } else {
      llvm::SmallVector<llvm::StringRef> tokens;
      s.split(tokens, ",");
      for (llvm::StringRef token : tokens) {
	static const std::map<std::string, bool Subcomponents::*> map = {
	  {"udt", &Subcomponents::udt},
	  {"oobs", &Subcomponents::oobs},
	  {"fps", &Subcomponents::fps},
	  {"prech", &Subcomponents::prech},
	  {"postch", &Subcomponents::postch},
	};
	const auto it = map.find(token.str());
	if (it == map.end()) {
	  llvm::WithColor::error() << "invalid subcomponent: " << token << "\n";
	  std::_Exit(EXIT_FAILURE);
	} else {
	  this->*(it->second) = true;
	}
      }
    }
  }

  Subcomponents enabled;

  namespace {

    llvm::cl::opt<std::string> ClouOpt {
      "clou",
      llvm::cl::desc("Enable LLVM-SCT a.k.a. ClouCC"),
      llvm::cl::callback([] (const std::string& s) {
	enabled = Subcomponents(s);
      }),
    };
    
  }
  
}
