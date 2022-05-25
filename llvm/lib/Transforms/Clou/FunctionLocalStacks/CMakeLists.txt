add_llvm_library(CLOUFunctionLocalStacks MODULE
  FunctionLocalStacks.cpp

  PLUGIN_TOOL
  opt
  )
