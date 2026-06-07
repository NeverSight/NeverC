
switch (getParsedKind()) {
case IgnoredAttribute:
case UnknownAttribute:
case NoSemaHandlerAttribute:
  llvm_unreachable("Ignored/unknown shouldn't get here");
case AT_AArch64SVEPcs: {
  if (Name == "aarch64_sve_pcs" && getSyntax() == AttributeCommonInfo::AS_GNU &&
      Scope == "")
    return 0;
  if (Name == "aarch64_sve_pcs" &&
      getSyntax() == AttributeCommonInfo::AS_Bracket && Scope == "clang")
    return 1;
  if (Name == "aarch64_sve_pcs" &&
      getSyntax() == AttributeCommonInfo::AS_Bracket && Scope == "neverc")
    return 2;
  if (Name == "aarch64_sve_pcs" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "clang")
    return 3;
  if (Name == "aarch64_sve_pcs" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "neverc")
    return 4;
  break;
}
case AT_AArch64VectorPcs: {
  if (Name == "aarch64_vector_pcs" &&
      getSyntax() == AttributeCommonInfo::AS_GNU && Scope == "")
    return 0;
  if (Name == "aarch64_vector_pcs" &&
      getSyntax() == AttributeCommonInfo::AS_Bracket && Scope == "clang")
    return 1;
  if (Name == "aarch64_vector_pcs" &&
      getSyntax() == AttributeCommonInfo::AS_Bracket && Scope == "neverc")
    return 2;
  if (Name == "aarch64_vector_pcs" &&
      getSyntax() == AttributeCommonInfo::AS_C23 && Scope == "clang")
    return 3;
  if (Name == "aarch64_vector_pcs" &&
      getSyntax() == AttributeCommonInfo::AS_C23 && Scope == "neverc")
    return 4;
  break;
}
case AT_AcquireHandle: {
  if (Name == "acquire_handle" && getSyntax() == AttributeCommonInfo::AS_GNU &&
      Scope == "")
    return 0;
  if (Name == "acquire_handle" &&
      getSyntax() == AttributeCommonInfo::AS_Bracket && Scope == "clang")
    return 1;
  if (Name == "acquire_handle" &&
      getSyntax() == AttributeCommonInfo::AS_Bracket && Scope == "neverc")
    return 2;
  if (Name == "acquire_handle" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "clang")
    return 3;
  if (Name == "acquire_handle" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "neverc")
    return 4;
  break;
}
case AT_AddressSpace: {
  if (Name == "address_space" && getSyntax() == AttributeCommonInfo::AS_GNU &&
      Scope == "")
    return 0;
  if (Name == "address_space" &&
      getSyntax() == AttributeCommonInfo::AS_Bracket && Scope == "clang")
    return 1;
  if (Name == "address_space" &&
      getSyntax() == AttributeCommonInfo::AS_Bracket && Scope == "neverc")
    return 2;
  if (Name == "address_space" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "clang")
    return 3;
  if (Name == "address_space" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "neverc")
    return 4;
  break;
}
case AT_Alias: {
  if (Name == "alias" && getSyntax() == AttributeCommonInfo::AS_GNU &&
      Scope == "")
    return 0;
  if (Name == "alias" && getSyntax() == AttributeCommonInfo::AS_Bracket &&
      Scope == "gnu")
    return 1;
  if (Name == "alias" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "gnu")
    return 2;
  break;
}
case AT_AlignValue: {
  if (Name == "align_value" && getSyntax() == AttributeCommonInfo::AS_GNU &&
      Scope == "")
    return 0;
  break;
}
case AT_Aligned: {
  if (Name == "aligned" && getSyntax() == AttributeCommonInfo::AS_GNU &&
      Scope == "")
    return 0;
  if (Name == "aligned" && getSyntax() == AttributeCommonInfo::AS_Bracket &&
      Scope == "gnu")
    return 1;
  if (Name == "aligned" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "gnu")
    return 2;
  if (Name == "align" && getSyntax() == AttributeCommonInfo::AS_Declspec &&
      Scope == "")
    return 3;
  if (Name == "alignas" && getSyntax() == AttributeCommonInfo::AS_Keyword &&
      Scope == "")
    return 4;
  if (Name == "_Alignas" && getSyntax() == AttributeCommonInfo::AS_Keyword &&
      Scope == "")
    return 5;
  break;
}
case AT_AllocAlign: {
  if (Name == "alloc_align" && getSyntax() == AttributeCommonInfo::AS_GNU &&
      Scope == "")
    return 0;
  if (Name == "alloc_align" && getSyntax() == AttributeCommonInfo::AS_Bracket &&
      Scope == "gnu")
    return 1;
  if (Name == "alloc_align" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "gnu")
    return 2;
  break;
}
case AT_AllocSize: {
  if (Name == "alloc_size" && getSyntax() == AttributeCommonInfo::AS_GNU &&
      Scope == "")
    return 0;
  if (Name == "alloc_size" && getSyntax() == AttributeCommonInfo::AS_Bracket &&
      Scope == "gnu")
    return 1;
  if (Name == "alloc_size" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "gnu")
    return 2;
  break;
}
case AT_AlwaysDestroy: {
  if (Name == "always_destroy" && getSyntax() == AttributeCommonInfo::AS_GNU &&
      Scope == "")
    return 0;
  if (Name == "always_destroy" &&
      getSyntax() == AttributeCommonInfo::AS_Bracket && Scope == "clang")
    return 1;
  if (Name == "always_destroy" &&
      getSyntax() == AttributeCommonInfo::AS_Bracket && Scope == "neverc")
    return 2;
  break;
}
case AT_AlwaysInline: {
  if (Name == "always_inline" && getSyntax() == AttributeCommonInfo::AS_GNU &&
      Scope == "")
    return 0;
  if (Name == "always_inline" &&
      getSyntax() == AttributeCommonInfo::AS_Bracket && Scope == "gnu")
    return 1;
  if (Name == "always_inline" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "gnu")
    return 2;
  if (Name == "always_inline" &&
      getSyntax() == AttributeCommonInfo::AS_Bracket && Scope == "neverc")
    return 3;
  if (Name == "always_inline" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "neverc")
    return 4;
  if (Name == "__forceinline" &&
      getSyntax() == AttributeCommonInfo::AS_Keyword && Scope == "")
    return 5;
  break;
}
case AT_AnalyzerNoReturn: {
  if (Name == "analyzer_noreturn" &&
      getSyntax() == AttributeCommonInfo::AS_GNU && Scope == "")
    return 0;
  break;
}
case AT_Annotate: {
  if (Name == "annotate" && getSyntax() == AttributeCommonInfo::AS_GNU &&
      Scope == "")
    return 0;
  if (Name == "annotate" && getSyntax() == AttributeCommonInfo::AS_Bracket &&
      Scope == "clang")
    return 1;
  if (Name == "annotate" && getSyntax() == AttributeCommonInfo::AS_Bracket &&
      Scope == "neverc")
    return 2;
  if (Name == "annotate" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "clang")
    return 3;
  if (Name == "annotate" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "neverc")
    return 4;
  break;
}
case AT_AnnotateType: {
  if (Name == "annotate_type" &&
      getSyntax() == AttributeCommonInfo::AS_Bracket && Scope == "neverc")
    return 0;
  if (Name == "annotate_type" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "neverc")
    return 1;
  break;
}
case AT_Interrupt: {
  if (Name == "interrupt" && getSyntax() == AttributeCommonInfo::AS_GNU &&
      Scope == "")
    return 0;
  if (Name == "interrupt" && getSyntax() == AttributeCommonInfo::AS_Bracket &&
      Scope == "gnu")
    return 1;
  if (Name == "interrupt" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "gnu")
    return 2;
  break;
}
case AT_AnyX86NoCallerSavedRegisters: {
  if (Name == "no_caller_saved_registers" &&
      getSyntax() == AttributeCommonInfo::AS_GNU && Scope == "")
    return 0;
  if (Name == "no_caller_saved_registers" &&
      getSyntax() == AttributeCommonInfo::AS_Bracket && Scope == "gnu")
    return 1;
  if (Name == "no_caller_saved_registers" &&
      getSyntax() == AttributeCommonInfo::AS_C23 && Scope == "gnu")
    return 2;
  break;
}
case AT_AnyX86NoCfCheck: {
  if (Name == "nocf_check" && getSyntax() == AttributeCommonInfo::AS_GNU &&
      Scope == "")
    return 0;
  if (Name == "nocf_check" && getSyntax() == AttributeCommonInfo::AS_Bracket &&
      Scope == "gnu")
    return 1;
  if (Name == "nocf_check" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "gnu")
    return 2;
  break;
}
case AT_ArgumentWithTypeTag: {
  if (Name == "argument_with_type_tag" &&
      getSyntax() == AttributeCommonInfo::AS_GNU && Scope == "")
    return 0;
  if (Name == "argument_with_type_tag" &&
      getSyntax() == AttributeCommonInfo::AS_Bracket && Scope == "clang")
    return 1;
  if (Name == "argument_with_type_tag" &&
      getSyntax() == AttributeCommonInfo::AS_Bracket && Scope == "neverc")
    return 2;
  if (Name == "argument_with_type_tag" &&
      getSyntax() == AttributeCommonInfo::AS_C23 && Scope == "clang")
    return 3;
  if (Name == "argument_with_type_tag" &&
      getSyntax() == AttributeCommonInfo::AS_C23 && Scope == "neverc")
    return 4;
  if (Name == "pointer_with_type_tag" &&
      getSyntax() == AttributeCommonInfo::AS_GNU && Scope == "")
    return 5;
  if (Name == "pointer_with_type_tag" &&
      getSyntax() == AttributeCommonInfo::AS_Bracket && Scope == "clang")
    return 6;
  if (Name == "pointer_with_type_tag" &&
      getSyntax() == AttributeCommonInfo::AS_Bracket && Scope == "neverc")
    return 7;
  if (Name == "pointer_with_type_tag" &&
      getSyntax() == AttributeCommonInfo::AS_C23 && Scope == "clang")
    return 8;
  if (Name == "pointer_with_type_tag" &&
      getSyntax() == AttributeCommonInfo::AS_C23 && Scope == "neverc")
    return 9;
  break;
}
case AT_ArmBuiltinAlias: {
  if (Name == "__neverc_arm_builtin_alias" &&
      getSyntax() == AttributeCommonInfo::AS_GNU && Scope == "")
    return 0;
  if (Name == "__neverc_arm_builtin_alias" &&
      getSyntax() == AttributeCommonInfo::AS_Bracket && Scope == "clang")
    return 1;
  if (Name == "__neverc_arm_builtin_alias" &&
      getSyntax() == AttributeCommonInfo::AS_Bracket && Scope == "neverc")
    return 2;
  if (Name == "__neverc_arm_builtin_alias" &&
      getSyntax() == AttributeCommonInfo::AS_C23 && Scope == "clang")
    return 3;
  if (Name == "__neverc_arm_builtin_alias" &&
      getSyntax() == AttributeCommonInfo::AS_C23 && Scope == "neverc")
    return 4;
  break;
}
case AT_ArmLocallyStreaming: {
  if (Name == "__arm_locally_streaming" &&
      getSyntax() == AttributeCommonInfo::AS_Keyword && Scope == "")
    return 0;
  break;
}
case AT_ArmNewZA: {
  if (Name == "__arm_new_za" &&
      getSyntax() == AttributeCommonInfo::AS_Keyword && Scope == "")
    return 0;
  break;
}
case AT_ArmPreservesZA: {
  if (Name == "__arm_preserves_za" &&
      getSyntax() == AttributeCommonInfo::AS_Keyword && Scope == "")
    return 0;
  break;
}
case AT_ArmSharedZA: {
  if (Name == "__arm_shared_za" &&
      getSyntax() == AttributeCommonInfo::AS_Keyword && Scope == "")
    return 0;
  break;
}
case AT_ArmStreaming: {
  if (Name == "__arm_streaming" &&
      getSyntax() == AttributeCommonInfo::AS_Keyword && Scope == "")
    return 0;
  break;
}
case AT_ArmStreamingCompatible: {
  if (Name == "__arm_streaming_compatible" &&
      getSyntax() == AttributeCommonInfo::AS_Keyword && Scope == "")
    return 0;
  break;
}
case AT_ArmSveVectorBits: {
  if (Name == "arm_sve_vector_bits" &&
      getSyntax() == AttributeCommonInfo::AS_GNU && Scope == "")
    return 0;
  break;
}
case AT_Artificial: {
  if (Name == "artificial" && getSyntax() == AttributeCommonInfo::AS_GNU &&
      Scope == "")
    return 0;
  if (Name == "artificial" && getSyntax() == AttributeCommonInfo::AS_Bracket &&
      Scope == "gnu")
    return 1;
  if (Name == "artificial" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "gnu")
    return 2;
  break;
}
case AT_AssumeAligned: {
  if (Name == "assume_aligned" && getSyntax() == AttributeCommonInfo::AS_GNU &&
      Scope == "")
    return 0;
  if (Name == "assume_aligned" &&
      getSyntax() == AttributeCommonInfo::AS_Bracket && Scope == "gnu")
    return 1;
  if (Name == "assume_aligned" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "gnu")
    return 2;
  break;
}
case AT_Assumption: {
  if (Name == "assume" && getSyntax() == AttributeCommonInfo::AS_GNU &&
      Scope == "")
    return 0;
  if (Name == "assume" && getSyntax() == AttributeCommonInfo::AS_Bracket &&
      Scope == "clang")
    return 1;
  if (Name == "assume" && getSyntax() == AttributeCommonInfo::AS_Bracket &&
      Scope == "neverc")
    return 2;
  if (Name == "assume" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "clang")
    return 3;
  if (Name == "assume" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "neverc")
    return 4;
  break;
}
case AT_Availability: {
  if (Name == "availability" && getSyntax() == AttributeCommonInfo::AS_GNU &&
      Scope == "")
    return 0;
  if (Name == "availability" &&
      getSyntax() == AttributeCommonInfo::AS_Bracket && Scope == "clang")
    return 1;
  if (Name == "availability" &&
      getSyntax() == AttributeCommonInfo::AS_Bracket && Scope == "neverc")
    return 2;
  if (Name == "availability" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "clang")
    return 3;
  if (Name == "availability" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "neverc")
    return 4;
  break;
}
case AT_AvailableOnlyInDefaultEvalMethod: {
  if (Name == "available_only_in_default_eval_method" &&
      getSyntax() == AttributeCommonInfo::AS_GNU && Scope == "")
    return 0;
  if (Name == "available_only_in_default_eval_method" &&
      getSyntax() == AttributeCommonInfo::AS_Bracket && Scope == "clang")
    return 1;
  if (Name == "available_only_in_default_eval_method" &&
      getSyntax() == AttributeCommonInfo::AS_Bracket && Scope == "neverc")
    return 2;
  if (Name == "available_only_in_default_eval_method" &&
      getSyntax() == AttributeCommonInfo::AS_C23 && Scope == "clang")
    return 3;
  if (Name == "available_only_in_default_eval_method" &&
      getSyntax() == AttributeCommonInfo::AS_C23 && Scope == "neverc")
    return 4;
  break;
}
case AT_BTFDeclTag: {
  if (Name == "btf_decl_tag" && getSyntax() == AttributeCommonInfo::AS_GNU &&
      Scope == "")
    return 0;
  if (Name == "btf_decl_tag" &&
      getSyntax() == AttributeCommonInfo::AS_Bracket && Scope == "clang")
    return 1;
  if (Name == "btf_decl_tag" &&
      getSyntax() == AttributeCommonInfo::AS_Bracket && Scope == "neverc")
    return 2;
  if (Name == "btf_decl_tag" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "clang")
    return 3;
  if (Name == "btf_decl_tag" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "neverc")
    return 4;
  break;
}
case AT_BTFTypeTag: {
  if (Name == "btf_type_tag" && getSyntax() == AttributeCommonInfo::AS_GNU &&
      Scope == "")
    return 0;
  if (Name == "btf_type_tag" &&
      getSyntax() == AttributeCommonInfo::AS_Bracket && Scope == "clang")
    return 1;
  if (Name == "btf_type_tag" &&
      getSyntax() == AttributeCommonInfo::AS_Bracket && Scope == "neverc")
    return 2;
  if (Name == "btf_type_tag" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "clang")
    return 3;
  if (Name == "btf_type_tag" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "neverc")
    return 4;
  break;
}
case AT_BuiltinAlias: {
  if (Name == "builtin_alias" &&
      getSyntax() == AttributeCommonInfo::AS_Bracket && Scope == "neverc")
    return 0;
  if (Name == "builtin_alias" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "neverc")
    return 1;
  if (Name == "neverc_builtin_alias" &&
      getSyntax() == AttributeCommonInfo::AS_GNU && Scope == "")
    return 2;
  break;
}
case AT_CDecl: {
  if (Name == "cdecl" && getSyntax() == AttributeCommonInfo::AS_GNU &&
      Scope == "")
    return 0;
  if (Name == "cdecl" && getSyntax() == AttributeCommonInfo::AS_Bracket &&
      Scope == "gnu")
    return 1;
  if (Name == "cdecl" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "gnu")
    return 2;
  if (Name == "__cdecl" && getSyntax() == AttributeCommonInfo::AS_Keyword &&
      Scope == "")
    return 3;
  if (Name == "_cdecl" && getSyntax() == AttributeCommonInfo::AS_Keyword &&
      Scope == "")
    return 4;
  if (Name == "cdecl" && getSyntax() == AttributeCommonInfo::AS_Keyword &&
      Scope == "")
    return 5;
  break;
}
case AT_CFGuard: {
  if (Name == "guard" && getSyntax() == AttributeCommonInfo::AS_Declspec &&
      Scope == "")
    return 0;
  if (Name == "guard" && getSyntax() == AttributeCommonInfo::AS_GNU &&
      Scope == "")
    return 1;
  if (Name == "guard" && getSyntax() == AttributeCommonInfo::AS_Bracket &&
      Scope == "clang")
    return 2;
  if (Name == "guard" && getSyntax() == AttributeCommonInfo::AS_Bracket &&
      Scope == "neverc")
    return 3;
  if (Name == "guard" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "clang")
    return 4;
  if (Name == "guard" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "neverc")
    return 5;
  break;
}
case AT_CPUDispatch: {
  if (Name == "cpu_dispatch" && getSyntax() == AttributeCommonInfo::AS_GNU &&
      Scope == "")
    return 0;
  if (Name == "cpu_dispatch" &&
      getSyntax() == AttributeCommonInfo::AS_Bracket && Scope == "clang")
    return 1;
  if (Name == "cpu_dispatch" &&
      getSyntax() == AttributeCommonInfo::AS_Bracket && Scope == "neverc")
    return 2;
  if (Name == "cpu_dispatch" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "clang")
    return 3;
  if (Name == "cpu_dispatch" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "neverc")
    return 4;
  if (Name == "cpu_dispatch" &&
      getSyntax() == AttributeCommonInfo::AS_Declspec && Scope == "")
    return 5;
  break;
}
case AT_CPUSpecific: {
  if (Name == "cpu_specific" && getSyntax() == AttributeCommonInfo::AS_GNU &&
      Scope == "")
    return 0;
  if (Name == "cpu_specific" &&
      getSyntax() == AttributeCommonInfo::AS_Bracket && Scope == "clang")
    return 1;
  if (Name == "cpu_specific" &&
      getSyntax() == AttributeCommonInfo::AS_Bracket && Scope == "neverc")
    return 2;
  if (Name == "cpu_specific" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "clang")
    return 3;
  if (Name == "cpu_specific" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "neverc")
    return 4;
  if (Name == "cpu_specific" &&
      getSyntax() == AttributeCommonInfo::AS_Declspec && Scope == "")
    return 5;
  break;
}
case AT_Callback: {
  if (Name == "callback" && getSyntax() == AttributeCommonInfo::AS_GNU &&
      Scope == "")
    return 0;
  if (Name == "callback" && getSyntax() == AttributeCommonInfo::AS_Bracket &&
      Scope == "clang")
    return 1;
  if (Name == "callback" && getSyntax() == AttributeCommonInfo::AS_Bracket &&
      Scope == "neverc")
    return 2;
  if (Name == "callback" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "clang")
    return 3;
  if (Name == "callback" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "neverc")
    return 4;
  break;
}
case AT_CarriesDependency: {
  if (Name == "carries_dependency" &&
      getSyntax() == AttributeCommonInfo::AS_GNU && Scope == "")
    return 0;
  if (Name == "carries_dependency" &&
      getSyntax() == AttributeCommonInfo::AS_Bracket && Scope == "")
    return 1;
  break;
}
case AT_Cleanup: {
  if (Name == "cleanup" && getSyntax() == AttributeCommonInfo::AS_GNU &&
      Scope == "")
    return 0;
  if (Name == "cleanup" && getSyntax() == AttributeCommonInfo::AS_Bracket &&
      Scope == "gnu")
    return 1;
  if (Name == "cleanup" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "gnu")
    return 2;
  break;
}
case AT_CodeAlign: {
  if (Name == "code_align" && getSyntax() == AttributeCommonInfo::AS_GNU &&
      Scope == "")
    return 0;
  if (Name == "code_align" && getSyntax() == AttributeCommonInfo::AS_Bracket &&
      Scope == "clang")
    return 1;
  if (Name == "code_align" && getSyntax() == AttributeCommonInfo::AS_Bracket &&
      Scope == "neverc")
    return 2;
  if (Name == "code_align" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "clang")
    return 3;
  if (Name == "code_align" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "neverc")
    return 4;
  break;
}
case AT_CodeSeg: {
  if (Name == "code_seg" && getSyntax() == AttributeCommonInfo::AS_Declspec &&
      Scope == "")
    return 0;
  break;
}
case AT_Cold: {
  if (Name == "cold" && getSyntax() == AttributeCommonInfo::AS_GNU &&
      Scope == "")
    return 0;
  if (Name == "cold" && getSyntax() == AttributeCommonInfo::AS_Bracket &&
      Scope == "gnu")
    return 1;
  if (Name == "cold" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "gnu")
    return 2;
  break;
}
case AT_Common: {
  if (Name == "common" && getSyntax() == AttributeCommonInfo::AS_GNU &&
      Scope == "")
    return 0;
  if (Name == "common" && getSyntax() == AttributeCommonInfo::AS_Bracket &&
      Scope == "gnu")
    return 1;
  if (Name == "common" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "gnu")
    return 2;
  break;
}
case AT_Const: {
  if (Name == "const" && getSyntax() == AttributeCommonInfo::AS_GNU &&
      Scope == "")
    return 0;
  if (Name == "const" && getSyntax() == AttributeCommonInfo::AS_Bracket &&
      Scope == "gnu")
    return 1;
  if (Name == "const" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "gnu")
    return 2;
  if (Name == "__const" && getSyntax() == AttributeCommonInfo::AS_GNU &&
      Scope == "")
    return 3;
  if (Name == "__const" && getSyntax() == AttributeCommonInfo::AS_Bracket &&
      Scope == "gnu")
    return 4;
  if (Name == "__const" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "gnu")
    return 5;
  break;
}
case AT_Constructor: {
  if (Name == "constructor" && getSyntax() == AttributeCommonInfo::AS_GNU &&
      Scope == "")
    return 0;
  if (Name == "constructor" && getSyntax() == AttributeCommonInfo::AS_Bracket &&
      Scope == "gnu")
    return 1;
  if (Name == "constructor" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "gnu")
    return 2;
  break;
}
case AT_Convergent: {
  if (Name == "convergent" && getSyntax() == AttributeCommonInfo::AS_GNU &&
      Scope == "")
    return 0;
  if (Name == "convergent" && getSyntax() == AttributeCommonInfo::AS_Bracket &&
      Scope == "clang")
    return 1;
  if (Name == "convergent" && getSyntax() == AttributeCommonInfo::AS_Bracket &&
      Scope == "neverc")
    return 2;
  if (Name == "convergent" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "clang")
    return 3;
  if (Name == "convergent" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "neverc")
    return 4;
  break;
}
case AT_DLLExport: {
  if (Name == "dllexport" && getSyntax() == AttributeCommonInfo::AS_Declspec &&
      Scope == "")
    return 0;
  if (Name == "dllexport" && getSyntax() == AttributeCommonInfo::AS_GNU &&
      Scope == "")
    return 1;
  if (Name == "dllexport" && getSyntax() == AttributeCommonInfo::AS_Bracket &&
      Scope == "gnu")
    return 2;
  if (Name == "dllexport" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "gnu")
    return 3;
  break;
}
case AT_DLLImport: {
  if (Name == "dllimport" && getSyntax() == AttributeCommonInfo::AS_Declspec &&
      Scope == "")
    return 0;
  if (Name == "dllimport" && getSyntax() == AttributeCommonInfo::AS_GNU &&
      Scope == "")
    return 1;
  if (Name == "dllimport" && getSyntax() == AttributeCommonInfo::AS_Bracket &&
      Scope == "gnu")
    return 2;
  if (Name == "dllimport" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "gnu")
    return 3;
  break;
}
case AT_Deprecated: {
  if (Name == "deprecated" && getSyntax() == AttributeCommonInfo::AS_GNU &&
      Scope == "")
    return 0;
  if (Name == "deprecated" && getSyntax() == AttributeCommonInfo::AS_Bracket &&
      Scope == "gnu")
    return 1;
  if (Name == "deprecated" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "gnu")
    return 2;
  if (Name == "deprecated" && getSyntax() == AttributeCommonInfo::AS_Declspec &&
      Scope == "")
    return 3;
  if (Name == "deprecated" && getSyntax() == AttributeCommonInfo::AS_Bracket &&
      Scope == "")
    return 4;
  if (Name == "deprecated" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "")
    return 5;
  break;
}
case AT_Destructor: {
  if (Name == "destructor" && getSyntax() == AttributeCommonInfo::AS_GNU &&
      Scope == "")
    return 0;
  if (Name == "destructor" && getSyntax() == AttributeCommonInfo::AS_Bracket &&
      Scope == "gnu")
    return 1;
  if (Name == "destructor" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "gnu")
    return 2;
  break;
}
case AT_DiagnoseAsBuiltin: {
  if (Name == "diagnose_as_builtin" &&
      getSyntax() == AttributeCommonInfo::AS_GNU && Scope == "")
    return 0;
  if (Name == "diagnose_as_builtin" &&
      getSyntax() == AttributeCommonInfo::AS_Bracket && Scope == "clang")
    return 1;
  if (Name == "diagnose_as_builtin" &&
      getSyntax() == AttributeCommonInfo::AS_Bracket && Scope == "neverc")
    return 2;
  if (Name == "diagnose_as_builtin" &&
      getSyntax() == AttributeCommonInfo::AS_C23 && Scope == "clang")
    return 3;
  if (Name == "diagnose_as_builtin" &&
      getSyntax() == AttributeCommonInfo::AS_C23 && Scope == "neverc")
    return 4;
  break;
}
case AT_DiagnoseIf: {
  if (Name == "diagnose_if" && getSyntax() == AttributeCommonInfo::AS_GNU &&
      Scope == "")
    return 0;
  break;
}
case AT_DisableTailCalls: {
  if (Name == "disable_tail_calls" &&
      getSyntax() == AttributeCommonInfo::AS_GNU && Scope == "")
    return 0;
  if (Name == "disable_tail_calls" &&
      getSyntax() == AttributeCommonInfo::AS_Bracket && Scope == "clang")
    return 1;
  if (Name == "disable_tail_calls" &&
      getSyntax() == AttributeCommonInfo::AS_Bracket && Scope == "neverc")
    return 2;
  if (Name == "disable_tail_calls" &&
      getSyntax() == AttributeCommonInfo::AS_C23 && Scope == "clang")
    return 3;
  if (Name == "disable_tail_calls" &&
      getSyntax() == AttributeCommonInfo::AS_C23 && Scope == "neverc")
    return 4;
  break;
}
case AT_DisableTryStmt: {
  if (Name == "disable_try_stmt" &&
      getSyntax() == AttributeCommonInfo::AS_GNU && Scope == "")
    return 0;
  if (Name == "disable_try_stmt" &&
      getSyntax() == AttributeCommonInfo::AS_Bracket && Scope == "gnu")
    return 1;
  if (Name == "disable_try_stmt" &&
      getSyntax() == AttributeCommonInfo::AS_C23 && Scope == "gnu")
    return 2;
  if (Name == "disable_try_stmt" &&
      getSyntax() == AttributeCommonInfo::AS_Declspec && Scope == "")
    return 3;
  break;
}
case AT_EnableIf: {
  if (Name == "enable_if" && getSyntax() == AttributeCommonInfo::AS_GNU &&
      Scope == "")
    return 0;
  break;
}
case AT_EnforceTCB: {
  if (Name == "enforce_tcb" && getSyntax() == AttributeCommonInfo::AS_GNU &&
      Scope == "")
    return 0;
  if (Name == "enforce_tcb" && getSyntax() == AttributeCommonInfo::AS_Bracket &&
      Scope == "clang")
    return 1;
  if (Name == "enforce_tcb" && getSyntax() == AttributeCommonInfo::AS_Bracket &&
      Scope == "neverc")
    return 2;
  if (Name == "enforce_tcb" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "clang")
    return 3;
  if (Name == "enforce_tcb" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "neverc")
    return 4;
  break;
}
case AT_EnforceTCBLeaf: {
  if (Name == "enforce_tcb_leaf" &&
      getSyntax() == AttributeCommonInfo::AS_GNU && Scope == "")
    return 0;
  if (Name == "enforce_tcb_leaf" &&
      getSyntax() == AttributeCommonInfo::AS_Bracket && Scope == "clang")
    return 1;
  if (Name == "enforce_tcb_leaf" &&
      getSyntax() == AttributeCommonInfo::AS_Bracket && Scope == "neverc")
    return 2;
  if (Name == "enforce_tcb_leaf" &&
      getSyntax() == AttributeCommonInfo::AS_C23 && Scope == "clang")
    return 3;
  if (Name == "enforce_tcb_leaf" &&
      getSyntax() == AttributeCommonInfo::AS_C23 && Scope == "neverc")
    return 4;
  break;
}
case AT_EnumExtensibility: {
  if (Name == "enum_extensibility" &&
      getSyntax() == AttributeCommonInfo::AS_GNU && Scope == "")
    return 0;
  if (Name == "enum_extensibility" &&
      getSyntax() == AttributeCommonInfo::AS_Bracket && Scope == "clang")
    return 1;
  if (Name == "enum_extensibility" &&
      getSyntax() == AttributeCommonInfo::AS_Bracket && Scope == "neverc")
    return 2;
  if (Name == "enum_extensibility" &&
      getSyntax() == AttributeCommonInfo::AS_C23 && Scope == "clang")
    return 3;
  if (Name == "enum_extensibility" &&
      getSyntax() == AttributeCommonInfo::AS_C23 && Scope == "neverc")
    return 4;
  break;
}
case AT_Error: {
  if (Name == "error" && getSyntax() == AttributeCommonInfo::AS_GNU &&
      Scope == "")
    return 0;
  if (Name == "error" && getSyntax() == AttributeCommonInfo::AS_Bracket &&
      Scope == "gnu")
    return 1;
  if (Name == "error" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "gnu")
    return 2;
  if (Name == "warning" && getSyntax() == AttributeCommonInfo::AS_GNU &&
      Scope == "")
    return 3;
  if (Name == "warning" && getSyntax() == AttributeCommonInfo::AS_Bracket &&
      Scope == "gnu")
    return 4;
  if (Name == "warning" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "gnu")
    return 5;
  break;
}
case AT_ExtVectorType: {
  if (Name == "ext_vector_type" && getSyntax() == AttributeCommonInfo::AS_GNU &&
      Scope == "")
    return 0;
  break;
}
case AT_FallThrough: {
  if (Name == "fallthrough" && getSyntax() == AttributeCommonInfo::AS_Bracket &&
      Scope == "")
    return 0;
  if (Name == "fallthrough" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "")
    return 1;
  if (Name == "fallthrough" && getSyntax() == AttributeCommonInfo::AS_Bracket &&
      Scope == "neverc")
    return 2;
  if (Name == "fallthrough" && getSyntax() == AttributeCommonInfo::AS_GNU &&
      Scope == "")
    return 3;
  if (Name == "fallthrough" && getSyntax() == AttributeCommonInfo::AS_Bracket &&
      Scope == "gnu")
    return 4;
  if (Name == "fallthrough" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "gnu")
    return 5;
  break;
}
case AT_FastCall: {
  if (Name == "fastcall" && getSyntax() == AttributeCommonInfo::AS_GNU &&
      Scope == "")
    return 0;
  if (Name == "fastcall" && getSyntax() == AttributeCommonInfo::AS_Bracket &&
      Scope == "gnu")
    return 1;
  if (Name == "fastcall" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "gnu")
    return 2;
  if (Name == "__fastcall" && getSyntax() == AttributeCommonInfo::AS_Keyword &&
      Scope == "")
    return 3;
  if (Name == "_fastcall" && getSyntax() == AttributeCommonInfo::AS_Keyword &&
      Scope == "")
    return 4;
  break;
}
case AT_FlagEnum: {
  if (Name == "flag_enum" && getSyntax() == AttributeCommonInfo::AS_GNU &&
      Scope == "")
    return 0;
  if (Name == "flag_enum" && getSyntax() == AttributeCommonInfo::AS_Bracket &&
      Scope == "clang")
    return 1;
  if (Name == "flag_enum" && getSyntax() == AttributeCommonInfo::AS_Bracket &&
      Scope == "neverc")
    return 2;
  if (Name == "flag_enum" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "clang")
    return 3;
  if (Name == "flag_enum" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "neverc")
    return 4;
  break;
}
case AT_Flatten: {
  if (Name == "flatten" && getSyntax() == AttributeCommonInfo::AS_GNU &&
      Scope == "")
    return 0;
  if (Name == "flatten" && getSyntax() == AttributeCommonInfo::AS_Bracket &&
      Scope == "gnu")
    return 1;
  if (Name == "flatten" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "gnu")
    return 2;
  break;
}
case AT_Format: {
  if (Name == "format" && getSyntax() == AttributeCommonInfo::AS_GNU &&
      Scope == "")
    return 0;
  if (Name == "format" && getSyntax() == AttributeCommonInfo::AS_Bracket &&
      Scope == "gnu")
    return 1;
  if (Name == "format" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "gnu")
    return 2;
  break;
}
case AT_FormatArg: {
  if (Name == "format_arg" && getSyntax() == AttributeCommonInfo::AS_GNU &&
      Scope == "")
    return 0;
  if (Name == "format_arg" && getSyntax() == AttributeCommonInfo::AS_Bracket &&
      Scope == "gnu")
    return 1;
  if (Name == "format_arg" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "gnu")
    return 2;
  break;
}
case AT_FunctionReturnThunks: {
  if (Name == "function_return" && getSyntax() == AttributeCommonInfo::AS_GNU &&
      Scope == "")
    return 0;
  if (Name == "function_return" &&
      getSyntax() == AttributeCommonInfo::AS_Bracket && Scope == "gnu")
    return 1;
  if (Name == "function_return" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "gnu")
    return 2;
  break;
}
case AT_GNUInline: {
  if (Name == "gnu_inline" && getSyntax() == AttributeCommonInfo::AS_GNU &&
      Scope == "")
    return 0;
  if (Name == "gnu_inline" && getSyntax() == AttributeCommonInfo::AS_Bracket &&
      Scope == "gnu")
    return 1;
  if (Name == "gnu_inline" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "gnu")
    return 2;
  break;
}
case AT_Hot: {
  if (Name == "hot" && getSyntax() == AttributeCommonInfo::AS_GNU &&
      Scope == "")
    return 0;
  if (Name == "hot" && getSyntax() == AttributeCommonInfo::AS_Bracket &&
      Scope == "gnu")
    return 1;
  if (Name == "hot" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "gnu")
    return 2;
  break;
}
case AT_IFunc: {
  if (Name == "ifunc" && getSyntax() == AttributeCommonInfo::AS_GNU &&
      Scope == "")
    return 0;
  if (Name == "ifunc" && getSyntax() == AttributeCommonInfo::AS_Bracket &&
      Scope == "gnu")
    return 1;
  if (Name == "ifunc" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "gnu")
    return 2;
  break;
}
case AT_InternalLinkage: {
  if (Name == "internal_linkage" &&
      getSyntax() == AttributeCommonInfo::AS_GNU && Scope == "")
    return 0;
  if (Name == "internal_linkage" &&
      getSyntax() == AttributeCommonInfo::AS_Bracket && Scope == "clang")
    return 1;
  if (Name == "internal_linkage" &&
      getSyntax() == AttributeCommonInfo::AS_Bracket && Scope == "neverc")
    return 2;
  if (Name == "internal_linkage" &&
      getSyntax() == AttributeCommonInfo::AS_C23 && Scope == "clang")
    return 3;
  if (Name == "internal_linkage" &&
      getSyntax() == AttributeCommonInfo::AS_C23 && Scope == "neverc")
    return 4;
  break;
}
case AT_LTOVisibilityPublic: {
  if (Name == "lto_visibility_public" &&
      getSyntax() == AttributeCommonInfo::AS_GNU && Scope == "")
    return 0;
  if (Name == "lto_visibility_public" &&
      getSyntax() == AttributeCommonInfo::AS_Bracket && Scope == "clang")
    return 1;
  if (Name == "lto_visibility_public" &&
      getSyntax() == AttributeCommonInfo::AS_Bracket && Scope == "neverc")
    return 2;
  if (Name == "lto_visibility_public" &&
      getSyntax() == AttributeCommonInfo::AS_C23 && Scope == "clang")
    return 3;
  if (Name == "lto_visibility_public" &&
      getSyntax() == AttributeCommonInfo::AS_C23 && Scope == "neverc")
    return 4;
  break;
}
case AT_Leaf: {
  if (Name == "leaf" && getSyntax() == AttributeCommonInfo::AS_GNU &&
      Scope == "")
    return 0;
  if (Name == "leaf" && getSyntax() == AttributeCommonInfo::AS_Bracket &&
      Scope == "gnu")
    return 1;
  if (Name == "leaf" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "gnu")
    return 2;
  break;
}
case AT_Likely: {
  if (Name == "likely" && getSyntax() == AttributeCommonInfo::AS_Bracket &&
      Scope == "")
    return 0;
  if (Name == "likely" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "neverc")
    return 1;
  break;
}
case AT_LoaderUninitialized: {
  if (Name == "loader_uninitialized" &&
      getSyntax() == AttributeCommonInfo::AS_GNU && Scope == "")
    return 0;
  if (Name == "loader_uninitialized" &&
      getSyntax() == AttributeCommonInfo::AS_Bracket && Scope == "clang")
    return 1;
  if (Name == "loader_uninitialized" &&
      getSyntax() == AttributeCommonInfo::AS_Bracket && Scope == "neverc")
    return 2;
  if (Name == "loader_uninitialized" &&
      getSyntax() == AttributeCommonInfo::AS_C23 && Scope == "clang")
    return 3;
  if (Name == "loader_uninitialized" &&
      getSyntax() == AttributeCommonInfo::AS_C23 && Scope == "neverc")
    return 4;
  break;
}
case AT_MSABI: {
  if (Name == "ms_abi" && getSyntax() == AttributeCommonInfo::AS_GNU &&
      Scope == "")
    return 0;
  if (Name == "ms_abi" && getSyntax() == AttributeCommonInfo::AS_Bracket &&
      Scope == "gnu")
    return 1;
  if (Name == "ms_abi" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "gnu")
    return 2;
  break;
}
case AT_MSAllocator: {
  if (Name == "allocator" && getSyntax() == AttributeCommonInfo::AS_Declspec &&
      Scope == "")
    return 0;
  break;
}
case AT_MSStruct: {
  if (Name == "ms_struct" && getSyntax() == AttributeCommonInfo::AS_GNU &&
      Scope == "")
    return 0;
  if (Name == "ms_struct" && getSyntax() == AttributeCommonInfo::AS_Bracket &&
      Scope == "gnu")
    return 1;
  if (Name == "ms_struct" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "gnu")
    return 2;
  break;
}
case AT_MatrixType: {
  if (Name == "matrix_type" && getSyntax() == AttributeCommonInfo::AS_GNU &&
      Scope == "")
    return 0;
  if (Name == "matrix_type" && getSyntax() == AttributeCommonInfo::AS_Bracket &&
      Scope == "clang")
    return 1;
  if (Name == "matrix_type" && getSyntax() == AttributeCommonInfo::AS_Bracket &&
      Scope == "neverc")
    return 2;
  if (Name == "matrix_type" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "clang")
    return 3;
  if (Name == "matrix_type" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "neverc")
    return 4;
  break;
}
case AT_MayAlias: {
  if (Name == "may_alias" && getSyntax() == AttributeCommonInfo::AS_GNU &&
      Scope == "")
    return 0;
  if (Name == "may_alias" && getSyntax() == AttributeCommonInfo::AS_Bracket &&
      Scope == "gnu")
    return 1;
  if (Name == "may_alias" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "gnu")
    return 2;
  break;
}
case AT_MaybeUndef: {
  if (Name == "maybe_undef" && getSyntax() == AttributeCommonInfo::AS_GNU &&
      Scope == "")
    return 0;
  if (Name == "maybe_undef" && getSyntax() == AttributeCommonInfo::AS_Bracket &&
      Scope == "clang")
    return 1;
  if (Name == "maybe_undef" && getSyntax() == AttributeCommonInfo::AS_Bracket &&
      Scope == "neverc")
    return 2;
  if (Name == "maybe_undef" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "clang")
    return 3;
  if (Name == "maybe_undef" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "neverc")
    return 4;
  break;
}
case AT_MinSize: {
  if (Name == "minsize" && getSyntax() == AttributeCommonInfo::AS_GNU &&
      Scope == "")
    return 0;
  if (Name == "minsize" && getSyntax() == AttributeCommonInfo::AS_Bracket &&
      Scope == "clang")
    return 1;
  if (Name == "minsize" && getSyntax() == AttributeCommonInfo::AS_Bracket &&
      Scope == "neverc")
    return 2;
  if (Name == "minsize" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "clang")
    return 3;
  if (Name == "minsize" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "neverc")
    return 4;
  break;
}
case AT_MinVectorWidth: {
  if (Name == "min_vector_width" &&
      getSyntax() == AttributeCommonInfo::AS_GNU && Scope == "")
    return 0;
  if (Name == "min_vector_width" &&
      getSyntax() == AttributeCommonInfo::AS_Bracket && Scope == "clang")
    return 1;
  if (Name == "min_vector_width" &&
      getSyntax() == AttributeCommonInfo::AS_Bracket && Scope == "neverc")
    return 2;
  if (Name == "min_vector_width" &&
      getSyntax() == AttributeCommonInfo::AS_C23 && Scope == "clang")
    return 3;
  if (Name == "min_vector_width" &&
      getSyntax() == AttributeCommonInfo::AS_C23 && Scope == "neverc")
    return 4;
  break;
}
case AT_Mode: {
  if (Name == "mode" && getSyntax() == AttributeCommonInfo::AS_GNU &&
      Scope == "")
    return 0;
  if (Name == "mode" && getSyntax() == AttributeCommonInfo::AS_Bracket &&
      Scope == "gnu")
    return 1;
  if (Name == "mode" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "gnu")
    return 2;
  break;
}
case AT_MustTail: {
  if (Name == "musttail" && getSyntax() == AttributeCommonInfo::AS_GNU &&
      Scope == "")
    return 0;
  if (Name == "musttail" && getSyntax() == AttributeCommonInfo::AS_Bracket &&
      Scope == "clang")
    return 1;
  if (Name == "musttail" && getSyntax() == AttributeCommonInfo::AS_Bracket &&
      Scope == "neverc")
    return 2;
  if (Name == "musttail" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "clang")
    return 3;
  if (Name == "musttail" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "neverc")
    return 4;
  break;
}
case AT_Naked: {
  if (Name == "naked" && getSyntax() == AttributeCommonInfo::AS_GNU &&
      Scope == "")
    return 0;
  if (Name == "naked" && getSyntax() == AttributeCommonInfo::AS_Bracket &&
      Scope == "gnu")
    return 1;
  if (Name == "naked" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "gnu")
    return 2;
  if (Name == "naked" && getSyntax() == AttributeCommonInfo::AS_Declspec &&
      Scope == "")
    return 3;
  break;
}
case AT_NeonPolyVectorType: {
  if (Name == "neon_polyvector_type" &&
      getSyntax() == AttributeCommonInfo::AS_GNU && Scope == "")
    return 0;
  if (Name == "neon_polyvector_type" &&
      getSyntax() == AttributeCommonInfo::AS_Bracket && Scope == "clang")
    return 1;
  if (Name == "neon_polyvector_type" &&
      getSyntax() == AttributeCommonInfo::AS_Bracket && Scope == "neverc")
    return 2;
  if (Name == "neon_polyvector_type" &&
      getSyntax() == AttributeCommonInfo::AS_C23 && Scope == "clang")
    return 3;
  if (Name == "neon_polyvector_type" &&
      getSyntax() == AttributeCommonInfo::AS_C23 && Scope == "neverc")
    return 4;
  break;
}
case AT_NeonVectorType: {
  if (Name == "neon_vector_type" &&
      getSyntax() == AttributeCommonInfo::AS_GNU && Scope == "")
    return 0;
  if (Name == "neon_vector_type" &&
      getSyntax() == AttributeCommonInfo::AS_Bracket && Scope == "clang")
    return 1;
  if (Name == "neon_vector_type" &&
      getSyntax() == AttributeCommonInfo::AS_Bracket && Scope == "neverc")
    return 2;
  if (Name == "neon_vector_type" &&
      getSyntax() == AttributeCommonInfo::AS_C23 && Scope == "clang")
    return 3;
  if (Name == "neon_vector_type" &&
      getSyntax() == AttributeCommonInfo::AS_C23 && Scope == "neverc")
    return 4;
  break;
}
case AT_NoAlias: {
  if (Name == "noalias" && getSyntax() == AttributeCommonInfo::AS_Declspec &&
      Scope == "")
    return 0;
  break;
}
case AT_NoBuiltin: {
  if (Name == "no_builtin" && getSyntax() == AttributeCommonInfo::AS_GNU &&
      Scope == "")
    return 0;
  if (Name == "no_builtin" && getSyntax() == AttributeCommonInfo::AS_Bracket &&
      Scope == "clang")
    return 1;
  if (Name == "no_builtin" && getSyntax() == AttributeCommonInfo::AS_Bracket &&
      Scope == "neverc")
    return 2;
  if (Name == "no_builtin" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "clang")
    return 3;
  if (Name == "no_builtin" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "neverc")
    return 4;
  break;
}
case AT_NoCommon: {
  if (Name == "nocommon" && getSyntax() == AttributeCommonInfo::AS_GNU &&
      Scope == "")
    return 0;
  if (Name == "nocommon" && getSyntax() == AttributeCommonInfo::AS_Bracket &&
      Scope == "gnu")
    return 1;
  if (Name == "nocommon" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "gnu")
    return 2;
  break;
}
case AT_NoDebug: {
  if (Name == "nodebug" && getSyntax() == AttributeCommonInfo::AS_GNU &&
      Scope == "")
    return 0;
  if (Name == "nodebug" && getSyntax() == AttributeCommonInfo::AS_Bracket &&
      Scope == "gnu")
    return 1;
  if (Name == "nodebug" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "gnu")
    return 2;
  break;
}
case AT_NoDeref: {
  if (Name == "noderef" && getSyntax() == AttributeCommonInfo::AS_GNU &&
      Scope == "")
    return 0;
  if (Name == "noderef" && getSyntax() == AttributeCommonInfo::AS_Bracket &&
      Scope == "clang")
    return 1;
  if (Name == "noderef" && getSyntax() == AttributeCommonInfo::AS_Bracket &&
      Scope == "neverc")
    return 2;
  if (Name == "noderef" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "clang")
    return 3;
  if (Name == "noderef" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "neverc")
    return 4;
  break;
}
case AT_NoDestroy: {
  if (Name == "no_destroy" && getSyntax() == AttributeCommonInfo::AS_GNU &&
      Scope == "")
    return 0;
  if (Name == "no_destroy" && getSyntax() == AttributeCommonInfo::AS_Bracket &&
      Scope == "clang")
    return 1;
  if (Name == "no_destroy" && getSyntax() == AttributeCommonInfo::AS_Bracket &&
      Scope == "neverc")
    return 2;
  break;
}
case AT_NoDuplicate: {
  if (Name == "noduplicate" && getSyntax() == AttributeCommonInfo::AS_GNU &&
      Scope == "")
    return 0;
  if (Name == "noduplicate" && getSyntax() == AttributeCommonInfo::AS_Bracket &&
      Scope == "clang")
    return 1;
  if (Name == "noduplicate" && getSyntax() == AttributeCommonInfo::AS_Bracket &&
      Scope == "neverc")
    return 2;
  if (Name == "noduplicate" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "clang")
    return 3;
  if (Name == "noduplicate" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "neverc")
    return 4;
  break;
}
case AT_NoEscape: {
  if (Name == "noescape" && getSyntax() == AttributeCommonInfo::AS_GNU &&
      Scope == "")
    return 0;
  if (Name == "noescape" && getSyntax() == AttributeCommonInfo::AS_Bracket &&
      Scope == "clang")
    return 1;
  if (Name == "noescape" && getSyntax() == AttributeCommonInfo::AS_Bracket &&
      Scope == "neverc")
    return 2;
  if (Name == "noescape" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "clang")
    return 3;
  if (Name == "noescape" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "neverc")
    return 4;
  break;
}
case AT_NoInline: {
  if (Name == "__noinline__" &&
      getSyntax() == AttributeCommonInfo::AS_Keyword && Scope == "")
    return 0;
  if (Name == "noinline" && getSyntax() == AttributeCommonInfo::AS_GNU &&
      Scope == "")
    return 1;
  if (Name == "noinline" && getSyntax() == AttributeCommonInfo::AS_Bracket &&
      Scope == "gnu")
    return 2;
  if (Name == "noinline" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "gnu")
    return 3;
  if (Name == "noinline" && getSyntax() == AttributeCommonInfo::AS_Bracket &&
      Scope == "neverc")
    return 4;
  if (Name == "noinline" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "neverc")
    return 5;
  if (Name == "noinline" && getSyntax() == AttributeCommonInfo::AS_Declspec &&
      Scope == "")
    return 6;
  break;
}
case AT_NoMerge: {
  if (Name == "nomerge" && getSyntax() == AttributeCommonInfo::AS_GNU &&
      Scope == "")
    return 0;
  if (Name == "nomerge" && getSyntax() == AttributeCommonInfo::AS_Bracket &&
      Scope == "clang")
    return 1;
  if (Name == "nomerge" && getSyntax() == AttributeCommonInfo::AS_Bracket &&
      Scope == "neverc")
    return 2;
  if (Name == "nomerge" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "clang")
    return 3;
  if (Name == "nomerge" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "neverc")
    return 4;
  break;
}
case AT_NoRandomizeLayout: {
  if (Name == "no_randomize_layout" &&
      getSyntax() == AttributeCommonInfo::AS_GNU && Scope == "")
    return 0;
  if (Name == "no_randomize_layout" &&
      getSyntax() == AttributeCommonInfo::AS_Bracket && Scope == "gnu")
    return 1;
  if (Name == "no_randomize_layout" &&
      getSyntax() == AttributeCommonInfo::AS_C23 && Scope == "gnu")
    return 2;
  break;
}
case AT_NoReturn: {
  if (Name == "noreturn" && getSyntax() == AttributeCommonInfo::AS_GNU &&
      Scope == "")
    return 0;
  if (Name == "noreturn" && getSyntax() == AttributeCommonInfo::AS_Bracket &&
      Scope == "gnu")
    return 1;
  if (Name == "noreturn" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "gnu")
    return 2;
  if (Name == "noreturn" && getSyntax() == AttributeCommonInfo::AS_Declspec &&
      Scope == "")
    return 3;
  break;
}
case AT_NoSpeculativeLoadHardening: {
  if (Name == "no_speculative_load_hardening" &&
      getSyntax() == AttributeCommonInfo::AS_GNU && Scope == "")
    return 0;
  if (Name == "no_speculative_load_hardening" &&
      getSyntax() == AttributeCommonInfo::AS_Bracket && Scope == "clang")
    return 1;
  if (Name == "no_speculative_load_hardening" &&
      getSyntax() == AttributeCommonInfo::AS_Bracket && Scope == "neverc")
    return 2;
  if (Name == "no_speculative_load_hardening" &&
      getSyntax() == AttributeCommonInfo::AS_C23 && Scope == "clang")
    return 3;
  if (Name == "no_speculative_load_hardening" &&
      getSyntax() == AttributeCommonInfo::AS_C23 && Scope == "neverc")
    return 4;
  break;
}
case AT_NoSplitStack: {
  if (Name == "no_split_stack" && getSyntax() == AttributeCommonInfo::AS_GNU &&
      Scope == "")
    return 0;
  if (Name == "no_split_stack" &&
      getSyntax() == AttributeCommonInfo::AS_Bracket && Scope == "gnu")
    return 1;
  if (Name == "no_split_stack" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "gnu")
    return 2;
  break;
}
case AT_NoStackProtector: {
  if (Name == "no_stack_protector" &&
      getSyntax() == AttributeCommonInfo::AS_GNU && Scope == "")
    return 0;
  if (Name == "no_stack_protector" &&
      getSyntax() == AttributeCommonInfo::AS_Bracket && Scope == "clang")
    return 1;
  if (Name == "no_stack_protector" &&
      getSyntax() == AttributeCommonInfo::AS_Bracket && Scope == "neverc")
    return 2;
  if (Name == "no_stack_protector" &&
      getSyntax() == AttributeCommonInfo::AS_C23 && Scope == "clang")
    return 3;
  if (Name == "no_stack_protector" &&
      getSyntax() == AttributeCommonInfo::AS_C23 && Scope == "neverc")
    return 4;
  if (Name == "no_stack_protector" &&
      getSyntax() == AttributeCommonInfo::AS_Bracket && Scope == "gnu")
    return 5;
  if (Name == "no_stack_protector" &&
      getSyntax() == AttributeCommonInfo::AS_C23 && Scope == "gnu")
    return 6;
  if (Name == "safebuffers" &&
      getSyntax() == AttributeCommonInfo::AS_Declspec && Scope == "")
    return 7;
  break;
}
case AT_NoThrow: {
  if (Name == "nothrow" && getSyntax() == AttributeCommonInfo::AS_GNU &&
      Scope == "")
    return 0;
  if (Name == "nothrow" && getSyntax() == AttributeCommonInfo::AS_Bracket &&
      Scope == "gnu")
    return 1;
  if (Name == "nothrow" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "gnu")
    return 2;
  if (Name == "nothrow" && getSyntax() == AttributeCommonInfo::AS_Declspec &&
      Scope == "")
    return 3;
  break;
}
case AT_NoUwtable: {
  if (Name == "nouwtable" && getSyntax() == AttributeCommonInfo::AS_GNU &&
      Scope == "")
    return 0;
  if (Name == "nouwtable" && getSyntax() == AttributeCommonInfo::AS_Bracket &&
      Scope == "clang")
    return 1;
  if (Name == "nouwtable" && getSyntax() == AttributeCommonInfo::AS_Bracket &&
      Scope == "neverc")
    return 2;
  if (Name == "nouwtable" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "clang")
    return 3;
  if (Name == "nouwtable" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "neverc")
    return 4;
  break;
}
case AT_NonNull: {
  if (Name == "nonnull" && getSyntax() == AttributeCommonInfo::AS_GNU &&
      Scope == "")
    return 0;
  if (Name == "nonnull" && getSyntax() == AttributeCommonInfo::AS_Bracket &&
      Scope == "gnu")
    return 1;
  if (Name == "nonnull" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "gnu")
    return 2;
  break;
}
case AT_NotTailCalled: {
  if (Name == "not_tail_called" && getSyntax() == AttributeCommonInfo::AS_GNU &&
      Scope == "")
    return 0;
  if (Name == "not_tail_called" &&
      getSyntax() == AttributeCommonInfo::AS_Bracket && Scope == "clang")
    return 1;
  if (Name == "not_tail_called" &&
      getSyntax() == AttributeCommonInfo::AS_Bracket && Scope == "neverc")
    return 2;
  if (Name == "not_tail_called" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "clang")
    return 3;
  if (Name == "not_tail_called" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "neverc")
    return 4;
  break;
}
case AT_OptimizeNone: {
  if (Name == "optnone" && getSyntax() == AttributeCommonInfo::AS_GNU &&
      Scope == "")
    return 0;
  if (Name == "optnone" && getSyntax() == AttributeCommonInfo::AS_Bracket &&
      Scope == "clang")
    return 1;
  if (Name == "optnone" && getSyntax() == AttributeCommonInfo::AS_Bracket &&
      Scope == "neverc")
    return 2;
  if (Name == "optnone" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "clang")
    return 3;
  if (Name == "optnone" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "neverc")
    return 4;
  break;
}
case AT_Overloadable: {
  if (Name == "overloadable" && getSyntax() == AttributeCommonInfo::AS_GNU &&
      Scope == "")
    return 0;
  if (Name == "overloadable" &&
      getSyntax() == AttributeCommonInfo::AS_Bracket && Scope == "clang")
    return 1;
  if (Name == "overloadable" &&
      getSyntax() == AttributeCommonInfo::AS_Bracket && Scope == "neverc")
    return 2;
  if (Name == "overloadable" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "clang")
    return 3;
  if (Name == "overloadable" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "neverc")
    return 4;
  break;
}
case AT_Override: {
  if (Name == "override" && getSyntax() == AttributeCommonInfo::AS_GNU &&
      Scope == "")
    return 0;
  if (Name == "override" && getSyntax() == AttributeCommonInfo::AS_Bracket &&
      Scope == "gnu")
    return 1;
  if (Name == "override" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "gnu")
    return 2;
  if (Name == "override" && getSyntax() == AttributeCommonInfo::AS_Declspec &&
      Scope == "")
    return 3;
  break;
}
case AT_Packed: {
  if (Name == "packed" && getSyntax() == AttributeCommonInfo::AS_GNU &&
      Scope == "")
    return 0;
  if (Name == "packed" && getSyntax() == AttributeCommonInfo::AS_Bracket &&
      Scope == "gnu")
    return 1;
  if (Name == "packed" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "gnu")
    return 2;
  break;
}
case AT_PassObjectSize: {
  if (Name == "pass_object_size" &&
      getSyntax() == AttributeCommonInfo::AS_GNU && Scope == "")
    return 0;
  if (Name == "pass_object_size" &&
      getSyntax() == AttributeCommonInfo::AS_Bracket && Scope == "clang")
    return 1;
  if (Name == "pass_object_size" &&
      getSyntax() == AttributeCommonInfo::AS_Bracket && Scope == "neverc")
    return 2;
  if (Name == "pass_object_size" &&
      getSyntax() == AttributeCommonInfo::AS_C23 && Scope == "clang")
    return 3;
  if (Name == "pass_object_size" &&
      getSyntax() == AttributeCommonInfo::AS_C23 && Scope == "neverc")
    return 4;
  if (Name == "pass_dynamic_object_size" &&
      getSyntax() == AttributeCommonInfo::AS_GNU && Scope == "")
    return 5;
  if (Name == "pass_dynamic_object_size" &&
      getSyntax() == AttributeCommonInfo::AS_Bracket && Scope == "clang")
    return 6;
  if (Name == "pass_dynamic_object_size" &&
      getSyntax() == AttributeCommonInfo::AS_Bracket && Scope == "neverc")
    return 7;
  if (Name == "pass_dynamic_object_size" &&
      getSyntax() == AttributeCommonInfo::AS_C23 && Scope == "clang")
    return 8;
  if (Name == "pass_dynamic_object_size" &&
      getSyntax() == AttributeCommonInfo::AS_C23 && Scope == "neverc")
    return 9;
  break;
}
case AT_PatchableFunctionEntry: {
  if (Name == "patchable_function_entry" &&
      getSyntax() == AttributeCommonInfo::AS_GNU && Scope == "")
    return 0;
  if (Name == "patchable_function_entry" &&
      getSyntax() == AttributeCommonInfo::AS_Bracket && Scope == "gnu")
    return 1;
  if (Name == "patchable_function_entry" &&
      getSyntax() == AttributeCommonInfo::AS_C23 && Scope == "gnu")
    return 2;
  break;
}
case AT_PragmaNeverCBSSSection: {
  break;
}
case AT_PragmaNeverCDataSection: {
  break;
}
case AT_PragmaNeverCRelroSection: {
  break;
}
case AT_PragmaNeverCRodataSection: {
  break;
}
case AT_PragmaNeverCTextSection: {
  break;
}
case AT_PreferredType: {
  if (Name == "preferred_type" && getSyntax() == AttributeCommonInfo::AS_GNU &&
      Scope == "")
    return 0;
  if (Name == "preferred_type" &&
      getSyntax() == AttributeCommonInfo::AS_Bracket && Scope == "clang")
    return 1;
  if (Name == "preferred_type" &&
      getSyntax() == AttributeCommonInfo::AS_Bracket && Scope == "neverc")
    return 2;
  if (Name == "preferred_type" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "clang")
    return 3;
  if (Name == "preferred_type" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "neverc")
    return 4;
  break;
}
case AT_PreserveAll: {
  if (Name == "preserve_all" && getSyntax() == AttributeCommonInfo::AS_GNU &&
      Scope == "")
    return 0;
  if (Name == "preserve_all" &&
      getSyntax() == AttributeCommonInfo::AS_Bracket && Scope == "clang")
    return 1;
  if (Name == "preserve_all" &&
      getSyntax() == AttributeCommonInfo::AS_Bracket && Scope == "neverc")
    return 2;
  if (Name == "preserve_all" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "clang")
    return 3;
  if (Name == "preserve_all" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "neverc")
    return 4;
  break;
}
case AT_PreserveMost: {
  if (Name == "preserve_most" && getSyntax() == AttributeCommonInfo::AS_GNU &&
      Scope == "")
    return 0;
  if (Name == "preserve_most" &&
      getSyntax() == AttributeCommonInfo::AS_Bracket && Scope == "clang")
    return 1;
  if (Name == "preserve_most" &&
      getSyntax() == AttributeCommonInfo::AS_Bracket && Scope == "neverc")
    return 2;
  if (Name == "preserve_most" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "clang")
    return 3;
  if (Name == "preserve_most" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "neverc")
    return 4;
  break;
}
case AT_Ptr32: {
  if (Name == "__ptr32" && getSyntax() == AttributeCommonInfo::AS_Keyword &&
      Scope == "")
    return 0;
  break;
}
case AT_Ptr64: {
  if (Name == "__ptr64" && getSyntax() == AttributeCommonInfo::AS_Keyword &&
      Scope == "")
    return 0;
  break;
}
case AT_Pure: {
  if (Name == "pure" && getSyntax() == AttributeCommonInfo::AS_GNU &&
      Scope == "")
    return 0;
  if (Name == "pure" && getSyntax() == AttributeCommonInfo::AS_Bracket &&
      Scope == "gnu")
    return 1;
  if (Name == "pure" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "gnu")
    return 2;
  break;
}
case AT_RandomizeLayout: {
  if (Name == "randomize_layout" &&
      getSyntax() == AttributeCommonInfo::AS_GNU && Scope == "")
    return 0;
  if (Name == "randomize_layout" &&
      getSyntax() == AttributeCommonInfo::AS_Bracket && Scope == "gnu")
    return 1;
  if (Name == "randomize_layout" &&
      getSyntax() == AttributeCommonInfo::AS_C23 && Scope == "gnu")
    return 2;
  break;
}
case AT_ReadOnlyPlacement: {
  if (Name == "enforce_read_only_placement" &&
      getSyntax() == AttributeCommonInfo::AS_GNU && Scope == "")
    return 0;
  if (Name == "enforce_read_only_placement" &&
      getSyntax() == AttributeCommonInfo::AS_Bracket && Scope == "clang")
    return 1;
  if (Name == "enforce_read_only_placement" &&
      getSyntax() == AttributeCommonInfo::AS_Bracket && Scope == "neverc")
    return 2;
  if (Name == "enforce_read_only_placement" &&
      getSyntax() == AttributeCommonInfo::AS_C23 && Scope == "clang")
    return 3;
  if (Name == "enforce_read_only_placement" &&
      getSyntax() == AttributeCommonInfo::AS_C23 && Scope == "neverc")
    return 4;
  break;
}
case AT_RegCall: {
  if (Name == "regcall" && getSyntax() == AttributeCommonInfo::AS_GNU &&
      Scope == "")
    return 0;
  if (Name == "regcall" && getSyntax() == AttributeCommonInfo::AS_Bracket &&
      Scope == "gnu")
    return 1;
  if (Name == "regcall" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "gnu")
    return 2;
  if (Name == "__regcall" && getSyntax() == AttributeCommonInfo::AS_Keyword &&
      Scope == "")
    return 3;
  break;
}
case AT_Regparm: {
  if (Name == "regparm" && getSyntax() == AttributeCommonInfo::AS_GNU &&
      Scope == "")
    return 0;
  if (Name == "regparm" && getSyntax() == AttributeCommonInfo::AS_Bracket &&
      Scope == "gnu")
    return 1;
  if (Name == "regparm" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "gnu")
    return 2;
  break;
}
case AT_ReleaseHandle: {
  if (Name == "release_handle" && getSyntax() == AttributeCommonInfo::AS_GNU &&
      Scope == "")
    return 0;
  if (Name == "release_handle" &&
      getSyntax() == AttributeCommonInfo::AS_Bracket && Scope == "clang")
    return 1;
  if (Name == "release_handle" &&
      getSyntax() == AttributeCommonInfo::AS_Bracket && Scope == "neverc")
    return 2;
  if (Name == "release_handle" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "clang")
    return 3;
  if (Name == "release_handle" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "neverc")
    return 4;
  break;
}
case AT_Restrict: {
  if (Name == "restrict" && getSyntax() == AttributeCommonInfo::AS_Declspec &&
      Scope == "")
    return 0;
  if (Name == "malloc" && getSyntax() == AttributeCommonInfo::AS_GNU &&
      Scope == "")
    return 1;
  if (Name == "malloc" && getSyntax() == AttributeCommonInfo::AS_Bracket &&
      Scope == "gnu")
    return 2;
  if (Name == "malloc" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "gnu")
    return 3;
  break;
}
case AT_Retain: {
  if (Name == "retain" && getSyntax() == AttributeCommonInfo::AS_GNU &&
      Scope == "")
    return 0;
  if (Name == "retain" && getSyntax() == AttributeCommonInfo::AS_Bracket &&
      Scope == "gnu")
    return 1;
  if (Name == "retain" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "gnu")
    return 2;
  break;
}
case AT_ReturnsNonNull: {
  if (Name == "returns_nonnull" && getSyntax() == AttributeCommonInfo::AS_GNU &&
      Scope == "")
    return 0;
  if (Name == "returns_nonnull" &&
      getSyntax() == AttributeCommonInfo::AS_Bracket && Scope == "gnu")
    return 1;
  if (Name == "returns_nonnull" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "gnu")
    return 2;
  break;
}
case AT_ReturnsTwice: {
  if (Name == "returns_twice" && getSyntax() == AttributeCommonInfo::AS_GNU &&
      Scope == "")
    return 0;
  if (Name == "returns_twice" &&
      getSyntax() == AttributeCommonInfo::AS_Bracket && Scope == "gnu")
    return 1;
  if (Name == "returns_twice" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "gnu")
    return 2;
  break;
}
case AT_SPtr: {
  if (Name == "__sptr" && getSyntax() == AttributeCommonInfo::AS_Keyword &&
      Scope == "")
    return 0;
  break;
}
case AT_Section: {
  if (Name == "section" && getSyntax() == AttributeCommonInfo::AS_GNU &&
      Scope == "")
    return 0;
  if (Name == "section" && getSyntax() == AttributeCommonInfo::AS_Bracket &&
      Scope == "gnu")
    return 1;
  if (Name == "section" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "gnu")
    return 2;
  if (Name == "allocate" && getSyntax() == AttributeCommonInfo::AS_Declspec &&
      Scope == "")
    return 3;
  break;
}
case AT_SelectAny: {
  if (Name == "selectany" && getSyntax() == AttributeCommonInfo::AS_Declspec &&
      Scope == "")
    return 0;
  if (Name == "selectany" && getSyntax() == AttributeCommonInfo::AS_GNU &&
      Scope == "")
    return 1;
  if (Name == "selectany" && getSyntax() == AttributeCommonInfo::AS_Bracket &&
      Scope == "gnu")
    return 2;
  if (Name == "selectany" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "gnu")
    return 3;
  break;
}
case AT_Sentinel: {
  if (Name == "sentinel" && getSyntax() == AttributeCommonInfo::AS_GNU &&
      Scope == "")
    return 0;
  if (Name == "sentinel" && getSyntax() == AttributeCommonInfo::AS_Bracket &&
      Scope == "gnu")
    return 1;
  if (Name == "sentinel" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "gnu")
    return 2;
  break;
}
case AT_SpeculativeLoadHardening: {
  if (Name == "speculative_load_hardening" &&
      getSyntax() == AttributeCommonInfo::AS_GNU && Scope == "")
    return 0;
  if (Name == "speculative_load_hardening" &&
      getSyntax() == AttributeCommonInfo::AS_Bracket && Scope == "clang")
    return 1;
  if (Name == "speculative_load_hardening" &&
      getSyntax() == AttributeCommonInfo::AS_Bracket && Scope == "neverc")
    return 2;
  if (Name == "speculative_load_hardening" &&
      getSyntax() == AttributeCommonInfo::AS_C23 && Scope == "clang")
    return 3;
  if (Name == "speculative_load_hardening" &&
      getSyntax() == AttributeCommonInfo::AS_C23 && Scope == "neverc")
    return 4;
  break;
}
case AT_StandardNoReturn: {
  if (Name == "noreturn" && getSyntax() == AttributeCommonInfo::AS_Bracket &&
      Scope == "")
    return 0;
  if (Name == "noreturn" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "")
    return 1;
  if (Name == "_Noreturn" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "")
    return 2;
  break;
}
case AT_StdCall: {
  if (Name == "stdcall" && getSyntax() == AttributeCommonInfo::AS_GNU &&
      Scope == "")
    return 0;
  if (Name == "stdcall" && getSyntax() == AttributeCommonInfo::AS_Bracket &&
      Scope == "gnu")
    return 1;
  if (Name == "stdcall" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "gnu")
    return 2;
  if (Name == "__stdcall" && getSyntax() == AttributeCommonInfo::AS_Keyword &&
      Scope == "")
    return 3;
  if (Name == "_stdcall" && getSyntax() == AttributeCommonInfo::AS_Keyword &&
      Scope == "")
    return 4;
  break;
}
case AT_StrictFP: {
  break;
}
case AT_StrictGuardStackCheck: {
  if (Name == "strict_gs_check" &&
      getSyntax() == AttributeCommonInfo::AS_Declspec && Scope == "")
    return 0;
  break;
}
case AT_Suppress: {
  if (Name == "suppress" && getSyntax() == AttributeCommonInfo::AS_Bracket &&
      Scope == "gsl")
    return 0;
  if (Name == "suppress" && getSyntax() == AttributeCommonInfo::AS_GNU &&
      Scope == "")
    return 1;
  if (Name == "suppress" && getSyntax() == AttributeCommonInfo::AS_Bracket &&
      Scope == "clang")
    return 2;
  if (Name == "suppress" && getSyntax() == AttributeCommonInfo::AS_Bracket &&
      Scope == "neverc")
    return 3;
  if (Name == "suppress" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "clang")
    return 4;
  if (Name == "suppress" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "neverc")
    return 5;
  break;
}
case AT_SysVABI: {
  if (Name == "sysv_abi" && getSyntax() == AttributeCommonInfo::AS_GNU &&
      Scope == "")
    return 0;
  if (Name == "sysv_abi" && getSyntax() == AttributeCommonInfo::AS_Bracket &&
      Scope == "gnu")
    return 1;
  if (Name == "sysv_abi" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "gnu")
    return 2;
  break;
}
case AT_TLSModel: {
  if (Name == "tls_model" && getSyntax() == AttributeCommonInfo::AS_GNU &&
      Scope == "")
    return 0;
  if (Name == "tls_model" && getSyntax() == AttributeCommonInfo::AS_Bracket &&
      Scope == "gnu")
    return 1;
  if (Name == "tls_model" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "gnu")
    return 2;
  break;
}
case AT_Target: {
  if (Name == "target" && getSyntax() == AttributeCommonInfo::AS_GNU &&
      Scope == "")
    return 0;
  if (Name == "target" && getSyntax() == AttributeCommonInfo::AS_Bracket &&
      Scope == "gnu")
    return 1;
  if (Name == "target" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "gnu")
    return 2;
  break;
}
case AT_TargetClones: {
  if (Name == "target_clones" && getSyntax() == AttributeCommonInfo::AS_GNU &&
      Scope == "")
    return 0;
  if (Name == "target_clones" &&
      getSyntax() == AttributeCommonInfo::AS_Bracket && Scope == "gnu")
    return 1;
  if (Name == "target_clones" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "gnu")
    return 2;
  break;
}
case AT_TargetVersion: {
  if (Name == "target_version" && getSyntax() == AttributeCommonInfo::AS_GNU &&
      Scope == "")
    return 0;
  if (Name == "target_version" &&
      getSyntax() == AttributeCommonInfo::AS_Bracket && Scope == "gnu")
    return 1;
  if (Name == "target_version" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "gnu")
    return 2;
  break;
}
case AT_Thread: {
  if (Name == "thread" && getSyntax() == AttributeCommonInfo::AS_Declspec &&
      Scope == "")
    return 0;
  break;
}
case AT_TransparentUnion: {
  if (Name == "transparent_union" &&
      getSyntax() == AttributeCommonInfo::AS_GNU && Scope == "")
    return 0;
  if (Name == "transparent_union" &&
      getSyntax() == AttributeCommonInfo::AS_Bracket && Scope == "gnu")
    return 1;
  if (Name == "transparent_union" &&
      getSyntax() == AttributeCommonInfo::AS_C23 && Scope == "gnu")
    return 2;
  break;
}
case AT_TypeNonNull: {
  if (Name == "_Nonnull" && getSyntax() == AttributeCommonInfo::AS_Keyword &&
      Scope == "")
    return 0;
  break;
}
case AT_TypeNullUnspecified: {
  if (Name == "_Null_unspecified" &&
      getSyntax() == AttributeCommonInfo::AS_Keyword && Scope == "")
    return 0;
  break;
}
case AT_TypeNullable: {
  if (Name == "_Nullable" && getSyntax() == AttributeCommonInfo::AS_Keyword &&
      Scope == "")
    return 0;
  break;
}
case AT_TypeTagForDatatype: {
  if (Name == "type_tag_for_datatype" &&
      getSyntax() == AttributeCommonInfo::AS_GNU && Scope == "")
    return 0;
  if (Name == "type_tag_for_datatype" &&
      getSyntax() == AttributeCommonInfo::AS_Bracket && Scope == "clang")
    return 1;
  if (Name == "type_tag_for_datatype" &&
      getSyntax() == AttributeCommonInfo::AS_Bracket && Scope == "neverc")
    return 2;
  if (Name == "type_tag_for_datatype" &&
      getSyntax() == AttributeCommonInfo::AS_C23 && Scope == "clang")
    return 3;
  if (Name == "type_tag_for_datatype" &&
      getSyntax() == AttributeCommonInfo::AS_C23 && Scope == "neverc")
    return 4;
  break;
}
case AT_TypeVisibility: {
  if (Name == "type_visibility" && getSyntax() == AttributeCommonInfo::AS_GNU &&
      Scope == "")
    return 0;
  if (Name == "type_visibility" &&
      getSyntax() == AttributeCommonInfo::AS_Bracket && Scope == "clang")
    return 1;
  if (Name == "type_visibility" &&
      getSyntax() == AttributeCommonInfo::AS_Bracket && Scope == "neverc")
    return 2;
  if (Name == "type_visibility" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "clang")
    return 3;
  if (Name == "type_visibility" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "neverc")
    return 4;
  break;
}
case AT_UPtr: {
  if (Name == "__uptr" && getSyntax() == AttributeCommonInfo::AS_Keyword &&
      Scope == "")
    return 0;
  break;
}
case AT_Unavailable: {
  if (Name == "unavailable" && getSyntax() == AttributeCommonInfo::AS_GNU &&
      Scope == "")
    return 0;
  if (Name == "unavailable" && getSyntax() == AttributeCommonInfo::AS_Bracket &&
      Scope == "clang")
    return 1;
  if (Name == "unavailable" && getSyntax() == AttributeCommonInfo::AS_Bracket &&
      Scope == "neverc")
    return 2;
  if (Name == "unavailable" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "clang")
    return 3;
  if (Name == "unavailable" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "neverc")
    return 4;
  break;
}
case AT_Uninitialized: {
  if (Name == "uninitialized" && getSyntax() == AttributeCommonInfo::AS_GNU &&
      Scope == "")
    return 0;
  if (Name == "uninitialized" &&
      getSyntax() == AttributeCommonInfo::AS_Bracket && Scope == "clang")
    return 1;
  if (Name == "uninitialized" &&
      getSyntax() == AttributeCommonInfo::AS_Bracket && Scope == "neverc")
    return 2;
  break;
}
case AT_Unlikely: {
  if (Name == "unlikely" && getSyntax() == AttributeCommonInfo::AS_Bracket &&
      Scope == "")
    return 0;
  if (Name == "unlikely" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "neverc")
    return 1;
  break;
}
case AT_UnsafeBufferUsage: {
  if (Name == "unsafe_buffer_usage" &&
      getSyntax() == AttributeCommonInfo::AS_GNU && Scope == "")
    return 0;
  if (Name == "unsafe_buffer_usage" &&
      getSyntax() == AttributeCommonInfo::AS_Bracket && Scope == "clang")
    return 1;
  if (Name == "unsafe_buffer_usage" &&
      getSyntax() == AttributeCommonInfo::AS_Bracket && Scope == "neverc")
    return 2;
  if (Name == "unsafe_buffer_usage" &&
      getSyntax() == AttributeCommonInfo::AS_C23 && Scope == "clang")
    return 3;
  if (Name == "unsafe_buffer_usage" &&
      getSyntax() == AttributeCommonInfo::AS_C23 && Scope == "neverc")
    return 4;
  break;
}
case AT_Unused: {
  if (Name == "maybe_unused" &&
      getSyntax() == AttributeCommonInfo::AS_Bracket && Scope == "")
    return 0;
  if (Name == "unused" && getSyntax() == AttributeCommonInfo::AS_GNU &&
      Scope == "")
    return 1;
  if (Name == "unused" && getSyntax() == AttributeCommonInfo::AS_Bracket &&
      Scope == "gnu")
    return 2;
  if (Name == "unused" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "gnu")
    return 3;
  if (Name == "maybe_unused" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "")
    return 4;
  break;
}
case AT_UseHandle: {
  if (Name == "use_handle" && getSyntax() == AttributeCommonInfo::AS_GNU &&
      Scope == "")
    return 0;
  if (Name == "use_handle" && getSyntax() == AttributeCommonInfo::AS_Bracket &&
      Scope == "clang")
    return 1;
  if (Name == "use_handle" && getSyntax() == AttributeCommonInfo::AS_Bracket &&
      Scope == "neverc")
    return 2;
  if (Name == "use_handle" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "clang")
    return 3;
  if (Name == "use_handle" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "neverc")
    return 4;
  break;
}
case AT_Used: {
  if (Name == "used" && getSyntax() == AttributeCommonInfo::AS_GNU &&
      Scope == "")
    return 0;
  if (Name == "used" && getSyntax() == AttributeCommonInfo::AS_Bracket &&
      Scope == "gnu")
    return 1;
  if (Name == "used" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "gnu")
    return 2;
  break;
}
case AT_VectorCall: {
  if (Name == "vectorcall" && getSyntax() == AttributeCommonInfo::AS_GNU &&
      Scope == "")
    return 0;
  if (Name == "vectorcall" && getSyntax() == AttributeCommonInfo::AS_Bracket &&
      Scope == "clang")
    return 1;
  if (Name == "vectorcall" && getSyntax() == AttributeCommonInfo::AS_Bracket &&
      Scope == "neverc")
    return 2;
  if (Name == "vectorcall" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "clang")
    return 3;
  if (Name == "vectorcall" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "neverc")
    return 4;
  if (Name == "__vectorcall" &&
      getSyntax() == AttributeCommonInfo::AS_Keyword && Scope == "")
    return 5;
  if (Name == "_vectorcall" && getSyntax() == AttributeCommonInfo::AS_Keyword &&
      Scope == "")
    return 6;
  break;
}
case AT_VectorSize: {
  if (Name == "vector_size" && getSyntax() == AttributeCommonInfo::AS_GNU &&
      Scope == "")
    return 0;
  if (Name == "vector_size" && getSyntax() == AttributeCommonInfo::AS_Bracket &&
      Scope == "gnu")
    return 1;
  if (Name == "vector_size" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "gnu")
    return 2;
  break;
}
case AT_Visibility: {
  if (Name == "visibility" && getSyntax() == AttributeCommonInfo::AS_GNU &&
      Scope == "")
    return 0;
  if (Name == "visibility" && getSyntax() == AttributeCommonInfo::AS_Bracket &&
      Scope == "gnu")
    return 1;
  if (Name == "visibility" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "gnu")
    return 2;
  break;
}
case AT_Volatile: {
  if (Name == "volatile" && getSyntax() == AttributeCommonInfo::AS_GNU &&
      Scope == "")
    return 0;
  if (Name == "volatile" && getSyntax() == AttributeCommonInfo::AS_Bracket &&
      Scope == "gnu")
    return 1;
  if (Name == "volatile" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "gnu")
    return 2;
  if (Name == "volatile" && getSyntax() == AttributeCommonInfo::AS_Declspec &&
      Scope == "")
    return 3;
  break;
}
case AT_WarnUnused: {
  if (Name == "warn_unused" && getSyntax() == AttributeCommonInfo::AS_GNU &&
      Scope == "")
    return 0;
  if (Name == "warn_unused" && getSyntax() == AttributeCommonInfo::AS_Bracket &&
      Scope == "gnu")
    return 1;
  if (Name == "warn_unused" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "gnu")
    return 2;
  break;
}
case AT_WarnUnusedResult: {
  if (Name == "nodiscard" && getSyntax() == AttributeCommonInfo::AS_Bracket &&
      Scope == "")
    return 0;
  if (Name == "nodiscard" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "")
    return 1;
  if (Name == "warn_unused_result" &&
      getSyntax() == AttributeCommonInfo::AS_Bracket && Scope == "neverc")
    return 2;
  if (Name == "warn_unused_result" &&
      getSyntax() == AttributeCommonInfo::AS_GNU && Scope == "")
    return 3;
  if (Name == "warn_unused_result" &&
      getSyntax() == AttributeCommonInfo::AS_Bracket && Scope == "gnu")
    return 4;
  if (Name == "warn_unused_result" &&
      getSyntax() == AttributeCommonInfo::AS_C23 && Scope == "gnu")
    return 5;
  break;
}
case AT_Weak: {
  if (Name == "weak" && getSyntax() == AttributeCommonInfo::AS_GNU &&
      Scope == "")
    return 0;
  if (Name == "weak" && getSyntax() == AttributeCommonInfo::AS_Bracket &&
      Scope == "gnu")
    return 1;
  if (Name == "weak" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "gnu")
    return 2;
  break;
}
case AT_WeakImport: {
  if (Name == "weak_import" && getSyntax() == AttributeCommonInfo::AS_GNU &&
      Scope == "")
    return 0;
  if (Name == "weak_import" && getSyntax() == AttributeCommonInfo::AS_Bracket &&
      Scope == "clang")
    return 1;
  if (Name == "weak_import" && getSyntax() == AttributeCommonInfo::AS_Bracket &&
      Scope == "neverc")
    return 2;
  if (Name == "weak_import" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "clang")
    return 3;
  if (Name == "weak_import" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "neverc")
    return 4;
  break;
}
case AT_WeakRef: {
  if (Name == "weakref" && getSyntax() == AttributeCommonInfo::AS_GNU &&
      Scope == "")
    return 0;
  if (Name == "weakref" && getSyntax() == AttributeCommonInfo::AS_Bracket &&
      Scope == "gnu")
    return 1;
  if (Name == "weakref" && getSyntax() == AttributeCommonInfo::AS_C23 &&
      Scope == "gnu")
    return 2;
  break;
}
case AT_X86ForceAlignArgPointer: {
  if (Name == "force_align_arg_pointer" &&
      getSyntax() == AttributeCommonInfo::AS_GNU && Scope == "")
    return 0;
  if (Name == "force_align_arg_pointer" &&
      getSyntax() == AttributeCommonInfo::AS_Bracket && Scope == "gnu")
    return 1;
  if (Name == "force_align_arg_pointer" &&
      getSyntax() == AttributeCommonInfo::AS_C23 && Scope == "gnu")
    return 2;
  break;
}
case AT_ZeroCallUsedRegs: {
  if (Name == "zero_call_used_regs" &&
      getSyntax() == AttributeCommonInfo::AS_GNU && Scope == "")
    return 0;
  if (Name == "zero_call_used_regs" &&
      getSyntax() == AttributeCommonInfo::AS_Bracket && Scope == "gnu")
    return 1;
  if (Name == "zero_call_used_regs" &&
      getSyntax() == AttributeCommonInfo::AS_C23 && Scope == "gnu")
    return 2;
  break;
}
}
return 0;
