
#ifdef ATTR_VISITOR_DECLS_ONLY

bool TraverseAArch64SVEPcsAttr(AArch64SVEPcsAttr *A);
bool VisitAArch64SVEPcsAttr(AArch64SVEPcsAttr *A) { return true; }
bool TraverseAArch64VectorPcsAttr(AArch64VectorPcsAttr *A);
bool VisitAArch64VectorPcsAttr(AArch64VectorPcsAttr *A) { return true; }
bool TraverseAcquireHandleAttr(AcquireHandleAttr *A);
bool VisitAcquireHandleAttr(AcquireHandleAttr *A) { return true; }
bool TraverseAddressSpaceAttr(AddressSpaceAttr *A);
bool VisitAddressSpaceAttr(AddressSpaceAttr *A) { return true; }
bool TraverseAliasAttr(AliasAttr *A);
bool VisitAliasAttr(AliasAttr *A) { return true; }
bool TraverseAlignValueAttr(AlignValueAttr *A);
bool VisitAlignValueAttr(AlignValueAttr *A) { return true; }
bool TraverseAlignedAttr(AlignedAttr *A);
bool VisitAlignedAttr(AlignedAttr *A) { return true; }
bool TraverseAllocAlignAttr(AllocAlignAttr *A);
bool VisitAllocAlignAttr(AllocAlignAttr *A) { return true; }
bool TraverseAllocSizeAttr(AllocSizeAttr *A);
bool VisitAllocSizeAttr(AllocSizeAttr *A) { return true; }
bool TraverseAlwaysDestroyAttr(AlwaysDestroyAttr *A);
bool VisitAlwaysDestroyAttr(AlwaysDestroyAttr *A) { return true; }
bool TraverseAlwaysInlineAttr(AlwaysInlineAttr *A);
bool VisitAlwaysInlineAttr(AlwaysInlineAttr *A) { return true; }
bool TraverseAnalyzerNoReturnAttr(AnalyzerNoReturnAttr *A);
bool VisitAnalyzerNoReturnAttr(AnalyzerNoReturnAttr *A) { return true; }
bool TraverseAnnotateAttr(AnnotateAttr *A);
bool VisitAnnotateAttr(AnnotateAttr *A) { return true; }
bool TraverseAnnotateTypeAttr(AnnotateTypeAttr *A);
bool VisitAnnotateTypeAttr(AnnotateTypeAttr *A) { return true; }
bool TraverseAnyX86InterruptAttr(AnyX86InterruptAttr *A);
bool VisitAnyX86InterruptAttr(AnyX86InterruptAttr *A) { return true; }
bool TraverseAnyX86NoCallerSavedRegistersAttr(
    AnyX86NoCallerSavedRegistersAttr *A);
bool VisitAnyX86NoCallerSavedRegistersAttr(
    AnyX86NoCallerSavedRegistersAttr *A) {
  return true;
}
bool TraverseAnyX86NoCfCheckAttr(AnyX86NoCfCheckAttr *A);
bool VisitAnyX86NoCfCheckAttr(AnyX86NoCfCheckAttr *A) { return true; }
bool TraverseArgumentWithTypeTagAttr(ArgumentWithTypeTagAttr *A);
bool VisitArgumentWithTypeTagAttr(ArgumentWithTypeTagAttr *A) { return true; }
bool TraverseArmBuiltinAliasAttr(ArmBuiltinAliasAttr *A);
bool VisitArmBuiltinAliasAttr(ArmBuiltinAliasAttr *A) { return true; }
bool TraverseArmLocallyStreamingAttr(ArmLocallyStreamingAttr *A);
bool VisitArmLocallyStreamingAttr(ArmLocallyStreamingAttr *A) { return true; }
bool TraverseArmNewZAAttr(ArmNewZAAttr *A);
bool VisitArmNewZAAttr(ArmNewZAAttr *A) { return true; }
bool TraverseArmPreservesZAAttr(ArmPreservesZAAttr *A);
bool VisitArmPreservesZAAttr(ArmPreservesZAAttr *A) { return true; }
bool TraverseArmSharedZAAttr(ArmSharedZAAttr *A);
bool VisitArmSharedZAAttr(ArmSharedZAAttr *A) { return true; }
bool TraverseArmStreamingAttr(ArmStreamingAttr *A);
bool VisitArmStreamingAttr(ArmStreamingAttr *A) { return true; }
bool TraverseArmStreamingCompatibleAttr(ArmStreamingCompatibleAttr *A);
bool VisitArmStreamingCompatibleAttr(ArmStreamingCompatibleAttr *A) {
  return true;
}
bool TraverseArtificialAttr(ArtificialAttr *A);
bool VisitArtificialAttr(ArtificialAttr *A) { return true; }
bool TraverseAsmLabelAttr(AsmLabelAttr *A);
bool VisitAsmLabelAttr(AsmLabelAttr *A) { return true; }
bool TraverseAssumeAlignedAttr(AssumeAlignedAttr *A);
bool VisitAssumeAlignedAttr(AssumeAlignedAttr *A) { return true; }
bool TraverseAssumptionAttr(AssumptionAttr *A);
bool VisitAssumptionAttr(AssumptionAttr *A) { return true; }
bool TraverseAvailabilityAttr(AvailabilityAttr *A);
bool VisitAvailabilityAttr(AvailabilityAttr *A) { return true; }
bool TraverseAvailableOnlyInDefaultEvalMethodAttr(
    AvailableOnlyInDefaultEvalMethodAttr *A);
