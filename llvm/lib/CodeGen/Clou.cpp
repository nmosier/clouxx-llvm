#include "llvm/Clou/Clou.h"

#include <string>
#include <map>
#include <cstdlib>
#include <set>

#include "llvm/Support/CommandLine.h"
#include "llvm/Support/WithColor.h"
#include "llvm/ADT/APInt.h"

namespace clou {

  bool ClouNoSpill;
  namespace {
    llvm::cl::opt<bool, true> ClouNoSpillFlag("clou-nospill",
					      llvm::cl::desc("Clou's No-spill Register Pass"),
					      llvm::cl::location(ClouNoSpill),
					      llvm::cl::init(false)
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

    // Validity checks
    if (postch && !fps) {
      llvm::WithColor::error() << ": post-call hardening ('postch') requires function-private stacks ('fps') to be enabled\n";
      std::_Exit(EXIT_FAILURE);
    }
  }

  Subcomponents enabled;

  namespace {

    llvm::cl::opt<std::string> ClouOpt {
      "clou",
      llvm::cl::desc("Enable LLVM-SCT a.k.a. ClouCC"),
      llvm::cl::ValueOptional,
      llvm::cl::ZeroOrMore,
      llvm::cl::callback([] (const std::string& s) {
	enabled = Subcomponents(s);
      }),
    };
    
  }

  namespace {
    std::set<std::string> whitelist;
    llvm::cl::opt<std::string> ClouWhitelistOpt {
      "clou-whitelist",
      llvm::cl::desc("Whitelist function(s)"),
      llvm::cl::ZeroOrMore,
      llvm::cl::callback([] (const std::string& s) {
	llvm::SmallVector<llvm::StringRef> tokens;
	llvm::StringRef(s).split(tokens, ',');
	for (llvm::StringRef token : tokens)
	  whitelist.insert(token.str());
      })
    };
  }

  bool whitelisted(const llvm::Function& F) {
    return whitelist.find(F.getName().str()) != whitelist.end();
  }

  size_t max_inline_instructions;
  static llvm::cl::opt<size_t, true> ClouMaxInlineInsts {
    "clou-max-inline-insts",
    llvm::cl::desc("Maximum number of instructions for which we'll inline a function"),
    llvm::cl::location(max_inline_instructions),
    llvm::cl::init(100),
  };

  size_t max_inline_count;
  static llvm::cl::opt<size_t, true> ClouMaxInlineCount {
    "clou-max-inline-count",
    llvm::cl::desc("Maximum number of times we'll inline a function"),
    llvm::cl::location(max_inline_count),
    llvm::cl::init(2),
  };

  bool UnsafeAA;
  static llvm::cl::opt<bool, true> UnsafeAliasAnalysisFlag {
    "clou-unsafe-aa",
    llvm::cl::desc("Use an optimistic, unsafe alias analysis (only use for testing!)"),
    llvm::cl::location(UnsafeAA),
    llvm::cl::init(false),
  };

  bool WeightGraph;
  static llvm::cl::opt<bool, true> WeightGraphFlag {
    "clou-weight",
    llvm::cl::desc("Weight the s-t graph"),
    llvm::cl::location(WeightGraph),
    llvm::cl::init(true),
  };
    
}
