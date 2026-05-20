set(LLVM_LINK_COMPONENTS
  AggressiveInstCombine
  Analysis
  BitReader
  BitWriter
  CodeGenTypes
  Core
  FrontendDriver
  IRPrinter
  IRReader
  InstCombine
  LTO
  Linker
  MC
  Object
  Passes
  ScalarOpts
  Support
  Target
  TargetParser
  TransformUtils
  )

# Workaround for MSVC ARM64 performance regression
if(MSVC AND NOT CMAKE_CXX_COMPILER_ID MATCHES Clang
    AND MSVC_VERSION VERSION_GREATER_EQUAL 1932
    AND CMAKE_SYSTEM_PROCESSOR MATCHES "ARM64")
  string(TOUPPER "${CMAKE_BUILD_TYPE}" uppercase_CMAKE_BUILD_TYPE)
  string(REGEX MATCHALL "/[Oo][12]" opt_flags "${CMAKE_CXX_FLAGS} ${CMAKE_CXX_FLAGS_${uppercase_CMAKE_BUILD_TYPE}}")
  if (opt_flags)
    if(opt_flags MATCHES "1$")
      set(opt_flags "/Od;/Os;/Oy;/Ob2;/GF;/Gy")
    elseif (opt_flags MATCHES "2$")
      set(opt_flags "/Od;/Oi;/Ot;/Oy;/Ob2;/GF;/Gy")
    endif()
    set_source_files_properties(Builtin/BuiltinEmitter.cpp PROPERTIES COMPILE_OPTIONS "${opt_flags}")
  endif()
endif()

set(NEVERC_EMIT_TARGET_SOURCES
  Targets/AArch64.cpp
  Targets/X86.cpp
  )

add_neverc_library(nevercEmit
  Core/ModuleEmitter.cpp
  Core/FunctionEmitter.cpp
  Core/EmitterAction.cpp
  Core/TypeEmitter.cpp
  Core/ModuleBuilder.cpp
  Core/TBAAEmitter.cpp

  ABI/ABIInfo.cpp
  ABI/ABIInfoImpl.cpp
  ABI/CABISupport.cpp
  ABI/TargetInfo.cpp
  ${NEVERC_EMIT_TARGET_SOURCES}

  Expr/ExprEmitter.cpp
  Expr/AggEmitter.cpp
  Expr/ComplexEmitter.cpp
  Expr/ConstantEmitter.cpp
  Expr/ScalarEmitter.cpp

  Decl/DeclEmitter.cpp
  Decl/InitEmitter.cpp
  Decl/RecordLayoutEmitter.cpp
  Decl/ConstantInitBuilder.cpp
  Decl/PatternInit.cpp

  Stmt/StmtEmitter.cpp
  Stmt/CallEmitter.cpp
  Stmt/CleanupEmitter.cpp
  Stmt/ExceptionEmitter.cpp
  Stmt/LoopEmitter.cpp
  Stmt/VarBypassDetector.cpp

  Builtin/BuiltinEmitter.cpp
  Builtin/BuiltinEmitterAArch64.cpp
  Builtin/BuiltinEmitterX86.cpp
  Builtin/AtomicEmitter.cpp

  Debug/DebugEmitter.cpp

  Backend/BackendUtil.cpp
  Backend/ParallelCodeGenMerge.cpp
  Backend/LinkInModulesPass.cpp
  Backend/StringRuntimeLinker.cpp

  DEPENDS
  intrinsics_gen
  NeverCDriverOptions

  LINK_LIBS
  nevercTree
  nevercFoundation
  nevercCompiler
  nevercScan
  nevercMerge
  )

target_include_directories(nevercEmit
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
  )