bool VisitAvailableOnlyInDefaultEvalMethodAttr(
    AvailableOnlyInDefaultEvalMethodAttr *A) {
  return true;
}
bool TraverseBTFDeclTagAttr(BTFDeclTagAttr *A);
bool VisitBTFDeclTagAttr(BTFDeclTagAttr *A) { return true; }
bool TraverseBTFTypeTagAttr(BTFTypeTagAttr *A);
bool VisitBTFTypeTagAttr(BTFTypeTagAttr *A) { return true; }
bool TraverseBuiltinAttr(BuiltinAttr *A);
bool VisitBuiltinAttr(BuiltinAttr *A) { return true; }
bool TraverseBuiltinAliasAttr(BuiltinAliasAttr *A);
bool VisitBuiltinAliasAttr(BuiltinAliasAttr *A) { return true; }
bool TraverseC11NoReturnAttr(C11NoReturnAttr *A);
bool VisitC11NoReturnAttr(C11NoReturnAttr *A) { return true; }
bool TraverseCDeclAttr(CDeclAttr *A);
bool VisitCDeclAttr(CDeclAttr *A) { return true; }
bool TraverseCFGuardAttr(CFGuardAttr *A);
bool VisitCFGuardAttr(CFGuardAttr *A) { return true; }
bool TraverseCPUDispatchAttr(CPUDispatchAttr *A);
bool VisitCPUDispatchAttr(CPUDispatchAttr *A) { return true; }
bool TraverseCPUSpecificAttr(CPUSpecificAttr *A);
bool VisitCPUSpecificAttr(CPUSpecificAttr *A) { return true; }
bool TraverseCallbackAttr(CallbackAttr *A);
bool VisitCallbackAttr(CallbackAttr *A) { return true; }
bool TraverseCarriesDependencyAttr(CarriesDependencyAttr *A);
bool VisitCarriesDependencyAttr(CarriesDependencyAttr *A) { return true; }
bool TraverseCleanupAttr(CleanupAttr *A);
bool VisitCleanupAttr(CleanupAttr *A) { return true; }
bool TraverseCodeAlignAttr(CodeAlignAttr *A);
bool VisitCodeAlignAttr(CodeAlignAttr *A) { return true; }
bool TraverseCodeSegAttr(CodeSegAttr *A);
bool VisitCodeSegAttr(CodeSegAttr *A) { return true; }
bool TraverseColdAttr(ColdAttr *A);
bool VisitColdAttr(ColdAttr *A) { return true; }
bool TraverseCommonAttr(CommonAttr *A);
bool VisitCommonAttr(CommonAttr *A) { return true; }
bool TraverseConstAttr(ConstAttr *A);
bool VisitConstAttr(ConstAttr *A) { return true; }
bool TraverseConstructorAttr(ConstructorAttr *A);
bool VisitConstructorAttr(ConstructorAttr *A) { return true; }
bool TraverseConvergentAttr(ConvergentAttr *A);
bool VisitConvergentAttr(ConvergentAttr *A) { return true; }
bool TraverseDLLExportAttr(DLLExportAttr *A);
bool VisitDLLExportAttr(DLLExportAttr *A) { return true; }
bool TraverseDLLImportAttr(DLLImportAttr *A);
bool VisitDLLImportAttr(DLLImportAttr *A) { return true; }
bool TraverseDeprecatedAttr(DeprecatedAttr *A);
bool VisitDeprecatedAttr(DeprecatedAttr *A) { return true; }
bool TraverseDestructorAttr(DestructorAttr *A);
bool VisitDestructorAttr(DestructorAttr *A) { return true; }
bool TraverseDiagnoseAsBuiltinAttr(DiagnoseAsBuiltinAttr *A);
bool VisitDiagnoseAsBuiltinAttr(DiagnoseAsBuiltinAttr *A) { return true; }
bool TraverseDiagnoseIfAttr(DiagnoseIfAttr *A);
bool VisitDiagnoseIfAttr(DiagnoseIfAttr *A) { return true; }
bool TraverseDisableTailCallsAttr(DisableTailCallsAttr *A);
bool VisitDisableTailCallsAttr(DisableTailCallsAttr *A) { return true; }
bool TraverseDisableTryStmtAttr(DisableTryStmtAttr *A);
bool VisitDisableTryStmtAttr(DisableTryStmtAttr *A) { return true; }
bool TraverseEnableIfAttr(EnableIfAttr *A);
bool VisitEnableIfAttr(EnableIfAttr *A) { return true; }
bool TraverseEnforceTCBAttr(EnforceTCBAttr *A);
bool VisitEnforceTCBAttr(EnforceTCBAttr *A) { return true; }
bool TraverseEnforceTCBLeafAttr(EnforceTCBLeafAttr *A);
bool VisitEnforceTCBLeafAttr(EnforceTCBLeafAttr *A) { return true; }
bool TraverseEnumExtensibilityAttr(EnumExtensibilityAttr *A);
bool VisitEnumExtensibilityAttr(EnumExtensibilityAttr *A) { return true; }
bool TraverseErrorAttr(ErrorAttr *A);
bool VisitErrorAttr(ErrorAttr *A) { return true; }
bool TraverseFallThroughAttr(FallThroughAttr *A);
bool VisitFallThroughAttr(FallThroughAttr *A) { return true; }
bool TraverseFastCallAttr(FastCallAttr *A);
bool VisitFastCallAttr(FastCallAttr *A) { return true; }
bool TraverseFlagEnumAttr(FlagEnumAttr *A);
bool VisitFlagEnumAttr(FlagEnumAttr *A) { return true; }
bool TraverseFlattenAttr(FlattenAttr *A);
bool VisitFlattenAttr(FlattenAttr *A) { return true; }
bool TraverseFormatAttr(FormatAttr *A);
bool VisitFormatAttr(FormatAttr *A) { return true; }
bool TraverseFormatArgAttr(FormatArgAttr *A);
bool VisitFormatArgAttr(FormatArgAttr *A) { return true; }
bool TraverseFunctionReturnThunksAttr(FunctionReturnThunksAttr *A);
bool VisitFunctionReturnThunksAttr(FunctionReturnThunksAttr *A) { return true; }
bool TraverseGNUInlineAttr(GNUInlineAttr *A);
bool VisitGNUInlineAttr(GNUInlineAttr *A) { return true; }
bool TraverseHotAttr(HotAttr *A);
bool VisitHotAttr(HotAttr *A) { return true; }
bool TraverseIFuncAttr(IFuncAttr *A);
bool VisitIFuncAttr(IFuncAttr *A) { return true; }
bool TraverseInitSegAttr(InitSegAttr *A);
bool VisitInitSegAttr(InitSegAttr *A) { return true; }
bool TraverseInternalLinkageAttr(InternalLinkageAttr *A);
bool VisitInternalLinkageAttr(InternalLinkageAttr *A) { return true; }
bool TraverseLTOVisibilityPublicAttr(LTOVisibilityPublicAttr *A);
bool VisitLTOVisibilityPublicAttr(LTOVisibilityPublicAttr *A) { return true; }
bool TraverseLeafAttr(LeafAttr *A);
bool VisitLeafAttr(LeafAttr *A) { return true; }
bool TraverseLikelyAttr(LikelyAttr *A);
bool VisitLikelyAttr(LikelyAttr *A) { return true; }
bool TraverseLoaderUninitializedAttr(LoaderUninitializedAttr *A);
bool VisitLoaderUninitializedAttr(LoaderUninitializedAttr *A) { return true; }
bool TraverseMSABIAttr(MSABIAttr *A);
bool VisitMSABIAttr(MSABIAttr *A) { return true; }
bool TraverseMSAllocatorAttr(MSAllocatorAttr *A);
bool VisitMSAllocatorAttr(MSAllocatorAttr *A) { return true; }
bool TraverseMSStructAttr(MSStructAttr *A);
bool VisitMSStructAttr(MSStructAttr *A) { return true; }
bool TraverseMaxFieldAlignmentAttr(MaxFieldAlignmentAttr *A);
bool VisitMaxFieldAlignmentAttr(MaxFieldAlignmentAttr *A) { return true; }
bool TraverseMayAliasAttr(MayAliasAttr *A);
bool VisitMayAliasAttr(MayAliasAttr *A) { return true; }
bool TraverseMaybeUndefAttr(MaybeUndefAttr *A);
bool VisitMaybeUndefAttr(MaybeUndefAttr *A) { return true; }
bool TraverseMinSizeAttr(MinSizeAttr *A);
bool VisitMinSizeAttr(MinSizeAttr *A) { return true; }
bool TraverseMinVectorWidthAttr(MinVectorWidthAttr *A);
bool VisitMinVectorWidthAttr(MinVectorWidthAttr *A) { return true; }
bool TraverseModeAttr(ModeAttr *A);
bool VisitModeAttr(ModeAttr *A) { return true; }
bool TraverseMustTailAttr(MustTailAttr *A);
bool VisitMustTailAttr(MustTailAttr *A) { return true; }
bool TraverseNakedAttr(NakedAttr *A);
bool VisitNakedAttr(NakedAttr *A) { return true; }
bool TraverseNoAliasAttr(NoAliasAttr *A);
bool VisitNoAliasAttr(NoAliasAttr *A) { return true; }
bool TraverseNoBuiltinAttr(NoBuiltinAttr *A);
bool VisitNoBuiltinAttr(NoBuiltinAttr *A) { return true; }
bool TraverseNoCommonAttr(NoCommonAttr *A);
bool VisitNoCommonAttr(NoCommonAttr *A) { return true; }
bool TraverseNoDebugAttr(NoDebugAttr *A);
bool VisitNoDebugAttr(NoDebugAttr *A) { return true; }
bool TraverseNoDerefAttr(NoDerefAttr *A);
bool VisitNoDerefAttr(NoDerefAttr *A) { return true; }
bool TraverseNoDestroyAttr(NoDestroyAttr *A);
bool VisitNoDestroyAttr(NoDestroyAttr *A) { return true; }
bool TraverseNoDuplicateAttr(NoDuplicateAttr *A);
bool VisitNoDuplicateAttr(NoDuplicateAttr *A) { return true; }
bool TraverseNoEscapeAttr(NoEscapeAttr *A);
bool VisitNoEscapeAttr(NoEscapeAttr *A) { return true; }
bool TraverseNoInlineAttr(NoInlineAttr *A);
bool VisitNoInlineAttr(NoInlineAttr *A) { return true; }
bool TraverseNoMergeAttr(NoMergeAttr *A);
bool VisitNoMergeAttr(NoMergeAttr *A) { return true; }
bool TraverseNoRandomizeLayoutAttr(NoRandomizeLayoutAttr *A);
bool VisitNoRandomizeLayoutAttr(NoRandomizeLayoutAttr *A) { return true; }
bool TraverseNoReturnAttr(NoReturnAttr *A);
bool VisitNoReturnAttr(NoReturnAttr *A) { return true; }
bool TraverseNoSpeculativeLoadHardeningAttr(NoSpeculativeLoadHardeningAttr *A);
bool VisitNoSpeculativeLoadHardeningAttr(NoSpeculativeLoadHardeningAttr *A) {
  return true;
}
bool TraverseNoSplitStackAttr(NoSplitStackAttr *A);
bool VisitNoSplitStackAttr(NoSplitStackAttr *A) { return true; }
bool TraverseNoStackProtectorAttr(NoStackProtectorAttr *A);
bool VisitNoStackProtectorAttr(NoStackProtectorAttr *A) { return true; }
bool TraverseNoThrowAttr(NoThrowAttr *A);
bool VisitNoThrowAttr(NoThrowAttr *A) { return true; }
bool TraverseNoUwtableAttr(NoUwtableAttr *A);
bool VisitNoUwtableAttr(NoUwtableAttr *A) { return true; }
bool TraverseNonNullAttr(NonNullAttr *A);
bool VisitNonNullAttr(NonNullAttr *A) { return true; }
bool TraverseNotTailCalledAttr(NotTailCalledAttr *A);
bool VisitNotTailCalledAttr(NotTailCalledAttr *A) { return true; }
bool TraverseOptimizeNoneAttr(OptimizeNoneAttr *A);
bool VisitOptimizeNoneAttr(OptimizeNoneAttr *A) { return true; }
bool TraverseOverloadableAttr(OverloadableAttr *A);
bool VisitOverloadableAttr(OverloadableAttr *A) { return true; }
bool TraverseOverrideAttr(OverrideAttr *A);
bool VisitOverrideAttr(OverrideAttr *A) { return true; }
bool TraversePackedAttr(PackedAttr *A);
bool VisitPackedAttr(PackedAttr *A) { return true; }
bool TraversePassObjectSizeAttr(PassObjectSizeAttr *A);
bool VisitPassObjectSizeAttr(PassObjectSizeAttr *A) { return true; }
bool TraversePatchableFunctionEntryAttr(PatchableFunctionEntryAttr *A);
bool VisitPatchableFunctionEntryAttr(PatchableFunctionEntryAttr *A) {
  return true;
}
bool TraversePragmaNeverCBSSSectionAttr(PragmaNeverCBSSSectionAttr *A);
bool VisitPragmaNeverCBSSSectionAttr(PragmaNeverCBSSSectionAttr *A) {
  return true;
}
bool TraversePragmaNeverCDataSectionAttr(PragmaNeverCDataSectionAttr *A);
bool VisitPragmaNeverCDataSectionAttr(PragmaNeverCDataSectionAttr *A) {
  return true;
}
bool TraversePragmaNeverCRelroSectionAttr(PragmaNeverCRelroSectionAttr *A);
bool VisitPragmaNeverCRelroSectionAttr(PragmaNeverCRelroSectionAttr *A) {
  return true;
}
bool TraversePragmaNeverCRodataSectionAttr(PragmaNeverCRodataSectionAttr *A);
bool VisitPragmaNeverCRodataSectionAttr(PragmaNeverCRodataSectionAttr *A) {
  return true;
}
bool TraversePragmaNeverCTextSectionAttr(PragmaNeverCTextSectionAttr *A);
bool VisitPragmaNeverCTextSectionAttr(PragmaNeverCTextSectionAttr *A) {
  return true;
}
bool TraversePreferredTypeAttr(PreferredTypeAttr *A);
bool VisitPreferredTypeAttr(PreferredTypeAttr *A) { return true; }
bool TraversePreserveAllAttr(PreserveAllAttr *A);
bool VisitPreserveAllAttr(PreserveAllAttr *A) { return true; }
bool TraversePreserveMostAttr(PreserveMostAttr *A);
bool VisitPreserveMostAttr(PreserveMostAttr *A) { return true; }
bool TraversePtr32Attr(Ptr32Attr *A);
bool VisitPtr32Attr(Ptr32Attr *A) { return true; }
bool TraversePtr64Attr(Ptr64Attr *A);
bool VisitPtr64Attr(Ptr64Attr *A) { return true; }
bool TraversePureAttr(PureAttr *A);
bool VisitPureAttr(PureAttr *A) { return true; }
bool TraverseRandomizeLayoutAttr(RandomizeLayoutAttr *A);
bool VisitRandomizeLayoutAttr(RandomizeLayoutAttr *A) { return true; }
bool TraverseReadOnlyPlacementAttr(ReadOnlyPlacementAttr *A);
bool VisitReadOnlyPlacementAttr(ReadOnlyPlacementAttr *A) { return true; }
bool TraverseRegCallAttr(RegCallAttr *A);
bool VisitRegCallAttr(RegCallAttr *A) { return true; }
bool TraverseReleaseHandleAttr(ReleaseHandleAttr *A);
bool VisitReleaseHandleAttr(ReleaseHandleAttr *A) { return true; }
bool TraverseRestrictAttr(RestrictAttr *A);
bool VisitRestrictAttr(RestrictAttr *A) { return true; }
bool TraverseRetainAttr(RetainAttr *A);
bool VisitRetainAttr(RetainAttr *A) { return true; }
bool TraverseReturnsNonNullAttr(ReturnsNonNullAttr *A);
bool VisitReturnsNonNullAttr(ReturnsNonNullAttr *A) { return true; }
bool TraverseReturnsTwiceAttr(ReturnsTwiceAttr *A);
bool VisitReturnsTwiceAttr(ReturnsTwiceAttr *A) { return true; }
bool TraverseSPtrAttr(SPtrAttr *A);
bool VisitSPtrAttr(SPtrAttr *A) { return true; }
bool TraverseSectionAttr(SectionAttr *A);
bool VisitSectionAttr(SectionAttr *A) { return true; }
bool TraverseSelectAnyAttr(SelectAnyAttr *A);
bool VisitSelectAnyAttr(SelectAnyAttr *A) { return true; }
bool TraverseSentinelAttr(SentinelAttr *A);
bool VisitSentinelAttr(SentinelAttr *A) { return true; }
bool TraverseSpeculativeLoadHardeningAttr(SpeculativeLoadHardeningAttr *A);
bool VisitSpeculativeLoadHardeningAttr(SpeculativeLoadHardeningAttr *A) {
  return true;
}
bool TraverseStandardNoReturnAttr(StandardNoReturnAttr *A);
bool VisitStandardNoReturnAttr(StandardNoReturnAttr *A) { return true; }
bool TraverseStdCallAttr(StdCallAttr *A);
bool VisitStdCallAttr(StdCallAttr *A) { return true; }
bool TraverseStrictFPAttr(StrictFPAttr *A);
bool VisitStrictFPAttr(StrictFPAttr *A) { return true; }
bool TraverseStrictGuardStackCheckAttr(StrictGuardStackCheckAttr *A);
bool VisitStrictGuardStackCheckAttr(StrictGuardStackCheckAttr *A) {
  return true;
}
bool TraverseSuppressAttr(SuppressAttr *A);
bool VisitSuppressAttr(SuppressAttr *A) { return true; }
bool TraverseSysVABIAttr(SysVABIAttr *A);
bool VisitSysVABIAttr(SysVABIAttr *A) { return true; }
bool TraverseTLSModelAttr(TLSModelAttr *A);
bool VisitTLSModelAttr(TLSModelAttr *A) { return true; }
bool TraverseTargetAttr(TargetAttr *A);
bool VisitTargetAttr(TargetAttr *A) { return true; }
bool TraverseTargetClonesAttr(TargetClonesAttr *A);
bool VisitTargetClonesAttr(TargetClonesAttr *A) { return true; }
bool TraverseTargetVersionAttr(TargetVersionAttr *A);
bool VisitTargetVersionAttr(TargetVersionAttr *A) { return true; }
bool TraverseThreadAttr(ThreadAttr *A);
bool VisitThreadAttr(ThreadAttr *A) { return true; }
bool TraverseTransparentUnionAttr(TransparentUnionAttr *A);
bool VisitTransparentUnionAttr(TransparentUnionAttr *A) { return true; }
bool TraverseTypeNonNullAttr(TypeNonNullAttr *A);
bool VisitTypeNonNullAttr(TypeNonNullAttr *A) { return true; }
bool TraverseTypeNullUnspecifiedAttr(TypeNullUnspecifiedAttr *A);
bool VisitTypeNullUnspecifiedAttr(TypeNullUnspecifiedAttr *A) { return true; }
bool TraverseTypeNullableAttr(TypeNullableAttr *A);
bool VisitTypeNullableAttr(TypeNullableAttr *A) { return true; }
bool TraverseTypeTagForDatatypeAttr(TypeTagForDatatypeAttr *A);
bool VisitTypeTagForDatatypeAttr(TypeTagForDatatypeAttr *A) { return true; }
bool TraverseTypeVisibilityAttr(TypeVisibilityAttr *A);
bool VisitTypeVisibilityAttr(TypeVisibilityAttr *A) { return true; }
bool TraverseUPtrAttr(UPtrAttr *A);
bool VisitUPtrAttr(UPtrAttr *A) { return true; }
bool TraverseUnavailableAttr(UnavailableAttr *A);
bool VisitUnavailableAttr(UnavailableAttr *A) { return true; }
bool TraverseUninitializedAttr(UninitializedAttr *A);
bool VisitUninitializedAttr(UninitializedAttr *A) { return true; }
bool TraverseUnlikelyAttr(UnlikelyAttr *A);
bool VisitUnlikelyAttr(UnlikelyAttr *A) { return true; }
bool TraverseUnsafeBufferUsageAttr(UnsafeBufferUsageAttr *A);
bool VisitUnsafeBufferUsageAttr(UnsafeBufferUsageAttr *A) { return true; }
bool TraverseUnusedAttr(UnusedAttr *A);
bool VisitUnusedAttr(UnusedAttr *A) { return true; }
bool TraverseUseHandleAttr(UseHandleAttr *A);
bool VisitUseHandleAttr(UseHandleAttr *A) { return true; }
bool TraverseUsedAttr(UsedAttr *A);
bool VisitUsedAttr(UsedAttr *A) { return true; }
bool TraverseVectorCallAttr(VectorCallAttr *A);
bool VisitVectorCallAttr(VectorCallAttr *A) { return true; }
bool TraverseVisibilityAttr(VisibilityAttr *A);
bool VisitVisibilityAttr(VisibilityAttr *A) { return true; }
bool TraverseVolatileAttr(VolatileAttr *A);
bool VisitVolatileAttr(VolatileAttr *A) { return true; }
bool TraverseWarnUnusedAttr(WarnUnusedAttr *A);
bool VisitWarnUnusedAttr(WarnUnusedAttr *A) { return true; }
bool TraverseWarnUnusedResultAttr(WarnUnusedResultAttr *A);
bool VisitWarnUnusedResultAttr(WarnUnusedResultAttr *A) { return true; }
bool TraverseWeakAttr(WeakAttr *A);
bool VisitWeakAttr(WeakAttr *A) { return true; }
bool TraverseWeakImportAttr(WeakImportAttr *A);
bool VisitWeakImportAttr(WeakImportAttr *A) { return true; }
bool TraverseWeakRefAttr(WeakRefAttr *A);
bool VisitWeakRefAttr(WeakRefAttr *A) { return true; }
bool TraverseX86ForceAlignArgPointerAttr(X86ForceAlignArgPointerAttr *A);
bool VisitX86ForceAlignArgPointerAttr(X86ForceAlignArgPointerAttr *A) {
  return true;
}
bool TraverseZeroCallUsedRegsAttr(ZeroCallUsedRegsAttr *A);
bool VisitZeroCallUsedRegsAttr(ZeroCallUsedRegsAttr *A) { return true; }

