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
						   llvm::cl::ZeroOrMore,
						   llvm::cl::callback([] (const std::string& s) {
						     ClouLog = !s.empty();
						   }));
  }

  bool PSF = false;
  static llvm::cl::opt<bool, true> PSFFlag("clou-psf",
					  llvm::cl::desc("Clou PSF (experimental)"),
					  llvm::cl::location(PSF),
					  llvm::cl::init(false)
					  );

#define SUBCOMPONENTS_INIT_Y(name, init) name(false)
#define SUBCOMPONENTS_INIT_X(name, init) SUBCOMPONENTS_INIT_Y(name, init),
  Subcomponents::Subcomponents(): SUBCOMPONENTS_X(SUBCOMPONENTS_INIT_X, SUBCOMPONENTS_INIT_Y) {}
#undef SUBCOMPONENTS_INIT_X
#undef SUBCOMPONENTS_INIT_Y
  Subcomponents::Subcomponents(llvm::StringRef s): Subcomponents() {
    if (s == "" || s == "all") {
#define SUBCOMPONENTS_ASSIGN(name, init) name = init;
      SUBCOMPONENTS_X(SUBCOMPONENTS_ASSIGN, SUBCOMPONENTS_ASSIGN);
#undef SUBCOMPONENTS_ASSIGN
    } else if (s == "none") {
#define SUBCOMPONENTS_ASSIGN(name, init) name = false;
      SUBCOMPONENTS_X(SUBCOMPONENTS_ASSIGN, SUBCOMPONENTS_ASSIGN);
#undef SUBCOMPONENTS_ASSIGN
    } else {
      llvm::SmallVector<llvm::StringRef> tokens;
      s.split(tokens, ",");
      for (llvm::StringRef token : tokens) {
	static const std::map<std::string, bool Subcomponents::*> map = {
#define SUBCOMPONENTS_TAB(name, init) {#name, &Subcomponents::name},
	  SUBCOMPONENTS_X(SUBCOMPONENTS_TAB, SUBCOMPONENTS_TAB)
#undef SUBCOMPONENTS_TAB
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

    static Subcomponents& operator|=(Subcomponents& a, const Subcomponents& b) {
#define SUBCOMPONENTS_OR(name, init) a.name |= b.name;
      SUBCOMPONENTS_X(SUBCOMPONENTS_OR, SUBCOMPONENTS_OR)
#undef SUBCOMPONENTS_OR
	return a;
    }

    llvm::cl::opt<std::string> ClouOpt {
      "clou",
      llvm::cl::desc("Enable LLVM-SCT a.k.a. ClouCC"),
      llvm::cl::ValueOptional,
      llvm::cl::ZeroOrMore,
      llvm::cl::callback([] (const std::string& s_) {
	llvm::StringRef s(s_);
	if (s.startswith("+"))
	  enabled |= Subcomponents(s.drop_front());
	else
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

  bool InsertTrapAfterMitigations;
  static llvm::cl::opt<bool, true> InsertTrapAfterMitigationsFlag {
    "clou-mitigation-trap",
    llvm::cl::desc("Insert a trap after each mitigation; used for dynamic mitigation counting"),
    llvm::cl::location(InsertTrapAfterMitigations),
    llvm::cl::init(false),
  };

  MinCutAlgKind MinCutAlg;
  static llvm::cl::opt<MinCutAlgKind, true> MinCutAlgKindOpt {
    "clou-min-cut-alg",
    llvm::cl::desc("Which minimum directed multi-cut algorithm to use"),
    llvm::cl::values(clEnumValN(MinCutAlgKind::SMT, "smt", "Use a SMT solver"),
		     clEnumValN(MinCutAlgKind::GREEDY, "greedy", "Use a greedy algorithm")
		     ),
    llvm::cl::location(MinCutAlg),
    llvm::cl::init(MinCutAlgKind::GREEDY),
  };

  bool RestrictedPSF;
  static llvm::cl::opt<bool, true> RestrictedPSFFlag {
    "clou-restricted-psf",
    llvm::cl::desc("Restricted PSF mode: only protect against PSF in LSQ (only makes sense with '-clou-psf' flag)"),
    llvm::cl::location(RestrictedPSF),
    llvm::cl::init(true),
  };

  float LoopWeight;
  static llvm::cl::opt<float, true> LoopWeight_ {
    "clou-loop-weight",
    llvm::cl::desc("Loop weight in LFENCE Insertion Pass"),
    llvm::cl::location(LoopWeight),
    llvm::cl::init(1)
  };

  float DominatorWeight;
  static llvm::cl::opt<float, true> DominatorWeight_ {
    "clou-dom-weight",
    llvm::cl::desc("Dominator weight in LFENCE Insertion Pass"),
    llvm::cl::location(DominatorWeight),
    llvm::cl::init(1)
  };

  float STWeight;
  static llvm::cl::opt<float, true> STWeight_ {
    "clou-st-weight",
    llvm::cl::desc("ST pair weight in LFENCE Insertion Pass"),
    llvm::cl::location(STWeight),
    llvm::cl::init(1)
  };

  bool ExpandSTs;
  static llvm::cl::opt<bool, true> ExpandSTs_ {
    "clou-expand-sts",
    llvm::cl::desc("Expand STs (EXPERIMENTAL)"),
    llvm::cl::location(ExpandSTs),
    llvm::cl::init(false)
  };

  llvm::StringRef FnAttr_fps_usestack("llsct_fps_usestack");

  const uint64_t StackSize = 0x10000;


  bool StrictCallingConv;
  static llvm::cl::opt<bool, true> StrictCallingConv_ {
    "llsct-strict-cc",
    llvm::cl::desc("LLSCT's Strict Calling Convention"),
    llvm::cl::location(StrictCallingConv),
    llvm::cl::init(true)
  };


  bool NCASAll;
  static llvm::cl::opt<bool, true> NCASAll_ {
    "llsct-ncas-all",
    llvm::cl::desc("Treat all NCA stores as having secret value operands"),
    llvm::cl::location(NCASAll),
    llvm::cl::init(false)
  };

  float Timeout;
  static llvm::cl::opt<float, true> TimeoutOpt {
    "llsct-timeout",
    llvm::cl::desc("Set timeout for fence pass"),
    llvm::cl::location(Timeout),
    llvm::cl::init(0)
  };
}
