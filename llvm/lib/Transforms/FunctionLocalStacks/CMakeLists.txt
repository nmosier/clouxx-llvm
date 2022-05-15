add_compile_options(-g)

add_llvm_library(LLVMFunctionLocalStacks MODULE
  FunctionLocalStacks.cpp

  PLUGIN_TOOL
  opt
  )