#else  // ATTR_VISITOR_DECLS_ONLY

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseAArch64SVEPcsAttr(AArch64SVEPcsAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitAArch64SVEPcsAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseAArch64VectorPcsAttr(
    AArch64VectorPcsAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitAArch64VectorPcsAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseAcquireHandleAttr(AcquireHandleAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitAcquireHandleAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseAddressSpaceAttr(AddressSpaceAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitAddressSpaceAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseAliasAttr(AliasAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitAliasAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseAlignValueAttr(AlignValueAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitAlignValueAttr(A))
    return false;
  if (!getDerived().TraverseStmt(A->getAlignment()))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseAlignedAttr(AlignedAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitAlignedAttr(A))
    return false;
  if (A->isAlignmentExpr()) {
    if (!getDerived().TraverseStmt(A->getAlignmentExpr()))
      return false;
  } else if (auto *TSI = A->getAlignmentType()) {
    if (!getDerived().TraverseTypeLoc(TSI->getTypeLoc()))
      return false;
  }
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseAllocAlignAttr(AllocAlignAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitAllocAlignAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseAllocSizeAttr(AllocSizeAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitAllocSizeAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseAlwaysDestroyAttr(AlwaysDestroyAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitAlwaysDestroyAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseAlwaysInlineAttr(AlwaysInlineAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitAlwaysInlineAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseAnalyzerNoReturnAttr(
    AnalyzerNoReturnAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitAnalyzerNoReturnAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseAnnotateAttr(AnnotateAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitAnnotateAttr(A))
    return false;
  {
    Expr **I = A->args_begin();
    Expr **E = A->args_end();
    for (; I != E; ++I) {
      if (!getDerived().TraverseStmt(*I))
        return false;
    }
  }
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseAnnotateTypeAttr(AnnotateTypeAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitAnnotateTypeAttr(A))
    return false;
  {
    Expr **I = A->args_begin();
    Expr **E = A->args_end();
    for (; I != E; ++I) {
      if (!getDerived().TraverseStmt(*I))
        return false;
    }
  }
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseAnyX86InterruptAttr(
    AnyX86InterruptAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitAnyX86InterruptAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseAnyX86NoCallerSavedRegistersAttr(
    AnyX86NoCallerSavedRegistersAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitAnyX86NoCallerSavedRegistersAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseAnyX86NoCfCheckAttr(
    AnyX86NoCfCheckAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitAnyX86NoCfCheckAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseArgumentWithTypeTagAttr(
    ArgumentWithTypeTagAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitArgumentWithTypeTagAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseArmBuiltinAliasAttr(
    ArmBuiltinAliasAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitArmBuiltinAliasAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseArmLocallyStreamingAttr(
    ArmLocallyStreamingAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitArmLocallyStreamingAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseArmNewZAAttr(ArmNewZAAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitArmNewZAAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseArmPreservesZAAttr(ArmPreservesZAAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitArmPreservesZAAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseArmSharedZAAttr(ArmSharedZAAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitArmSharedZAAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseArmStreamingAttr(ArmStreamingAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitArmStreamingAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseArmStreamingCompatibleAttr(
    ArmStreamingCompatibleAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitArmStreamingCompatibleAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseArtificialAttr(ArtificialAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitArtificialAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseAsmLabelAttr(AsmLabelAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitAsmLabelAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseAssumeAlignedAttr(AssumeAlignedAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitAssumeAlignedAttr(A))
    return false;
  if (!getDerived().TraverseStmt(A->getAlignment()))
    return false;
  if (!getDerived().TraverseStmt(A->getOffset()))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseAssumptionAttr(AssumptionAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitAssumptionAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseAvailabilityAttr(AvailabilityAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitAvailabilityAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseAvailableOnlyInDefaultEvalMethodAttr(
    AvailableOnlyInDefaultEvalMethodAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitAvailableOnlyInDefaultEvalMethodAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseBTFDeclTagAttr(BTFDeclTagAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitBTFDeclTagAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseBTFTypeTagAttr(BTFTypeTagAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitBTFTypeTagAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseBuiltinAttr(BuiltinAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitBuiltinAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseBuiltinAliasAttr(BuiltinAliasAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitBuiltinAliasAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseC11NoReturnAttr(C11NoReturnAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitC11NoReturnAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseCDeclAttr(CDeclAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitCDeclAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseCFGuardAttr(CFGuardAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitCFGuardAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseCPUDispatchAttr(CPUDispatchAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitCPUDispatchAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseCPUSpecificAttr(CPUSpecificAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitCPUSpecificAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseCallbackAttr(CallbackAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitCallbackAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseCarriesDependencyAttr(
    CarriesDependencyAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitCarriesDependencyAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseCleanupAttr(CleanupAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitCleanupAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseCodeAlignAttr(CodeAlignAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitCodeAlignAttr(A))
    return false;
  if (!getDerived().TraverseStmt(A->getAlignment()))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseCodeSegAttr(CodeSegAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitCodeSegAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseColdAttr(ColdAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitColdAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseCommonAttr(CommonAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitCommonAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseConstAttr(ConstAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitConstAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseConstructorAttr(ConstructorAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitConstructorAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseConvergentAttr(ConvergentAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitConvergentAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseDLLExportAttr(DLLExportAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitDLLExportAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseDLLImportAttr(DLLImportAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitDLLImportAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseDeprecatedAttr(DeprecatedAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitDeprecatedAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseDestructorAttr(DestructorAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitDestructorAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseDiagnoseAsBuiltinAttr(
    DiagnoseAsBuiltinAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitDiagnoseAsBuiltinAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseDiagnoseIfAttr(DiagnoseIfAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitDiagnoseIfAttr(A))
    return false;
  if (!getDerived().TraverseStmt(A->getCond()))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseDisableTailCallsAttr(
    DisableTailCallsAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitDisableTailCallsAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseDisableTryStmtAttr(DisableTryStmtAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitDisableTryStmtAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseEnableIfAttr(EnableIfAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitEnableIfAttr(A))
    return false;
  if (!getDerived().TraverseStmt(A->getCond()))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseEnforceTCBAttr(EnforceTCBAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitEnforceTCBAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseEnforceTCBLeafAttr(EnforceTCBLeafAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitEnforceTCBLeafAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseEnumExtensibilityAttr(
    EnumExtensibilityAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitEnumExtensibilityAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseErrorAttr(ErrorAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitErrorAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseFallThroughAttr(FallThroughAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitFallThroughAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseFastCallAttr(FastCallAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitFastCallAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseFlagEnumAttr(FlagEnumAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitFlagEnumAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseFlattenAttr(FlattenAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitFlattenAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseFormatAttr(FormatAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitFormatAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseFormatArgAttr(FormatArgAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitFormatArgAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseFunctionReturnThunksAttr(
    FunctionReturnThunksAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitFunctionReturnThunksAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseGNUInlineAttr(GNUInlineAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitGNUInlineAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseHotAttr(HotAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitHotAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseIFuncAttr(IFuncAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitIFuncAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseInitSegAttr(InitSegAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitInitSegAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseInternalLinkageAttr(
    InternalLinkageAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitInternalLinkageAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseLTOVisibilityPublicAttr(
    LTOVisibilityPublicAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitLTOVisibilityPublicAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseLeafAttr(LeafAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitLeafAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseLikelyAttr(LikelyAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitLikelyAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseLoaderUninitializedAttr(
    LoaderUninitializedAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitLoaderUninitializedAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseMSABIAttr(MSABIAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitMSABIAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseMSAllocatorAttr(MSAllocatorAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitMSAllocatorAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseMSStructAttr(MSStructAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitMSStructAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseMaxFieldAlignmentAttr(
    MaxFieldAlignmentAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitMaxFieldAlignmentAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseMayAliasAttr(MayAliasAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitMayAliasAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseMaybeUndefAttr(MaybeUndefAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitMaybeUndefAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseMinSizeAttr(MinSizeAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitMinSizeAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseMinVectorWidthAttr(MinVectorWidthAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitMinVectorWidthAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseModeAttr(ModeAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitModeAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseMustTailAttr(MustTailAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitMustTailAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseNakedAttr(NakedAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitNakedAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseNoAliasAttr(NoAliasAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitNoAliasAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseNoBuiltinAttr(NoBuiltinAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitNoBuiltinAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseNoCommonAttr(NoCommonAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitNoCommonAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseNoDebugAttr(NoDebugAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitNoDebugAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseNoDerefAttr(NoDerefAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitNoDerefAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseNoDestroyAttr(NoDestroyAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitNoDestroyAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseNoDuplicateAttr(NoDuplicateAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitNoDuplicateAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseNoEscapeAttr(NoEscapeAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitNoEscapeAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseNoInlineAttr(NoInlineAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitNoInlineAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseNoMergeAttr(NoMergeAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitNoMergeAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseNoRandomizeLayoutAttr(
    NoRandomizeLayoutAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitNoRandomizeLayoutAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseNoReturnAttr(NoReturnAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitNoReturnAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseNoSpeculativeLoadHardeningAttr(
    NoSpeculativeLoadHardeningAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitNoSpeculativeLoadHardeningAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseNoSplitStackAttr(NoSplitStackAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitNoSplitStackAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseNoStackProtectorAttr(
    NoStackProtectorAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitNoStackProtectorAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseNoThrowAttr(NoThrowAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitNoThrowAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseNoUwtableAttr(NoUwtableAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitNoUwtableAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseNonNullAttr(NonNullAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitNonNullAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseNotTailCalledAttr(NotTailCalledAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitNotTailCalledAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseOptimizeNoneAttr(OptimizeNoneAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitOptimizeNoneAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseOverloadableAttr(OverloadableAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitOverloadableAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseOverrideAttr(OverrideAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitOverrideAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraversePackedAttr(PackedAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitPackedAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraversePassObjectSizeAttr(PassObjectSizeAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitPassObjectSizeAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraversePatchableFunctionEntryAttr(
    PatchableFunctionEntryAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitPatchableFunctionEntryAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraversePragmaNeverCBSSSectionAttr(
    PragmaNeverCBSSSectionAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitPragmaNeverCBSSSectionAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraversePragmaNeverCDataSectionAttr(
    PragmaNeverCDataSectionAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitPragmaNeverCDataSectionAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraversePragmaNeverCRelroSectionAttr(
    PragmaNeverCRelroSectionAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitPragmaNeverCRelroSectionAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraversePragmaNeverCRodataSectionAttr(
    PragmaNeverCRodataSectionAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitPragmaNeverCRodataSectionAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraversePragmaNeverCTextSectionAttr(
    PragmaNeverCTextSectionAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitPragmaNeverCTextSectionAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraversePreferredTypeAttr(PreferredTypeAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitPreferredTypeAttr(A))
    return false;
  if (auto *TSI = A->getTypeLoc())
    if (!getDerived().TraverseTypeLoc(TSI->getTypeLoc()))
      return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraversePreserveAllAttr(PreserveAllAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitPreserveAllAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraversePreserveMostAttr(PreserveMostAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitPreserveMostAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraversePtr32Attr(Ptr32Attr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitPtr32Attr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraversePtr64Attr(Ptr64Attr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitPtr64Attr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraversePureAttr(PureAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitPureAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseRandomizeLayoutAttr(
    RandomizeLayoutAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitRandomizeLayoutAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseReadOnlyPlacementAttr(
    ReadOnlyPlacementAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitReadOnlyPlacementAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseRegCallAttr(RegCallAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitRegCallAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseReleaseHandleAttr(ReleaseHandleAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitReleaseHandleAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseRestrictAttr(RestrictAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitRestrictAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseRetainAttr(RetainAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitRetainAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseReturnsNonNullAttr(ReturnsNonNullAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitReturnsNonNullAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseReturnsTwiceAttr(ReturnsTwiceAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitReturnsTwiceAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseSPtrAttr(SPtrAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitSPtrAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseSectionAttr(SectionAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitSectionAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseSelectAnyAttr(SelectAnyAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitSelectAnyAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseSentinelAttr(SentinelAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitSentinelAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseSpeculativeLoadHardeningAttr(
    SpeculativeLoadHardeningAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitSpeculativeLoadHardeningAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseStandardNoReturnAttr(
    StandardNoReturnAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitStandardNoReturnAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseStdCallAttr(StdCallAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitStdCallAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseStrictFPAttr(StrictFPAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitStrictFPAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseStrictGuardStackCheckAttr(
    StrictGuardStackCheckAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitStrictGuardStackCheckAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseSuppressAttr(SuppressAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitSuppressAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseSysVABIAttr(SysVABIAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitSysVABIAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseTLSModelAttr(TLSModelAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitTLSModelAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseTargetAttr(TargetAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitTargetAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseTargetClonesAttr(TargetClonesAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitTargetClonesAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseTargetVersionAttr(TargetVersionAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitTargetVersionAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseThreadAttr(ThreadAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitThreadAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseTransparentUnionAttr(
    TransparentUnionAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitTransparentUnionAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseTypeNonNullAttr(TypeNonNullAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitTypeNonNullAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseTypeNullUnspecifiedAttr(
    TypeNullUnspecifiedAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitTypeNullUnspecifiedAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseTypeNullableAttr(TypeNullableAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitTypeNullableAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseTypeTagForDatatypeAttr(
    TypeTagForDatatypeAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitTypeTagForDatatypeAttr(A))
    return false;
  if (auto *TSI = A->getMatchingCTypeLoc())
    if (!getDerived().TraverseTypeLoc(TSI->getTypeLoc()))
      return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseTypeVisibilityAttr(TypeVisibilityAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitTypeVisibilityAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseUPtrAttr(UPtrAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitUPtrAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseUnavailableAttr(UnavailableAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitUnavailableAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseUninitializedAttr(UninitializedAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitUninitializedAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseUnlikelyAttr(UnlikelyAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitUnlikelyAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseUnsafeBufferUsageAttr(
    UnsafeBufferUsageAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitUnsafeBufferUsageAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseUnusedAttr(UnusedAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitUnusedAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseUseHandleAttr(UseHandleAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitUseHandleAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseUsedAttr(UsedAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitUsedAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseVectorCallAttr(VectorCallAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitVectorCallAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseVisibilityAttr(VisibilityAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitVisibilityAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseVolatileAttr(VolatileAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitVolatileAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseWarnUnusedAttr(WarnUnusedAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitWarnUnusedAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseWarnUnusedResultAttr(
    WarnUnusedResultAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitWarnUnusedResultAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseWeakAttr(WeakAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitWeakAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseWeakImportAttr(WeakImportAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitWeakImportAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseWeakRefAttr(WeakRefAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitWeakRefAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseX86ForceAlignArgPointerAttr(
    X86ForceAlignArgPointerAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitX86ForceAlignArgPointerAttr(A))
    return false;
  return true;
}

template <typename Derived>
bool VISITORCLASS<Derived>::TraverseZeroCallUsedRegsAttr(
    ZeroCallUsedRegsAttr *A) {
  if (!getDerived().VisitAttr(A))
    return false;
  if (!getDerived().VisitZeroCallUsedRegsAttr(A))
    return false;
  return true;
}

template <typename Derived> bool VISITORCLASS<Derived>::TraverseAttr(Attr *A) {
  if (!A)
    return true;

  switch (A->getKind()) {
  case attr::AArch64SVEPcs:
    return getDerived().TraverseAArch64SVEPcsAttr(cast<AArch64SVEPcsAttr>(A));
  case attr::AArch64VectorPcs:
    return getDerived().TraverseAArch64VectorPcsAttr(
        cast<AArch64VectorPcsAttr>(A));
  case attr::AcquireHandle:
    return getDerived().TraverseAcquireHandleAttr(cast<AcquireHandleAttr>(A));
  case attr::AddressSpace:
    return getDerived().TraverseAddressSpaceAttr(cast<AddressSpaceAttr>(A));
  case attr::Alias:
    return getDerived().TraverseAliasAttr(cast<AliasAttr>(A));
  case attr::AlignValue:
    return getDerived().TraverseAlignValueAttr(cast<AlignValueAttr>(A));
  case attr::Aligned:
    return getDerived().TraverseAlignedAttr(cast<AlignedAttr>(A));
  case attr::AllocAlign:
    return getDerived().TraverseAllocAlignAttr(cast<AllocAlignAttr>(A));
  case attr::AllocSize:
    return getDerived().TraverseAllocSizeAttr(cast<AllocSizeAttr>(A));
  case attr::AlwaysDestroy:
    return getDerived().TraverseAlwaysDestroyAttr(cast<AlwaysDestroyAttr>(A));
  case attr::AlwaysInline:
    return getDerived().TraverseAlwaysInlineAttr(cast<AlwaysInlineAttr>(A));
  case attr::AnalyzerNoReturn:
    return getDerived().TraverseAnalyzerNoReturnAttr(
        cast<AnalyzerNoReturnAttr>(A));
  case attr::Annotate:
    return getDerived().TraverseAnnotateAttr(cast<AnnotateAttr>(A));
  case attr::AnnotateType:
    return getDerived().TraverseAnnotateTypeAttr(cast<AnnotateTypeAttr>(A));
  case attr::AnyX86Interrupt:
    return getDerived().TraverseAnyX86InterruptAttr(
        cast<AnyX86InterruptAttr>(A));
  case attr::AnyX86NoCallerSavedRegisters:
    return getDerived().TraverseAnyX86NoCallerSavedRegistersAttr(
        cast<AnyX86NoCallerSavedRegistersAttr>(A));
  case attr::AnyX86NoCfCheck:
    return getDerived().TraverseAnyX86NoCfCheckAttr(
        cast<AnyX86NoCfCheckAttr>(A));
  case attr::ArgumentWithTypeTag:
    return getDerived().TraverseArgumentWithTypeTagAttr(
        cast<ArgumentWithTypeTagAttr>(A));
  case attr::ArmBuiltinAlias:
    return getDerived().TraverseArmBuiltinAliasAttr(
        cast<ArmBuiltinAliasAttr>(A));
  case attr::ArmLocallyStreaming:
    return getDerived().TraverseArmLocallyStreamingAttr(
        cast<ArmLocallyStreamingAttr>(A));
  case attr::ArmNewZA:
    return getDerived().TraverseArmNewZAAttr(cast<ArmNewZAAttr>(A));
  case attr::ArmPreservesZA:
    return getDerived().TraverseArmPreservesZAAttr(cast<ArmPreservesZAAttr>(A));
  case attr::ArmSharedZA:
    return getDerived().TraverseArmSharedZAAttr(cast<ArmSharedZAAttr>(A));
  case attr::ArmStreaming:
    return getDerived().TraverseArmStreamingAttr(cast<ArmStreamingAttr>(A));
  case attr::ArmStreamingCompatible:
    return getDerived().TraverseArmStreamingCompatibleAttr(
        cast<ArmStreamingCompatibleAttr>(A));
  case attr::Artificial:
    return getDerived().TraverseArtificialAttr(cast<ArtificialAttr>(A));
  case attr::AsmLabel:
    return getDerived().TraverseAsmLabelAttr(cast<AsmLabelAttr>(A));
  case attr::AssumeAligned:
    return getDerived().TraverseAssumeAlignedAttr(cast<AssumeAlignedAttr>(A));
  case attr::Assumption:
    return getDerived().TraverseAssumptionAttr(cast<AssumptionAttr>(A));
  case attr::Availability:
    return getDerived().TraverseAvailabilityAttr(cast<AvailabilityAttr>(A));
  case attr::AvailableOnlyInDefaultEvalMethod:
    return getDerived().TraverseAvailableOnlyInDefaultEvalMethodAttr(
        cast<AvailableOnlyInDefaultEvalMethodAttr>(A));
  case attr::BTFDeclTag:
    return getDerived().TraverseBTFDeclTagAttr(cast<BTFDeclTagAttr>(A));
  case attr::BTFTypeTag:
    return getDerived().TraverseBTFTypeTagAttr(cast<BTFTypeTagAttr>(A));
  case attr::Builtin:
    return getDerived().TraverseBuiltinAttr(cast<BuiltinAttr>(A));
  case attr::BuiltinAlias:
    return getDerived().TraverseBuiltinAliasAttr(cast<BuiltinAliasAttr>(A));
  case attr::C11NoReturn:
    return getDerived().TraverseC11NoReturnAttr(cast<C11NoReturnAttr>(A));
  case attr::CDecl:
    return getDerived().TraverseCDeclAttr(cast<CDeclAttr>(A));
  case attr::CFGuard:
    return getDerived().TraverseCFGuardAttr(cast<CFGuardAttr>(A));
  case attr::CPUDispatch:
    return getDerived().TraverseCPUDispatchAttr(cast<CPUDispatchAttr>(A));
  case attr::CPUSpecific:
    return getDerived().TraverseCPUSpecificAttr(cast<CPUSpecificAttr>(A));
  case attr::Callback:
    return getDerived().TraverseCallbackAttr(cast<CallbackAttr>(A));
  case attr::CarriesDependency:
    return getDerived().TraverseCarriesDependencyAttr(
        cast<CarriesDependencyAttr>(A));
  case attr::Cleanup:
    return getDerived().TraverseCleanupAttr(cast<CleanupAttr>(A));
  case attr::CodeAlign:
    return getDerived().TraverseCodeAlignAttr(cast<CodeAlignAttr>(A));
  case attr::CodeSeg:
    return getDerived().TraverseCodeSegAttr(cast<CodeSegAttr>(A));
  case attr::Cold:
    return getDerived().TraverseColdAttr(cast<ColdAttr>(A));
  case attr::Common:
    return getDerived().TraverseCommonAttr(cast<CommonAttr>(A));
  case attr::Const:
    return getDerived().TraverseConstAttr(cast<ConstAttr>(A));
  case attr::Constructor:
    return getDerived().TraverseConstructorAttr(cast<ConstructorAttr>(A));
  case attr::Convergent:
    return getDerived().TraverseConvergentAttr(cast<ConvergentAttr>(A));
  case attr::DLLExport:
    return getDerived().TraverseDLLExportAttr(cast<DLLExportAttr>(A));
  case attr::DLLImport:
    return getDerived().TraverseDLLImportAttr(cast<DLLImportAttr>(A));
  case attr::Deprecated:
    return getDerived().TraverseDeprecatedAttr(cast<DeprecatedAttr>(A));
  case attr::Destructor:
    return getDerived().TraverseDestructorAttr(cast<DestructorAttr>(A));
  case attr::DiagnoseAsBuiltin:
    return getDerived().TraverseDiagnoseAsBuiltinAttr(
        cast<DiagnoseAsBuiltinAttr>(A));
  case attr::DiagnoseIf:
    return getDerived().TraverseDiagnoseIfAttr(cast<DiagnoseIfAttr>(A));
  case attr::DisableTailCalls:
    return getDerived().TraverseDisableTailCallsAttr(
        cast<DisableTailCallsAttr>(A));
  case attr::DisableTryStmt:
    return getDerived().TraverseDisableTryStmtAttr(cast<DisableTryStmtAttr>(A));
  case attr::EnableIf:
    return getDerived().TraverseEnableIfAttr(cast<EnableIfAttr>(A));
  case attr::EnforceTCB:
    return getDerived().TraverseEnforceTCBAttr(cast<EnforceTCBAttr>(A));
  case attr::EnforceTCBLeaf:
    return getDerived().TraverseEnforceTCBLeafAttr(cast<EnforceTCBLeafAttr>(A));
  case attr::EnumExtensibility:
    return getDerived().TraverseEnumExtensibilityAttr(
        cast<EnumExtensibilityAttr>(A));
  case attr::Error:
    return getDerived().TraverseErrorAttr(cast<ErrorAttr>(A));
  case attr::FallThrough:
    return getDerived().TraverseFallThroughAttr(cast<FallThroughAttr>(A));
  case attr::FastCall:
    return getDerived().TraverseFastCallAttr(cast<FastCallAttr>(A));
  case attr::FlagEnum:
    return getDerived().TraverseFlagEnumAttr(cast<FlagEnumAttr>(A));
  case attr::Flatten:
    return getDerived().TraverseFlattenAttr(cast<FlattenAttr>(A));
  case attr::Format:
    return getDerived().TraverseFormatAttr(cast<FormatAttr>(A));
  case attr::FormatArg:
    return getDerived().TraverseFormatArgAttr(cast<FormatArgAttr>(A));
  case attr::FunctionReturnThunks:
    return getDerived().TraverseFunctionReturnThunksAttr(
        cast<FunctionReturnThunksAttr>(A));
  case attr::GNUInline:
    return getDerived().TraverseGNUInlineAttr(cast<GNUInlineAttr>(A));
  case attr::Hot:
    return getDerived().TraverseHotAttr(cast<HotAttr>(A));
  case attr::IFunc:
    return getDerived().TraverseIFuncAttr(cast<IFuncAttr>(A));
  case attr::InitSeg:
    return getDerived().TraverseInitSegAttr(cast<InitSegAttr>(A));
  case attr::InternalLinkage:
    return getDerived().TraverseInternalLinkageAttr(
        cast<InternalLinkageAttr>(A));
  case attr::LTOVisibilityPublic:
    return getDerived().TraverseLTOVisibilityPublicAttr(
        cast<LTOVisibilityPublicAttr>(A));
  case attr::Leaf:
    return getDerived().TraverseLeafAttr(cast<LeafAttr>(A));
  case attr::Likely:
    return getDerived().TraverseLikelyAttr(cast<LikelyAttr>(A));
  case attr::LoaderUninitialized:
    return getDerived().TraverseLoaderUninitializedAttr(
        cast<LoaderUninitializedAttr>(A));
  case attr::MSABI:
    return getDerived().TraverseMSABIAttr(cast<MSABIAttr>(A));
  case attr::MSAllocator:
    return getDerived().TraverseMSAllocatorAttr(cast<MSAllocatorAttr>(A));
  case attr::MSStruct:
    return getDerived().TraverseMSStructAttr(cast<MSStructAttr>(A));
  case attr::MaxFieldAlignment:
    return getDerived().TraverseMaxFieldAlignmentAttr(
        cast<MaxFieldAlignmentAttr>(A));
  case attr::MayAlias:
    return getDerived().TraverseMayAliasAttr(cast<MayAliasAttr>(A));
  case attr::MaybeUndef:
    return getDerived().TraverseMaybeUndefAttr(cast<MaybeUndefAttr>(A));
  case attr::MinSize:
    return getDerived().TraverseMinSizeAttr(cast<MinSizeAttr>(A));
  case attr::MinVectorWidth:
    return getDerived().TraverseMinVectorWidthAttr(cast<MinVectorWidthAttr>(A));
  case attr::Mode:
    return getDerived().TraverseModeAttr(cast<ModeAttr>(A));
  case attr::MustTail:
    return getDerived().TraverseMustTailAttr(cast<MustTailAttr>(A));
  case attr::Naked:
    return getDerived().TraverseNakedAttr(cast<NakedAttr>(A));
  case attr::NoAlias:
    return getDerived().TraverseNoAliasAttr(cast<NoAliasAttr>(A));
  case attr::NoBuiltin:
    return getDerived().TraverseNoBuiltinAttr(cast<NoBuiltinAttr>(A));
  case attr::NoCommon:
    return getDerived().TraverseNoCommonAttr(cast<NoCommonAttr>(A));
  case attr::NoDebug:
    return getDerived().TraverseNoDebugAttr(cast<NoDebugAttr>(A));
  case attr::NoDeref:
    return getDerived().TraverseNoDerefAttr(cast<NoDerefAttr>(A));
  case attr::NoDestroy:
    return getDerived().TraverseNoDestroyAttr(cast<NoDestroyAttr>(A));
  case attr::NoDuplicate:
    return getDerived().TraverseNoDuplicateAttr(cast<NoDuplicateAttr>(A));
  case attr::NoEscape:
    return getDerived().TraverseNoEscapeAttr(cast<NoEscapeAttr>(A));
  case attr::NoInline:
    return getDerived().TraverseNoInlineAttr(cast<NoInlineAttr>(A));
  case attr::NoMerge:
    return getDerived().TraverseNoMergeAttr(cast<NoMergeAttr>(A));
  case attr::NoRandomizeLayout:
    return getDerived().TraverseNoRandomizeLayoutAttr(
        cast<NoRandomizeLayoutAttr>(A));
  case attr::NoReturn:
    return getDerived().TraverseNoReturnAttr(cast<NoReturnAttr>(A));
  case attr::NoSpeculativeLoadHardening:
    return getDerived().TraverseNoSpeculativeLoadHardeningAttr(
        cast<NoSpeculativeLoadHardeningAttr>(A));
  case attr::NoSplitStack:
    return getDerived().TraverseNoSplitStackAttr(cast<NoSplitStackAttr>(A));
  case attr::NoStackProtector:
    return getDerived().TraverseNoStackProtectorAttr(
        cast<NoStackProtectorAttr>(A));
  case attr::NoThrow:
    return getDerived().TraverseNoThrowAttr(cast<NoThrowAttr>(A));
  case attr::NoUwtable:
    return getDerived().TraverseNoUwtableAttr(cast<NoUwtableAttr>(A));
  case attr::NonNull:
    return getDerived().TraverseNonNullAttr(cast<NonNullAttr>(A));
  case attr::NotTailCalled:
    return getDerived().TraverseNotTailCalledAttr(cast<NotTailCalledAttr>(A));
  case attr::OptimizeNone:
    return getDerived().TraverseOptimizeNoneAttr(cast<OptimizeNoneAttr>(A));
  case attr::Overloadable:
    return getDerived().TraverseOverloadableAttr(cast<OverloadableAttr>(A));
  case attr::Override:
    return getDerived().TraverseOverrideAttr(cast<OverrideAttr>(A));
  case attr::Packed:
    return getDerived().TraversePackedAttr(cast<PackedAttr>(A));
  case attr::PassObjectSize:
    return getDerived().TraversePassObjectSizeAttr(cast<PassObjectSizeAttr>(A));
  case attr::PatchableFunctionEntry:
    return getDerived().TraversePatchableFunctionEntryAttr(
        cast<PatchableFunctionEntryAttr>(A));
  case attr::PragmaNeverCBSSSection:
    return getDerived().TraversePragmaNeverCBSSSectionAttr(
        cast<PragmaNeverCBSSSectionAttr>(A));
  case attr::PragmaNeverCDataSection:
    return getDerived().TraversePragmaNeverCDataSectionAttr(
        cast<PragmaNeverCDataSectionAttr>(A));
  case attr::PragmaNeverCRelroSection:
    return getDerived().TraversePragmaNeverCRelroSectionAttr(
        cast<PragmaNeverCRelroSectionAttr>(A));
  case attr::PragmaNeverCRodataSection:
    return getDerived().TraversePragmaNeverCRodataSectionAttr(
        cast<PragmaNeverCRodataSectionAttr>(A));
  case attr::PragmaNeverCTextSection:
    return getDerived().TraversePragmaNeverCTextSectionAttr(
        cast<PragmaNeverCTextSectionAttr>(A));
  case attr::PreferredType:
    return getDerived().TraversePreferredTypeAttr(cast<PreferredTypeAttr>(A));
  case attr::PreserveAll:
    return getDerived().TraversePreserveAllAttr(cast<PreserveAllAttr>(A));
  case attr::PreserveMost:
    return getDerived().TraversePreserveMostAttr(cast<PreserveMostAttr>(A));
  case attr::Ptr32:
    return getDerived().TraversePtr32Attr(cast<Ptr32Attr>(A));
  case attr::Ptr64:
    return getDerived().TraversePtr64Attr(cast<Ptr64Attr>(A));
  case attr::Pure:
    return getDerived().TraversePureAttr(cast<PureAttr>(A));
  case attr::RandomizeLayout:
    return getDerived().TraverseRandomizeLayoutAttr(
        cast<RandomizeLayoutAttr>(A));
  case attr::ReadOnlyPlacement:
    return getDerived().TraverseReadOnlyPlacementAttr(
        cast<ReadOnlyPlacementAttr>(A));
  case attr::RegCall:
    return getDerived().TraverseRegCallAttr(cast<RegCallAttr>(A));
  case attr::ReleaseHandle:
    return getDerived().TraverseReleaseHandleAttr(cast<ReleaseHandleAttr>(A));
  case attr::Restrict:
    return getDerived().TraverseRestrictAttr(cast<RestrictAttr>(A));
  case attr::Retain:
    return getDerived().TraverseRetainAttr(cast<RetainAttr>(A));
  case attr::ReturnsNonNull:
    return getDerived().TraverseReturnsNonNullAttr(cast<ReturnsNonNullAttr>(A));
  case attr::ReturnsTwice:
    return getDerived().TraverseReturnsTwiceAttr(cast<ReturnsTwiceAttr>(A));
  case attr::SPtr:
    return getDerived().TraverseSPtrAttr(cast<SPtrAttr>(A));
  case attr::Section:
    return getDerived().TraverseSectionAttr(cast<SectionAttr>(A));
  case attr::SelectAny:
    return getDerived().TraverseSelectAnyAttr(cast<SelectAnyAttr>(A));
  case attr::Sentinel:
    return getDerived().TraverseSentinelAttr(cast<SentinelAttr>(A));
  case attr::SpeculativeLoadHardening:
    return getDerived().TraverseSpeculativeLoadHardeningAttr(
        cast<SpeculativeLoadHardeningAttr>(A));
  case attr::StandardNoReturn:
    return getDerived().TraverseStandardNoReturnAttr(
        cast<StandardNoReturnAttr>(A));
  case attr::StdCall:
    return getDerived().TraverseStdCallAttr(cast<StdCallAttr>(A));
  case attr::StrictFP:
    return getDerived().TraverseStrictFPAttr(cast<StrictFPAttr>(A));
  case attr::StrictGuardStackCheck:
    return getDerived().TraverseStrictGuardStackCheckAttr(
        cast<StrictGuardStackCheckAttr>(A));
  case attr::Suppress:
    return getDerived().TraverseSuppressAttr(cast<SuppressAttr>(A));
  case attr::SysVABI:
    return getDerived().TraverseSysVABIAttr(cast<SysVABIAttr>(A));
  case attr::TLSModel:
    return getDerived().TraverseTLSModelAttr(cast<TLSModelAttr>(A));
  case attr::Target:
    return getDerived().TraverseTargetAttr(cast<TargetAttr>(A));
  case attr::TargetClones:
    return getDerived().TraverseTargetClonesAttr(cast<TargetClonesAttr>(A));
  case attr::TargetVersion:
    return getDerived().TraverseTargetVersionAttr(cast<TargetVersionAttr>(A));
  case attr::Thread:
    return getDerived().TraverseThreadAttr(cast<ThreadAttr>(A));
  case attr::TransparentUnion:
    return getDerived().TraverseTransparentUnionAttr(
        cast<TransparentUnionAttr>(A));
  case attr::TypeNonNull:
    return getDerived().TraverseTypeNonNullAttr(cast<TypeNonNullAttr>(A));
  case attr::TypeNullUnspecified:
    return getDerived().TraverseTypeNullUnspecifiedAttr(
        cast<TypeNullUnspecifiedAttr>(A));
  case attr::TypeNullable:
    return getDerived().TraverseTypeNullableAttr(cast<TypeNullableAttr>(A));
  case attr::TypeTagForDatatype:
    return getDerived().TraverseTypeTagForDatatypeAttr(
        cast<TypeTagForDatatypeAttr>(A));
  case attr::TypeVisibility:
    return getDerived().TraverseTypeVisibilityAttr(cast<TypeVisibilityAttr>(A));
  case attr::UPtr:
    return getDerived().TraverseUPtrAttr(cast<UPtrAttr>(A));
  case attr::Unavailable:
    return getDerived().TraverseUnavailableAttr(cast<UnavailableAttr>(A));
  case attr::Uninitialized:
    return getDerived().TraverseUninitializedAttr(cast<UninitializedAttr>(A));
  case attr::Unlikely:
    return getDerived().TraverseUnlikelyAttr(cast<UnlikelyAttr>(A));
  case attr::UnsafeBufferUsage:
    return getDerived().TraverseUnsafeBufferUsageAttr(
        cast<UnsafeBufferUsageAttr>(A));
  case attr::Unused:
    return getDerived().TraverseUnusedAttr(cast<UnusedAttr>(A));
  case attr::UseHandle:
    return getDerived().TraverseUseHandleAttr(cast<UseHandleAttr>(A));
  case attr::Used:
    return getDerived().TraverseUsedAttr(cast<UsedAttr>(A));
  case attr::VectorCall:
    return getDerived().TraverseVectorCallAttr(cast<VectorCallAttr>(A));
  case attr::Visibility:
    return getDerived().TraverseVisibilityAttr(cast<VisibilityAttr>(A));
  case attr::Volatile:
    return getDerived().TraverseVolatileAttr(cast<VolatileAttr>(A));
  case attr::WarnUnused:
    return getDerived().TraverseWarnUnusedAttr(cast<WarnUnusedAttr>(A));
  case attr::WarnUnusedResult:
    return getDerived().TraverseWarnUnusedResultAttr(
        cast<WarnUnusedResultAttr>(A));
  case attr::Weak:
    return getDerived().TraverseWeakAttr(cast<WeakAttr>(A));
  case attr::WeakImport:
    return getDerived().TraverseWeakImportAttr(cast<WeakImportAttr>(A));
  case attr::WeakRef:
    return getDerived().TraverseWeakRefAttr(cast<WeakRefAttr>(A));
  case attr::X86ForceAlignArgPointer:
    return getDerived().TraverseX86ForceAlignArgPointerAttr(
        cast<X86ForceAlignArgPointerAttr>(A));
  case attr::ZeroCallUsedRegs:
    return getDerived().TraverseZeroCallUsedRegsAttr(
        cast<ZeroCallUsedRegsAttr>(A));
  }
  llvm_unreachable("bad attribute kind");
}
#endif // ATTR_VISITOR_DECLS_ONLY
