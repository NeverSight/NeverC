#include "PluginObjectMergeTestSupport.h"

namespace {

TEST(BuiltinLLVMObjectWriterPolicyABITest,
     RejectsOldUnknownAndIllegalFlagsBeforeTargetDispatch) {
  NevercObjectAPI Object{};
  NevercMutableBinaryAPI Binary{};
  Binary.Write = ignoreBinaryWrite;
  NevercObjectWriteRequest Request{};
  Request.Object = &Object;
  Request.Binary = &Binary;

  const auto Invoke = [&](uint16_t Minor, uint64_t Flags) {
    Request.Header = {sizeof(Request), NEVERC_OBJECT_FORMAT_API_MAJOR, Minor,
                      Flags};
    return writeBuiltinLLVMObject(nullptr, &Request);
  };

  EXPECT_EQ(Invoke(UINT16_C(0), 0).Code, NEVERC_STATUS_CAPABILITY_UNAVAILABLE);
  EXPECT_EQ(Invoke(UINT16_C(0), NEVERC_OBJECT_WRITE_CANONICAL_ELF_TABLES).Code,
            NEVERC_STATUS_ABI_MISMATCH);
  EXPECT_EQ(Invoke(NEVERC_OBJECT_FORMAT_API_MINOR, UINT64_C(1) << 63).Code,
            NEVERC_STATUS_ABI_MISMATCH);
  EXPECT_EQ(Invoke(NEVERC_OBJECT_FORMAT_API_MINOR,
                   NEVERC_OBJECT_WRITE_ANDROID_KERNEL_RELEASE)
                .Code,
            NEVERC_STATUS_ABI_MISMATCH);
  EXPECT_EQ(Invoke(NEVERC_OBJECT_FORMAT_API_MINOR,
                   NEVERC_OBJECT_WRITE_DROP_DEBUG_INFO)
                .Code,
            NEVERC_STATUS_ABI_MISMATCH);

  for (uint64_t LegalFlags : {NEVERC_OBJECT_WRITE_CANONICAL_ELF_TABLES,
                              NEVERC_OBJECT_WRITE_CANONICAL_ELF_TABLES |
                                  NEVERC_OBJECT_WRITE_DROP_DEBUG_INFO,
                              NEVERC_OBJECT_WRITE_CANONICAL_ELF_TABLES |
                                  NEVERC_OBJECT_WRITE_ANDROID_KERNEL_RELEASE,
                              NEVERC_OBJECT_WRITE_CANONICAL_ELF_TABLES |
                                  NEVERC_OBJECT_WRITE_ANDROID_KERNEL_RELEASE |
                                  NEVERC_OBJECT_WRITE_DROP_DEBUG_INFO})
    EXPECT_EQ(Invoke(NEVERC_OBJECT_FORMAT_API_MINOR, LegalFlags).Code,
              NEVERC_STATUS_CAPABILITY_UNAVAILABLE);
}

TEST(AndroidKernelProfileContractVerifierTest,
     FinalizationStripsContractEntitiesFromObjectGraph) {
  auto Graph = makeObject(1);
  ASSERT_NE(Graph, nullptr);
  const uint64_t Generation = Graph->generation();
  const uint64_t RetainedSectionID = Graph->sections().front().ID;
  const AndroidKernelContractEntities Contract =
      addAndroidKernelProfileContract(*Graph);
  PluginObjectRelocation Relocation;
  Relocation.ID = Graph->allocateEntityID();
  Relocation.SectionID = Contract.SectionID;
  Relocation.Kind = NEVERC_OBJECT_RELOCATION_ABSOLUTE;
  Relocation.TargetKind = NEVERC_OBJECT_RELOCATION_TARGET_SECTION;
  Relocation.Width = 64;
  Relocation.TargetSectionID = RetainedSectionID;
  Graph->relocations().push_back(std::move(Relocation));
  ASSERT_FALSE(verifyPluginObjectGraph(*Graph));
  ASSERT_EQ(Graph->sectionCount(), 2u);
  ASSERT_EQ(Graph->symbolCount(), 1u);
  ASSERT_EQ(Graph->relocationCount(), 1u);

  Error StripError =
      stripAndroidKernelProfileContract(*Graph, "test final output");
  ASSERT_FALSE(StripError) << errorText(std::move(StripError));
  EXPECT_EQ(Graph->sectionCount(), 1u);
  EXPECT_EQ(Graph->symbolCount(), 0u);
  EXPECT_EQ(Graph->relocationCount(), 0u);
  EXPECT_EQ(Graph->generation(), Generation + 1);
  EXPECT_FALSE(forbidAndroidKernelProfileContract(*Graph, "test final output"));
  EXPECT_FALSE(verifyPluginObjectGraph(*Graph));
}

TEST(AndroidKernelProfileContractVerifierTest,
     FinalizationRejectsRetainedRelocationToContract) {
  auto Graph = makeObject(1);
  ASSERT_NE(Graph, nullptr);
  const AndroidKernelContractEntities Contract =
      addAndroidKernelProfileContract(*Graph);

  PluginObjectRelocation ContractRelocation;
  ContractRelocation.ID = Graph->allocateEntityID();
  ContractRelocation.SectionID = Contract.SectionID;
  ContractRelocation.Kind = NEVERC_OBJECT_RELOCATION_ABSOLUTE;
  ContractRelocation.TargetKind = NEVERC_OBJECT_RELOCATION_TARGET_SECTION;
  ContractRelocation.Width = 64;
  ContractRelocation.TargetSectionID = Graph->sections().front().ID;
  Graph->relocations().push_back(std::move(ContractRelocation));

  PluginObjectRelocation Relocation;
  Relocation.ID = Graph->allocateEntityID();
  Relocation.SectionID = Graph->sections().front().ID;
  Relocation.Kind = NEVERC_OBJECT_RELOCATION_ABSOLUTE;
  Relocation.TargetKind = NEVERC_OBJECT_RELOCATION_TARGET_SYMBOL;
  Relocation.Width = 8;
  Relocation.TargetSymbolID = Contract.SymbolID;
  Graph->relocations().push_back(std::move(Relocation));
  ASSERT_FALSE(verifyPluginObjectGraph(*Graph));

  Error StripError =
      stripAndroidKernelProfileContract(*Graph, "test final output");
  ASSERT_TRUE(static_cast<bool>(StripError));
  EXPECT_NE(errorText(std::move(StripError))
                .find("retained section references the native Android kernel "
                      "profile contract"),
            std::string::npos);
  EXPECT_EQ(Graph->sectionCount(), 2u);
  EXPECT_EQ(Graph->symbolCount(), 1u);
  EXPECT_EQ(Graph->relocationCount(), 2u);
  EXPECT_FALSE(verifyPluginObjectGraph(*Graph));
}

TEST(AndroidKernelModuleFinalizerTest,
     ReleaseStripKeepsOnlyRelocationRequiredPrivateSymbols) {
  auto Graph = makeObject(1);
  ASSERT_NE(Graph, nullptr);
  Graph->sections().front().Data = {0, 0, 0, 0};
  const uint64_t TextSectionID = Graph->sections().front().ID;
  addAndroidKernelProfileContract(*Graph);

  PluginObjectSection Debug;
  Debug.ID = Graph->allocateEntityID();
  Debug.Name = ".debug_info";
  Debug.Kind = NEVERC_OBJECT_SECTION_KIND_DEBUG;
  Debug.Flags = NEVERC_OBJECT_SECTION_DEBUG;
  Debug.Alignment = 1;
  Debug.Data = {0};
  const uint64_t DebugSectionID = Debug.ID;
  Graph->sections().push_back(std::move(Debug));

  PluginObjectSection Comment;
  Comment.ID = Graph->allocateEntityID();
  Comment.Name = ".comment";
  Comment.Alignment = 1;
  Comment.Data = {'N', 'e', 'v', 'e', 'r', 'C'};
  Graph->sections().push_back(std::move(Comment));

  auto AddSymbol = [&](StringRef Name, NevercObjectSymbolBinding Binding,
                       NevercObjectSymbolDefinition Definition,
                       uint64_t SectionID, uint64_t Value) {
    PluginObjectSymbol Symbol;
    Symbol.ID = Graph->allocateEntityID();
    Symbol.Name = Name.str();
    Symbol.Binding = Binding;
    Symbol.Type = Definition == NEVERC_OBJECT_SYMBOL_DEFINITION_UNDEFINED
                      ? NEVERC_OBJECT_SYMBOL_TYPE_NO_TYPE
                      : NEVERC_OBJECT_SYMBOL_TYPE_OBJECT;
    Symbol.Definition = Definition;
    Symbol.SectionID = SectionID;
    Symbol.Value = Value;
    Symbol.Size = Definition == NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED ? 1 : 0;
    const uint64_t ID = Symbol.ID;
    Graph->symbols().push_back(std::move(Symbol));
    return ID;
  };

  const uint64_t NeededLocal =
      AddSymbol("release_needed_local", NEVERC_OBJECT_SYMBOL_BINDING_LOCAL,
                NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED, TextSectionID, 0);
  AddSymbol("release_unneeded_local", NEVERC_OBJECT_SYMBOL_BINDING_LOCAL,
            NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED, TextSectionID, 1);
  const uint64_t NeededImport =
      AddSymbol("release_needed_import", NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL,
                NEVERC_OBJECT_SYMBOL_DEFINITION_UNDEFINED, 0, 0);
  AddSymbol("release_unneeded_import", NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL,
            NEVERC_OBJECT_SYMBOL_DEFINITION_UNDEFINED, 0, 0);
  const uint64_t PublicDefinition = AddSymbol(
      "release_public_definition", NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL,
      NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED, TextSectionID, 2);
  AddSymbol("release_debug_only", NEVERC_OBJECT_SYMBOL_BINDING_LOCAL,
            NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED, DebugSectionID, 0);

  auto AddRelocation = [&](uint64_t SectionID, uint64_t Offset,
                           uint64_t TargetSymbolID) {
    PluginObjectRelocation Relocation;
    Relocation.ID = Graph->allocateEntityID();
    Relocation.SectionID = SectionID;
    Relocation.Offset = Offset;
    Relocation.Kind = NEVERC_OBJECT_RELOCATION_ABSOLUTE;
    Relocation.TargetKind = NEVERC_OBJECT_RELOCATION_TARGET_SYMBOL;
    Relocation.Width = 8;
    Relocation.TargetSymbolID = TargetSymbolID;
    Graph->relocations().push_back(std::move(Relocation));
  };
  AddRelocation(TextSectionID, 0, NeededLocal);
  AddRelocation(TextSectionID, 1, NeededImport);
  AddRelocation(DebugSectionID, 0, PublicDefinition);

  ASSERT_FALSE(verifyPluginObjectGraph(*Graph));
  const uint64_t Generation = Graph->generation();
  AndroidKernelModuleFinalizationPolicy Policy;
  Policy.DropDebugInfo = true;
  Policy.StripUnneededSymbols = true;
  Error Finalize = finalizeAndroidKernelModuleObjectGraph(
      *Graph, Policy, "test release Android module");
  ASSERT_FALSE(Finalize) << errorText(std::move(Finalize));
  EXPECT_EQ(Graph->generation(), Generation + 1);
  EXPECT_FALSE(verifyPluginObjectGraph(*Graph));

  const auto HasSection = [&](StringRef Name) {
    return std::any_of(
        Graph->sections().begin(), Graph->sections().end(),
        [&](const PluginObjectSection &S) { return S.Name == Name; });
  };
  const auto HasSymbol = [&](StringRef Name) {
    return std::any_of(
        Graph->symbols().begin(), Graph->symbols().end(),
        [&](const PluginObjectSymbol &S) { return S.Name == Name; });
  };
  EXPECT_FALSE(HasSection(".neverc.android.kernel.profile"));
  EXPECT_FALSE(HasSection(".debug_info"));
  EXPECT_FALSE(HasSection(".comment"));
  EXPECT_TRUE(HasSymbol("obj_0"));
  EXPECT_FALSE(HasSymbol("release_unneeded_local"));
  EXPECT_TRUE(HasSymbol("release_needed_import"));
  EXPECT_FALSE(HasSymbol("release_unneeded_import"));
  EXPECT_TRUE(HasSymbol("obj_2"));
  EXPECT_FALSE(HasSymbol("release_debug_only"));
  EXPECT_EQ(Graph->relocationCount(), 2u);
  EXPECT_FALSE(verifyFinalAndroidKernelModuleObjectGraph(
      *Graph, Policy, "test release Android module"));
}

TEST(AndroidKernelModuleFinalizerTest,
     DropDebugRemovesReclassifiedGDBIndexByName) {
  auto Graph = makeObject(0);
  ASSERT_NE(Graph, nullptr);

  PluginObjectSection DebugIndex;
  DebugIndex.ID = Graph->allocateEntityID();
  DebugIndex.Name = ".gdb_index";
  DebugIndex.Kind = NEVERC_OBJECT_SECTION_KIND_DATA;
  DebugIndex.Alignment = 4;
  DebugIndex.Data = {UINT8_C(1), UINT8_C(2), UINT8_C(3), UINT8_C(4)};
  Graph->sections().push_back(std::move(DebugIndex));
  ASSERT_FALSE(verifyPluginObjectGraph(*Graph));

  const uint64_t Generation = Graph->generation();
  AndroidKernelModuleFinalizationPolicy Policy;
  Policy.DropDebugInfo = true;
  Error Finalize = finalizeAndroidKernelModuleObjectGraph(
      *Graph, Policy, "test release Android module");
  ASSERT_FALSE(Finalize) << errorText(std::move(Finalize));
  EXPECT_EQ(Graph->generation(), Generation + 1);
  EXPECT_TRUE(Graph->sections().empty());
  EXPECT_FALSE(verifyPluginObjectGraph(*Graph));
}

TEST(AndroidKernelModuleFinalizerTest,
     DropDebugRejectsAllocatedGDBIndexAtomically) {
  auto Graph = makeObject(0);
  ASSERT_NE(Graph, nullptr);

  PluginObjectSection DebugIndex;
  DebugIndex.ID = Graph->allocateEntityID();
  DebugIndex.Name = ".gdb_index";
  DebugIndex.Kind = NEVERC_OBJECT_SECTION_KIND_DATA;
  DebugIndex.Flags = NEVERC_OBJECT_SECTION_ALLOCATED;
  DebugIndex.Alignment = 4;
  DebugIndex.Data = {UINT8_C(1), UINT8_C(2), UINT8_C(3), UINT8_C(4)};
  const uint64_t DebugIndexID = DebugIndex.ID;
  Graph->sections().push_back(std::move(DebugIndex));
  ASSERT_FALSE(verifyPluginObjectGraph(*Graph));

  const uint64_t Generation = Graph->generation();
  AndroidKernelModuleFinalizationPolicy Policy;
  Policy.DropDebugInfo = true;
  Error Verify = verifyFinalAndroidKernelModuleObjectGraph(
      *Graph, Policy, "test release Android module");
  ASSERT_TRUE(static_cast<bool>(Verify));
  const std::string VerifyMessage = errorText(std::move(Verify));
  EXPECT_NE(VerifyMessage.find("allocated"), std::string::npos)
      << VerifyMessage;
  EXPECT_NE(VerifyMessage.find(".gdb_index"), std::string::npos)
      << VerifyMessage;

  Error Finalize = finalizeAndroidKernelModuleObjectGraph(
      *Graph, Policy, "test release Android module");
  ASSERT_TRUE(static_cast<bool>(Finalize));
  const std::string Message = errorText(std::move(Finalize));
  EXPECT_NE(Message.find("allocated"), std::string::npos) << Message;
  EXPECT_NE(Message.find(".gdb_index"), std::string::npos) << Message;
  EXPECT_EQ(Graph->generation(), Generation);
  ASSERT_EQ(Graph->sectionCount(), 1U);
  EXPECT_EQ(Graph->sections().front().ID, DebugIndexID);
}

TEST(AndroidKernelModuleFinalizerTest,
     PlansIDAStyleNamesFromFinalRetainedSectionOrder) {
  auto Graph = makeObject(0);
  ASSERT_NE(Graph, nullptr);

  auto AddSection = [&](StringRef Name, NevercObjectSectionKind Kind,
                        NevercObjectSectionFlags Flags, uint64_t Alignment,
                        size_t DataSize, uint64_t ZeroFillSize = 0) {
    PluginObjectSection Section;
    Section.ID = Graph->allocateEntityID();
    Section.Name = Name.str();
    Section.Kind = Kind;
    Section.Flags = Flags;
    Section.Alignment = Alignment;
    Section.Data.resize(DataSize);
    Section.ZeroFillSize = ZeroFillSize;
    const uint64_t ID = Section.ID;
    Graph->sections().push_back(std::move(Section));
    return ID;
  };
  const uint64_t TextID = AddSection(".text", NEVERC_OBJECT_SECTION_KIND_TEXT,
                                     NEVERC_OBJECT_SECTION_ALLOCATED |
                                         NEVERC_OBJECT_SECTION_EXECUTABLE,
                                     16, 0x11);
  AddSection(".comment", NEVERC_OBJECT_SECTION_KIND_DATA, 0, 1, 3);
  const uint64_t DataID = AddSection(".data", NEVERC_OBJECT_SECTION_KIND_DATA,
                                     NEVERC_OBJECT_SECTION_ALLOCATED |
                                         NEVERC_OBJECT_SECTION_WRITABLE,
                                     0x20, 3);
  const uint64_t BssID = AddSection(
      ".bss", NEVERC_OBJECT_SECTION_KIND_ZERO_FILL,
      NEVERC_OBJECT_SECTION_ALLOCATED | NEVERC_OBJECT_SECTION_WRITABLE, 0x40, 0,
      0x10);
  const uint64_t MetadataID =
      AddSection(".metadata", NEVERC_OBJECT_SECTION_KIND_DATA, 0, 4, 8);
  const uint64_t ModInfoID =
      AddSection(".modinfo", NEVERC_OBJECT_SECTION_KIND_READ_ONLY_DATA,
                 NEVERC_OBJECT_SECTION_ALLOCATED, 1, 8);

  auto AddSymbol = [&](StringRef Name, NevercObjectSymbolBinding Binding,
                       NevercObjectSymbolType Type,
                       NevercObjectSymbolDefinition Definition,
                       uint64_t SectionID, uint64_t Value, uint64_t Size) {
    PluginObjectSymbol Symbol;
    Symbol.ID = Graph->allocateEntityID();
    Symbol.Name = Name.str();
    Symbol.Binding = Binding;
    Symbol.Type = Type;
    Symbol.Definition = Definition;
    Symbol.SectionID = SectionID;
    Symbol.Value = Value;
    Symbol.Size = Size;
    const uint64_t ID = Symbol.ID;
    Graph->symbols().push_back(std::move(Symbol));
    return ID;
  };

  const uint64_t FunctionA =
      AddSymbol("function_a", NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL,
                NEVERC_OBJECT_SYMBOL_TYPE_FUNCTION,
                NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED, TextID, 0, 1);
  const uint64_t FunctionB =
      AddSymbol("function_b", NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL,
                NEVERC_OBJECT_SYMBOL_TYPE_FUNCTION,
                NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED, TextID, 0, 1);
  const uint64_t ExecutableLabel =
      AddSymbol("executable_label", NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL,
                NEVERC_OBJECT_SYMBOL_TYPE_NO_TYPE,
                NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED, TextID, 8, 0);
  const uint64_t CanonicalLookingOriginal =
      AddSymbol("fn_0", NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL,
                NEVERC_OBJECT_SYMBOL_TYPE_FUNCTION,
                NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED, TextID, 4, 1);
  const uint64_t DataObject =
      AddSymbol("data_object", NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL,
                NEVERC_OBJECT_SYMBOL_TYPE_OBJECT,
                NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED, DataID, 1, 1);
  const uint64_t DataLabel =
      AddSymbol("data_label", NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL,
                NEVERC_OBJECT_SYMBOL_TYPE_NO_TYPE,
                NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED, DataID, 2, 0);
  const uint64_t BssObject =
      AddSymbol("bss_object", NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL,
                NEVERC_OBJECT_SYMBOL_TYPE_OBJECT,
                NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED, BssID, 8, 1);
  const uint64_t Metadata =
      AddSymbol("metadata_label", NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL,
                NEVERC_OBJECT_SYMBOL_TYPE_NO_TYPE,
                NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED, MetadataID, 3, 0);
  const uint64_t Absolute =
      AddSymbol("absolute_value", NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL,
                NEVERC_OBJECT_SYMBOL_TYPE_NO_TYPE,
                NEVERC_OBJECT_SYMBOL_DEFINITION_ABSOLUTE, 0, 0x2a, 0);
  const uint64_t Import =
      AddSymbol("kernel_import", NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL,
                NEVERC_OBJECT_SYMBOL_TYPE_NO_TYPE,
                NEVERC_OBJECT_SYMBOL_DEFINITION_UNDEFINED, 0, 0, 0);
  const uint64_t Loader =
      AddSymbol("init_module", NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL,
                NEVERC_OBJECT_SYMBOL_TYPE_FUNCTION,
                NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED, TextID, 0xc, 1);
  const uint64_t ProtectedSection =
      AddSymbol("module_metadata", NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL,
                NEVERC_OBJECT_SYMBOL_TYPE_OBJECT,
                NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED, ModInfoID, 0, 1);

  PluginObjectRelocation Relocation;
  Relocation.ID = Graph->allocateEntityID();
  Relocation.SectionID = TextID;
  Relocation.Offset = 0;
  Relocation.Kind = NEVERC_OBJECT_RELOCATION_ABSOLUTE;
  Relocation.TargetKind = NEVERC_OBJECT_RELOCATION_TARGET_SYMBOL;
  Relocation.Width = 64;
  Relocation.TargetSymbolID = Import;
  Graph->relocations().push_back(std::move(Relocation));

  ASSERT_FALSE(verifyPluginObjectGraph(*Graph));
  AndroidKernelModuleFinalizationPolicy Policy;
  Policy.StripUnneededSymbols = true;
  Error Finalize = finalizeAndroidKernelModuleObjectGraph(
      *Graph, Policy, "test structural release names");
  ASSERT_FALSE(Finalize) << errorText(std::move(Finalize));

  std::vector<std::string> FunctionAliasNames{
      Graph->findSymbol(FunctionA)->Name, Graph->findSymbol(FunctionB)->Name};
  llvm::sort(FunctionAliasNames);
  EXPECT_EQ(FunctionAliasNames, (std::vector<std::string>{"fn_0", "fn_0_1"}));
  EXPECT_EQ(Graph->findSymbol(ExecutableLabel)->Name, "code_8");
  EXPECT_EQ(Graph->findSymbol(CanonicalLookingOriginal)->Name, "fn_4");
  EXPECT_EQ(Graph->findSymbol(DataObject)->Name, "obj_21");
  EXPECT_EQ(Graph->findSymbol(DataLabel)->Name, "sym_22");
  EXPECT_EQ(Graph->findSymbol(BssObject)->Name, "obj_48");
  EXPECT_EQ(Graph->findSymbol(Metadata)->Name, "sym_S4_3");
  EXPECT_EQ(Graph->findSymbol(Absolute)->Name, "abs_2A");
  EXPECT_EQ(Graph->findSymbol(Import)->Name, "kernel_import");
  EXPECT_EQ(Graph->findSymbol(Loader)->Name, "init_module");
  EXPECT_EQ(Graph->findSymbol(ProtectedSection)->Name, "module_metadata");

  std::swap(Graph->findSymbol(FunctionA)->Name,
            Graph->findSymbol(FunctionB)->Name);
  Graph->advanceGeneration();
  EXPECT_FALSE(verifyFinalAndroidKernelModuleObjectGraph(
      *Graph, Policy, "test exact-tie release name exchange"));
  Graph->findSymbol(FunctionA)->Name = "fn_0_2";
  Graph->advanceGeneration();
  Error WrongAliasMultiset = verifyFinalAndroidKernelModuleObjectGraph(
      *Graph, Policy, "test wrong exact-tie release name multiset");
  ASSERT_TRUE(static_cast<bool>(WrongAliasMultiset));
  EXPECT_NE(
      errorText(std::move(WrongAliasMultiset)).find("release symbol plan"),
      std::string::npos);
}

TEST(AndroidKernelModuleFinalizerTest,
     CanonicalAuditKeepsFullNativeOtherInExchangeClasses) {
  auto Graph = makeAndroidObject(1);
  ASSERT_NE(Graph, nullptr);
  PluginObjectSection &Text = Graph->sections().front();
  Text.Name = ".text";
  Text.Kind = NEVERC_OBJECT_SECTION_KIND_TEXT;
  Text.Flags =
      NEVERC_OBJECT_SECTION_ALLOCATED | NEVERC_OBJECT_SECTION_EXECUTABLE;
  Text.Data = {UINT8_C(0)};
  attachCanonicalELFSectionFacts(*Graph);

  const auto AddFunction = [&](StringRef Name, uint64_t Other) {
    PluginObjectSymbol Symbol;
    Symbol.ID = Graph->allocateEntityID();
    Symbol.Name = Name.str();
    Symbol.Binding = NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL;
    Symbol.Type = NEVERC_OBJECT_SYMBOL_TYPE_FUNCTION;
    Symbol.Definition = NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED;
    Symbol.SectionID = Text.ID;
    Symbol.Size = 1;
    Symbol.Extension.Owner = TestFormatID;
    Symbol.Extension.Version = neverc::plugin::builtinext::SymbolVersion;
    const SmallVector<uint8_t, 48> NativeFacts = makeELFSymbolExtension(
        neverc::plugin::builtinext::SymbolVersion, ELF::STT_FUNC,
        ELF::STB_GLOBAL, Other, Symbol.Size);
    Symbol.Extension.Bytes.assign(NativeFacts.begin(), NativeFacts.end());
    const uint64_t ID = Symbol.ID;
    Graph->symbols().push_back(std::move(Symbol));
    return ID;
  };

  // Full st_other orders these as fn_0 then fn_0_1. Swapping the names is not
  // an exchange within one exact observable tie, even though both expose the
  // same stable DEFAULT visibility.
  const uint64_t Plain = AddFunction("fn_0_1", ELF::STV_DEFAULT);
  const uint64_t VariantPCS =
      AddFunction("fn_0", UINT64_C(0x80) | ELF::STV_DEFAULT);
  ASSERT_FALSE(verifyPluginObjectGraph(*Graph));

  AndroidKernelModuleFinalizationPolicy Policy;
  Policy.StripUnneededSymbols = true;
  Policy.SymbolNameState = AndroidKernelSymbolNameState::CanonicalRelease;
  Error Swapped = verifyFinalAndroidKernelModuleObjectGraph(
      *Graph, Policy, "test full native st_other exchange class");
  ASSERT_TRUE(static_cast<bool>(Swapped));
  EXPECT_NE(errorText(std::move(Swapped)).find("release symbol plan"),
            std::string::npos);

  Graph->findSymbol(Plain)->Name = "fn_0";
  Graph->findSymbol(VariantPCS)->Name = "fn_0_1";
  Graph->advanceGeneration();
  Error Canonical = verifyFinalAndroidKernelModuleObjectGraph(
      *Graph, Policy, "test canonical full native st_other plan");
  ASSERT_FALSE(Canonical) << errorText(std::move(Canonical));
}

TEST(AndroidKernelModuleFinalizerTest,
     CanonicalAuditUsesNativeBindingAndAbsoluteSizeExchangeClasses) {
  AndroidKernelModuleFinalizationPolicy Policy;
  Policy.StripUnneededSymbols = true;
  Policy.SymbolNameState = AndroidKernelSymbolNameState::CanonicalRelease;

  {
    auto Graph = makeAndroidObject(1);
    ASSERT_NE(Graph, nullptr);
    PluginObjectSection &Text = Graph->sections().front();
    Text.Name = ".text";
    Text.Kind = NEVERC_OBJECT_SECTION_KIND_TEXT;
    Text.Flags =
        NEVERC_OBJECT_SECTION_ALLOCATED | NEVERC_OBJECT_SECTION_EXECUTABLE;
    Text.Data = {UINT8_C(0)};
    attachCanonicalELFSectionFacts(*Graph);

    const auto AddFunction = [&](StringRef Name, uint64_t NativeBinding) {
      PluginObjectSymbol Symbol;
      Symbol.ID = Graph->allocateEntityID();
      Symbol.Name = Name.str();
      // llvm::object deliberately projects every non-local/non-weak ELF
      // binding, including STB_GNU_UNIQUE, to stable GLOBAL.
      Symbol.Binding = NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL;
      Symbol.Type = NEVERC_OBJECT_SYMBOL_TYPE_FUNCTION;
      Symbol.Definition = NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED;
      Symbol.SectionID = Text.ID;
      Symbol.Size = 1;
      Symbol.Extension.Owner = TestFormatID;
      Symbol.Extension.Version = neverc::plugin::builtinext::SymbolVersion;
      const SmallVector<uint8_t, 48> NativeFacts = makeELFSymbolExtension(
          neverc::plugin::builtinext::SymbolVersion, ELF::STT_FUNC,
          NativeBinding, ELF::STV_DEFAULT, Symbol.Size);
      Symbol.Extension.Bytes.assign(NativeFacts.begin(), NativeFacts.end());
      const uint64_t ID = Symbol.ID;
      Graph->symbols().push_back(std::move(Symbol));
      return ID;
    };

    const uint64_t Global = AddFunction("fn_0_1", ELF::STB_GLOBAL);
    const uint64_t Unique = AddFunction("fn_0", ELF::STB_GNU_UNIQUE);
    ASSERT_FALSE(verifyPluginObjectGraph(*Graph));
    Error Swapped = verifyFinalAndroidKernelModuleObjectGraph(
        *Graph, Policy, "test native binding exchange class");
    ASSERT_TRUE(static_cast<bool>(Swapped));
    EXPECT_NE(errorText(std::move(Swapped)).find("release symbol plan"),
              std::string::npos);

    Graph->findSymbol(Global)->Name = "fn_0";
    Graph->findSymbol(Unique)->Name = "fn_0_1";
    Graph->advanceGeneration();
    Error Canonical = verifyFinalAndroidKernelModuleObjectGraph(
        *Graph, Policy, "test canonical native binding plan");
    ASSERT_FALSE(Canonical) << errorText(std::move(Canonical));
  }

  {
    auto Graph = makeAndroidObject(1);
    ASSERT_NE(Graph, nullptr);
    attachCanonicalELFSectionFacts(*Graph);
    const auto AddAbsolute = [&](StringRef Name, uint64_t NativeSize) {
      PluginObjectSymbol Symbol;
      Symbol.ID = Graph->allocateEntityID();
      Symbol.Name = Name.str();
      Symbol.Binding = NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL;
      Symbol.Type = NEVERC_OBJECT_SYMBOL_TYPE_NO_TYPE;
      Symbol.Definition = NEVERC_OBJECT_SYMBOL_DEFINITION_ABSOLUTE;
      Symbol.Value = UINT64_C(0x2a);
      // The current built-in reader preserves ABS st_size in NCSY but projects
      // the stable size to zero because no section extent owns the symbol.
      Symbol.Size = 0;
      Symbol.Extension.Owner = TestFormatID;
      Symbol.Extension.Version = neverc::plugin::builtinext::SymbolVersion;
      const SmallVector<uint8_t, 48> NativeFacts = makeELFSymbolExtension(
          neverc::plugin::builtinext::SymbolVersion, ELF::STT_NOTYPE,
          ELF::STB_GLOBAL, ELF::STV_DEFAULT, NativeSize);
      Symbol.Extension.Bytes.assign(NativeFacts.begin(), NativeFacts.end());
      const uint64_t ID = Symbol.ID;
      Graph->symbols().push_back(std::move(Symbol));
      return ID;
    };

    const uint64_t Empty = AddAbsolute("abs_2A_1", 0);
    const uint64_t Sized = AddAbsolute("abs_2A", 7);
    ASSERT_FALSE(verifyPluginObjectGraph(*Graph));
    Error Swapped = verifyFinalAndroidKernelModuleObjectGraph(
        *Graph, Policy, "test native absolute-size exchange class");
    ASSERT_TRUE(static_cast<bool>(Swapped));
    EXPECT_NE(errorText(std::move(Swapped)).find("release symbol plan"),
              std::string::npos);

    Graph->findSymbol(Empty)->Name = "abs_2A";
    Graph->findSymbol(Sized)->Name = "abs_2A_1";
    Graph->advanceGeneration();
    Error Canonical = verifyFinalAndroidKernelModuleObjectGraph(
        *Graph, Policy, "test canonical native absolute-size plan");
    ASSERT_FALSE(Canonical) << errorText(std::move(Canonical));
  }
}

TEST(AndroidKernelModuleFinalizerTest,
     CanonicalAuditAcceptsReaderProjectedProtectedVisibility) {
  auto Graph = makeAndroidObject(1);
  ASSERT_NE(Graph, nullptr);
  PluginObjectSection &Text = Graph->sections().front();
  Text.Name = ".text";
  Text.Kind = NEVERC_OBJECT_SECTION_KIND_TEXT;
  Text.Flags =
      NEVERC_OBJECT_SECTION_ALLOCATED | NEVERC_OBJECT_SECTION_EXECUTABLE;
  Text.Data = {UINT8_C(0)};
  attachCanonicalELFSectionFacts(*Graph);

  PluginObjectSymbol Symbol;
  Symbol.ID = Graph->allocateEntityID();
  Symbol.Name = "fn_0";
  Symbol.Binding = NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL;
  // llvm::object only exposes SF_Hidden; INTERNAL and PROTECTED both project
  // to the stable DEFAULT value while NCSY retains the exact st_other byte.
  Symbol.Visibility = NEVERC_OBJECT_SYMBOL_VISIBILITY_DEFAULT;
  Symbol.Type = NEVERC_OBJECT_SYMBOL_TYPE_FUNCTION;
  Symbol.Definition = NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED;
  Symbol.SectionID = Text.ID;
  Symbol.Size = 1;
  Symbol.Extension.Owner = TestFormatID;
  Symbol.Extension.Version = neverc::plugin::builtinext::SymbolVersion;
  const SmallVector<uint8_t, 48> NativeFacts = makeELFSymbolExtension(
      neverc::plugin::builtinext::SymbolVersion, ELF::STT_FUNC, ELF::STB_GLOBAL,
      ELF::STV_PROTECTED, Symbol.Size);
  Symbol.Extension.Bytes.assign(NativeFacts.begin(), NativeFacts.end());
  Graph->symbols().push_back(std::move(Symbol));
  ASSERT_FALSE(verifyPluginObjectGraph(*Graph));

  AndroidKernelModuleFinalizationPolicy Policy;
  Policy.StripUnneededSymbols = true;
  Policy.SymbolNameState = AndroidKernelSymbolNameState::CanonicalRelease;
  Error Audit = verifyFinalAndroidKernelModuleObjectGraph(
      *Graph, Policy, "test reader-projected protected visibility");
  ASSERT_FALSE(Audit) << errorText(std::move(Audit));
}

TEST(AndroidKernelModuleFinalizerTest,
     CanonicalAuditRejectsMalformedNativeSymbolFacts) {
  const auto ExpectRejected = [&](ArrayRef<uint8_t> NativeFacts,
                                  uint32_t OuterVersion,
                                  StringRef ExpectedReason) {
    auto Graph = makeAndroidObject(1);
    ASSERT_NE(Graph, nullptr);
    PluginObjectSection &Text = Graph->sections().front();
    Text.Name = ".text";
    Text.Kind = NEVERC_OBJECT_SECTION_KIND_TEXT;
    Text.Flags =
        NEVERC_OBJECT_SECTION_ALLOCATED | NEVERC_OBJECT_SECTION_EXECUTABLE;
    Text.Data = {UINT8_C(0)};
    attachCanonicalELFSectionFacts(*Graph);

    PluginObjectSymbol Symbol;
    Symbol.ID = Graph->allocateEntityID();
    Symbol.Name = "fn_0";
    Symbol.Binding = NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL;
    Symbol.Type = NEVERC_OBJECT_SYMBOL_TYPE_FUNCTION;
    Symbol.Definition = NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED;
    Symbol.SectionID = Text.ID;
    Symbol.Size = 1;
    Symbol.Extension.Owner = TestFormatID;
    Symbol.Extension.Version = OuterVersion;
    Symbol.Extension.Bytes.assign(NativeFacts.begin(), NativeFacts.end());
    Graph->symbols().push_back(std::move(Symbol));
    ASSERT_FALSE(verifyPluginObjectGraph(*Graph));

    AndroidKernelModuleFinalizationPolicy Policy;
    Policy.StripUnneededSymbols = true;
    Policy.SymbolNameState = AndroidKernelSymbolNameState::CanonicalRelease;
    Error Audit = verifyFinalAndroidKernelModuleObjectGraph(
        *Graph, Policy, "test malformed canonical native st_other");
    ASSERT_TRUE(static_cast<bool>(Audit));
    EXPECT_NE(errorText(std::move(Audit)).find(ExpectedReason.str()),
              std::string::npos);
  };

  SmallVector<uint8_t, 48> Truncated = makeELFSymbolExtension(
      neverc::plugin::builtinext::SymbolVersion, ELF::STT_FUNC, ELF::STB_GLOBAL,
      ELF::STV_DEFAULT, 1);
  Truncated.pop_back();
  ExpectRejected(Truncated, neverc::plugin::builtinext::SymbolVersion,
                 "exact version-2 payload");

  const SmallVector<uint8_t, 48> TooWide = makeELFSymbolExtension(
      neverc::plugin::builtinext::SymbolVersion, ELF::STT_FUNC, ELF::STB_GLOBAL,
      UINT64_C(0x100), 1);
  ExpectRejected(TooWide, neverc::plugin::builtinext::SymbolVersion,
                 "does not fit ELF st_other");

  const SmallVector<uint8_t, 48> VisibilityMismatch = makeELFSymbolExtension(
      neverc::plugin::builtinext::SymbolVersion, ELF::STT_FUNC, ELF::STB_GLOBAL,
      ELF::STV_HIDDEN, 1);
  ExpectRejected(VisibilityMismatch, neverc::plugin::builtinext::SymbolVersion,
                 "disagrees with stable visibility");

  const SmallVector<uint8_t, 48> TypeMismatch = makeELFSymbolExtension(
      neverc::plugin::builtinext::SymbolVersion, ELF::STT_OBJECT,
      ELF::STB_GLOBAL, ELF::STV_DEFAULT, 1);
  ExpectRejected(TypeMismatch, neverc::plugin::builtinext::SymbolVersion,
                 "SymbolType disagrees with stable type");

  const SmallVector<uint8_t, 48> BindingMismatch =
      makeELFSymbolExtension(neverc::plugin::builtinext::SymbolVersion,
                             ELF::STT_FUNC, ELF::STB_WEAK, ELF::STV_DEFAULT, 1);
  ExpectRejected(BindingMismatch, neverc::plugin::builtinext::SymbolVersion,
                 "SymbolBinding disagrees with stable binding");

  const SmallVector<uint8_t, 48> SizeMismatch = makeELFSymbolExtension(
      neverc::plugin::builtinext::SymbolVersion, ELF::STT_FUNC, ELF::STB_GLOBAL,
      ELF::STV_DEFAULT, 2);
  ExpectRejected(SizeMismatch, neverc::plugin::builtinext::SymbolVersion,
                 "SymbolAuxiliary disagrees with stable size projection");

  const SmallVector<uint8_t, 48> VersionOne = makeELFSymbolExtension(
      1, ELF::STT_FUNC, ELF::STB_GLOBAL, ELF::STV_DEFAULT, 1);
  ExpectRejected(VersionOne, 1, "exact version-2 payload");

  const SmallVector<uint8_t, 48> NameStateMismatch = makeELFSymbolExtension(
      neverc::plugin::builtinext::SymbolVersion, ELF::STT_FUNC, ELF::STB_GLOBAL,
      ELF::STV_DEFAULT, 1, neverc::plugin::builtinext::SymbolNameEmpty);
  ExpectRejected(NameStateMismatch, neverc::plugin::builtinext::SymbolVersion,
                 "SymbolNameState disagrees with the stable name");

  const SmallVector<uint8_t, 48> InvalidNameState = makeELFSymbolExtension(
      neverc::plugin::builtinext::SymbolVersion, ELF::STT_FUNC, ELF::STB_GLOBAL,
      ELF::STV_DEFAULT, 1, 2);
  ExpectRejected(InvalidNameState, neverc::plugin::builtinext::SymbolVersion,
                 "invalid native SymbolNameState");
}

TEST(AndroidKernelModuleFinalizerTest,
     CanonicalSectionFactsReplayReaderProjectionAndRejectTampering) {
  struct ProjectionCase {
    StringLiteral Name;
    uint64_t Type;
    uint64_t NativeFlags;
    NevercObjectSectionKind ExpectedKind;
    NevercObjectSectionFlags ExpectedFlags;
  };
  constexpr ProjectionCase Cases[] = {
      {".note.android.ident", ELF::SHT_NOTE, ELF::SHF_ALLOC,
       NEVERC_OBJECT_SECTION_KIND_READ_ONLY_DATA,
       NEVERC_OBJECT_SECTION_ALLOCATED},
      {".init_array", ELF::SHT_INIT_ARRAY, ELF::SHF_ALLOC | ELF::SHF_WRITE,
       NEVERC_OBJECT_SECTION_KIND_DATA,
       NEVERC_OBJECT_SECTION_ALLOCATED | NEVERC_OBJECT_SECTION_WRITABLE},
      {".tdata.exec", ELF::SHT_PROGBITS,
       ELF::SHF_ALLOC | ELF::SHF_WRITE | ELF::SHF_TLS | ELF::SHF_EXECINSTR,
       NEVERC_OBJECT_SECTION_KIND_TLS_DATA,
       NEVERC_OBJECT_SECTION_ALLOCATED | NEVERC_OBJECT_SECTION_WRITABLE |
           NEVERC_OBJECT_SECTION_EXECUTABLE | NEVERC_OBJECT_SECTION_TLS},
      {".opaque", ELF::SHT_PROGBITS, 0,
       NEVERC_OBJECT_SECTION_KIND_FORMAT_EXTENSION, 0},
      {".gdb_index", ELF::SHT_PROGBITS, 0, NEVERC_OBJECT_SECTION_KIND_DEBUG,
       NEVERC_OBJECT_SECTION_DEBUG},
      {".text.unusual", ELF::SHT_PROGBITS, ELF::SHF_ALLOC | ELF::SHF_EXECINSTR,
       NEVERC_OBJECT_SECTION_KIND_TEXT,
       NEVERC_OBJECT_SECTION_ALLOCATED | NEVERC_OBJECT_SECTION_EXECUTABLE},
  };

  for (const ProjectionCase &TestCase : Cases) {
    const NativeELFSectionProjection Projection = projectNativeELFSection(
        TestCase.Name, TestCase.Type, TestCase.NativeFlags);
    EXPECT_EQ(Projection.Kind, TestCase.ExpectedKind) << TestCase.Name.str();
    EXPECT_EQ(Projection.Flags, TestCase.ExpectedFlags) << TestCase.Name.str();

    PluginObjectSection Section;
    Section.ID = 1;
    Section.Name = TestCase.Name.str();
    Section.Kind = Projection.Kind;
    Section.Flags = Projection.Flags;
    Section.Alignment = 1;
    Section.Data = {0};
    Section.Extension.Owner = TestFormatID;
    Section.Extension.Version = neverc::plugin::builtinext::SectionVersion;
    const SmallVector<uint8_t, 64> NativeFacts =
        makeELFSectionExtension(neverc::plugin::builtinext::SectionVersion,
                                TestCase.Type, TestCase.NativeFlags, 0);
    Section.Extension.Bytes.assign(NativeFacts.begin(), NativeFacts.end());
    Error Verify = verifyCanonicalAndroidKernelReleaseReaderSection(
        Section, 1, "test canonical reader section projection");
    EXPECT_FALSE(Verify) << TestCase.Name.str() << ": "
                         << errorText(std::move(Verify));
  }

  const auto ExpectTamperRejected = [&](size_t Field, uint64_t Value,
                                        StringRef ExpectedReason) {
    PluginObjectSection Section;
    Section.ID = 1;
    Section.Name = ".text";
    Section.Kind = NEVERC_OBJECT_SECTION_KIND_TEXT;
    Section.Flags =
        NEVERC_OBJECT_SECTION_ALLOCATED | NEVERC_OBJECT_SECTION_EXECUTABLE;
    Section.Alignment = 1;
    Section.Data = {0};
    Section.Extension.Owner = TestFormatID;
    Section.Extension.Version = neverc::plugin::builtinext::SectionVersion;
    const SmallVector<uint8_t, 64> NativeFacts = makeELFSectionExtension(
        neverc::plugin::builtinext::SectionVersion, ELF::SHT_PROGBITS,
        ELF::SHF_ALLOC | ELF::SHF_EXECINSTR, 0);
    Section.Extension.Bytes.assign(NativeFacts.begin(), NativeFacts.end());
    overwriteExtensionU64(Section.Extension.Bytes, Field, Value);
    Error Verify = verifyCanonicalAndroidKernelReleaseReaderSection(
        Section, 1, "test tampered canonical reader section facts");
    ASSERT_TRUE(static_cast<bool>(Verify));
    const std::string Message = errorText(std::move(Verify));
    EXPECT_NE(Message.find(ExpectedReason.str()), std::string::npos) << Message;
  };
  ExpectTamperRejected(neverc::plugin::builtinext::SectionIndex, 2,
                       "index disagrees");
  ExpectTamperRejected(neverc::plugin::builtinext::SectionAddress, 1,
                       "nonzero ET_REL sh_addr");
  ExpectTamperRejected(neverc::plugin::builtinext::SectionType,
                       UINT64_C(1) << 32, "invalid native section type");
  ExpectTamperRejected(neverc::plugin::builtinext::SectionFlags, ELF::SHF_ALLOC,
                       "stable kind");

  PluginObjectSection Truncated;
  Truncated.ID = 1;
  Truncated.Name = ".text";
  Truncated.Kind = NEVERC_OBJECT_SECTION_KIND_TEXT;
  Truncated.Flags =
      NEVERC_OBJECT_SECTION_ALLOCATED | NEVERC_OBJECT_SECTION_EXECUTABLE;
  Truncated.Alignment = 1;
  Truncated.Data = {0};
  Truncated.Extension.Owner = TestFormatID;
  Truncated.Extension.Version = neverc::plugin::builtinext::SectionVersion;
  const SmallVector<uint8_t, 64> Complete = makeELFSectionExtension(
      neverc::plugin::builtinext::SectionVersion, ELF::SHT_PROGBITS,
      ELF::SHF_ALLOC | ELF::SHF_EXECINSTR, 0);
  Truncated.Extension.Bytes.assign(Complete.begin(), Complete.end() - 1);
  Error TruncatedError = verifyCanonicalAndroidKernelReleaseReaderSection(
      Truncated, 1, "test truncated canonical reader section facts");
  ASSERT_TRUE(static_cast<bool>(TruncatedError));
  EXPECT_NE(errorText(std::move(TruncatedError)).find("exact NCSE v2"),
            std::string::npos);
}

TEST(AndroidKernelModuleFinalizerTest,
     CanonicalRelocationFactsPreserveNullTargetsAndRejectTampering) {
  auto Graph = makeAndroidObject(1);
  ASSERT_NE(Graph, nullptr);
  Graph->sections().front().Data.resize(16);

  const auto MakeRelocation = [&]() {
    PluginObjectRelocation Relocation;
    Relocation.ID = 2;
    Relocation.SectionID = Graph->sections().front().ID;
    Relocation.Kind = NEVERC_OBJECT_RELOCATION_ABSOLUTE;
    Relocation.TargetKind = NEVERC_OBJECT_RELOCATION_TARGET_SYMBOL;
    Relocation.Width = 64;
    Relocation.TargetSymbolID = 3;
    Relocation.Extension.Owner = TestFormatID;
    Relocation.Extension.Version =
        neverc::plugin::builtinext::RelocationVersion;
    const SmallVector<uint8_t, 80> NativeFacts = makeELFRelocationExtension(
        neverc::plugin::builtinext::RelocationVersion, ELF::R_AARCH64_ABS64,
        "R_AARCH64_ABS64");
    Relocation.Extension.Bytes.assign(NativeFacts.begin(), NativeFacts.end());
    return Relocation;
  };

  PluginObjectRelocation SymbolTarget = MakeRelocation();
  EXPECT_FALSE(verifyCanonicalAndroidKernelReleaseReaderRelocation(
      *Graph, SymbolTarget, "test canonical symbol relocation"));

  PluginObjectRelocation SectionTarget = MakeRelocation();
  SectionTarget.TargetKind = NEVERC_OBJECT_RELOCATION_TARGET_SECTION;
  SectionTarget.TargetSymbolID = 0;
  SectionTarget.TargetSectionID = Graph->sections().front().ID;
  SectionTarget.TargetValue = 16;
  EXPECT_FALSE(verifyCanonicalAndroidKernelReleaseReaderRelocation(
      *Graph, SectionTarget, "test canonical section relocation"));

  PluginObjectRelocation NullTarget = MakeRelocation();
  NullTarget.TargetKind = NEVERC_OBJECT_RELOCATION_TARGET_FORMAT_EXTENSION;
  NullTarget.TargetSymbolID = 0;
  NullTarget.TargetExtensionKind = ELF::R_AARCH64_ABS64 + 1;
  EXPECT_FALSE(verifyCanonicalAndroidKernelReleaseReaderRelocation(
      *Graph, NullTarget, "test canonical null-symbol relocation"));
  Error PortableNull = verifyPortableAndroidKernelReleaseWriterRelocation(
      *Graph, NullTarget, "test portable null-symbol relocation");
  ASSERT_TRUE(static_cast<bool>(PortableNull));
  EXPECT_NE(errorText(std::move(PortableNull)).find("target kind"),
            std::string::npos);

  NullTarget.TargetExtensionKind = ELF::R_AARCH64_ABS64 + 2;
  Error BadToken = verifyCanonicalAndroidKernelReleaseReaderRelocation(
      *Graph, NullTarget, "test bad null-symbol token");
  ASSERT_TRUE(static_cast<bool>(BadToken));
  EXPECT_NE(errorText(std::move(BadToken)).find("null-symbol target"),
            std::string::npos);

  SymbolTarget.TargetValue = 1;
  Error BadSymbolValue = verifyCanonicalAndroidKernelReleaseReaderRelocation(
      *Graph, SymbolTarget, "test bad symbol target value");
  ASSERT_TRUE(static_cast<bool>(BadSymbolValue));
  EXPECT_NE(errorText(std::move(BadSymbolValue)).find("nonzero target value"),
            std::string::npos);

  SectionTarget.TargetValue = 17;
  Error BadSectionValue = verifyCanonicalAndroidKernelReleaseReaderRelocation(
      *Graph, SectionTarget, "test bad section target value");
  ASSERT_TRUE(static_cast<bool>(BadSectionValue));
  EXPECT_NE(errorText(std::move(BadSectionValue)).find("outside"),
            std::string::npos);

  PluginObjectRelocation BadName = MakeRelocation();
  const SmallVector<uint8_t, 80> BadNameFacts =
      makeELFRelocationExtension(neverc::plugin::builtinext::RelocationVersion,
                                 ELF::R_AARCH64_ABS64, "R_AARCH64_CALL26");
  BadName.Extension.Bytes.assign(BadNameFacts.begin(), BadNameFacts.end());
  Error BadNameError = verifyCanonicalAndroidKernelReleaseReaderRelocation(
      *Graph, BadName, "test bad relocation name");
  ASSERT_TRUE(static_cast<bool>(BadNameError));
  EXPECT_NE(errorText(std::move(BadNameError)).find("official relocation name"),
            std::string::npos);

  PluginObjectRelocation BadWidth = MakeRelocation();
  BadWidth.Width = 32;
  Error BadWidthError = verifyCanonicalAndroidKernelReleaseReaderRelocation(
      *Graph, BadWidth, "test bad relocation stable facts");
  ASSERT_TRUE(static_cast<bool>(BadWidthError));
  EXPECT_NE(errorText(std::move(BadWidthError)).find("stable relocation facts"),
            std::string::npos);
}

TEST(AndroidKernelModuleFinalizerTest,
     CanonicalNativeSymbolFactsApplyTLSSectionTypeOverride) {
  struct SymbolCase {
    uint64_t NativeType;
    StringLiteral CanonicalName;
  };
  constexpr SymbolCase Cases[] = {
      {ELF::STT_OBJECT, "obj_0"},
      {ELF::STT_NOTYPE, "sym_0"},
  };
  for (const SymbolCase &TestCase : Cases) {
    auto Graph = makeAndroidObject(1);
    ASSERT_NE(Graph, nullptr);
    PluginObjectSection &TLS = Graph->sections().front();
    TLS.Name = ".tdata";
    TLS.Kind = NEVERC_OBJECT_SECTION_KIND_TLS_DATA;
    TLS.Flags = NEVERC_OBJECT_SECTION_ALLOCATED |
                NEVERC_OBJECT_SECTION_WRITABLE | NEVERC_OBJECT_SECTION_TLS;
    TLS.Data = {0};
    attachCanonicalELFSectionFacts(*Graph);

    PluginObjectSymbol Symbol;
    Symbol.ID = Graph->allocateEntityID();
    Symbol.Name = TestCase.CanonicalName.str();
    Symbol.Binding = NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL;
    Symbol.Type = NEVERC_OBJECT_SYMBOL_TYPE_TLS;
    Symbol.Definition = NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED;
    Symbol.SectionID = TLS.ID;
    Symbol.Size = 1;
    Symbol.Extension.Owner = TestFormatID;
    Symbol.Extension.Version = neverc::plugin::builtinext::SymbolVersion;
    const SmallVector<uint8_t, 48> NativeFacts = makeELFSymbolExtension(
        neverc::plugin::builtinext::SymbolVersion, TestCase.NativeType,
        ELF::STB_GLOBAL, ELF::STV_DEFAULT, 1);
    Symbol.Extension.Bytes.assign(NativeFacts.begin(), NativeFacts.end());
    Graph->symbols().push_back(std::move(Symbol));

    AndroidKernelModuleFinalizationPolicy Policy;
    Policy.StripUnneededSymbols = true;
    Policy.SymbolNameState = AndroidKernelSymbolNameState::CanonicalRelease;
    Error Verify = verifyFinalAndroidKernelModuleObjectGraph(
        *Graph, Policy, "test canonical TLS reader projection");
    EXPECT_FALSE(Verify) << TestCase.CanonicalName.str() << ": "
                         << errorText(std::move(Verify));
  }
}

TEST(AndroidKernelModuleFinalizerTest,
     CanonicalProvenanceRejectsWrongTargetWithoutRelocations) {
  auto Graph = makeObject(1);
  ASSERT_NE(Graph, nullptr);
  attachCanonicalELFSectionFacts(*Graph);
  AndroidKernelModuleFinalizationPolicy Policy;
  Policy.StripUnneededSymbols = true;
  Policy.SymbolNameState = AndroidKernelSymbolNameState::CanonicalRelease;
  Error Verify = verifyFinalAndroidKernelModuleObjectGraph(
      *Graph, Policy, "test zero-relocation wrong canonical target");
  ASSERT_TRUE(static_cast<bool>(Verify));
  EXPECT_NE(errorText(std::move(Verify)).find("AArch64 ELF64 little-endian"),
            std::string::npos);
}

TEST(AndroidKernelModuleFinalizerTest,
     PreservesOnlyExactLoaderImportAndProtectedSectionNames) {
  auto Graph = makeObject(1);
  ASSERT_NE(Graph, nullptr);
  PluginObjectSection &Text = Graph->sections().front();
  Text.Name = ".text";
  Text.Kind = NEVERC_OBJECT_SECTION_KIND_TEXT;
  Text.Flags =
      NEVERC_OBJECT_SECTION_ALLOCATED | NEVERC_OBJECT_SECTION_EXECUTABLE;
  Text.Data.resize(64);
  const uint64_t TextSectionID = Text.ID;

  auto AddSection = [&](StringRef Name) {
    PluginObjectSection Section;
    Section.ID = Graph->allocateEntityID();
    Section.Name = Name.str();
    Section.Kind = Name == ".text.ftrace_trampoline"
                       ? NEVERC_OBJECT_SECTION_KIND_TEXT
                       : NEVERC_OBJECT_SECTION_KIND_DATA;
    Section.Flags = NEVERC_OBJECT_SECTION_ALLOCATED;
    if (Name == ".text.ftrace_trampoline")
      Section.Flags |= NEVERC_OBJECT_SECTION_EXECUTABLE;
    Section.Alignment = 1;
    Section.Data = {0};
    const uint64_t ID = Section.ID;
    Graph->sections().push_back(std::move(Section));
    return ID;
  };
  auto AddSymbol = [&](StringRef Name, NevercObjectSymbolBinding Binding,
                       NevercObjectSymbolType Type,
                       NevercObjectSymbolDefinition Definition,
                       uint64_t SectionID, uint64_t Value = 0) {
    PluginObjectSymbol Symbol;
    Symbol.ID = Graph->allocateEntityID();
    Symbol.Name = Name.str();
    Symbol.Binding = Binding;
    Symbol.Type = Type;
    Symbol.Definition = Definition;
    Symbol.SectionID = SectionID;
    Symbol.Value = Value;
    Symbol.Size = Definition == NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED &&
                          Type != NEVERC_OBJECT_SYMBOL_TYPE_SECTION
                      ? 1
                      : 0;
    const uint64_t ID = Symbol.ID;
    Graph->symbols().push_back(std::move(Symbol));
    return ID;
  };
  auto AddRelocation = [&](uint64_t Offset, uint64_t TargetSymbolID) {
    PluginObjectRelocation Relocation;
    Relocation.ID = Graph->allocateEntityID();
    Relocation.SectionID = TextSectionID;
    Relocation.Offset = Offset;
    Relocation.Kind = NEVERC_OBJECT_RELOCATION_ABSOLUTE;
    Relocation.TargetKind = NEVERC_OBJECT_RELOCATION_TARGET_SYMBOL;
    Relocation.Width = 64;
    Relocation.TargetSymbolID = TargetSymbolID;
    Graph->relocations().push_back(std::move(Relocation));
  };

  const uint64_t Ordinary =
      AddSymbol("ordinary_defined", NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL,
                NEVERC_OBJECT_SYMBOL_TYPE_FUNCTION,
                NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED, TextSectionID);
  const uint64_t Absolute =
      AddSymbol("ordinary_absolute", NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL,
                NEVERC_OBJECT_SYMBOL_TYPE_NO_TYPE,
                NEVERC_OBJECT_SYMBOL_DEFINITION_ABSOLUTE, 0, 42);
  const uint64_t HexSpelledOriginal =
      AddSymbol("0123456789abcdef", NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL,
                NEVERC_OBJECT_SYMBOL_TYPE_FUNCTION,
                NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED, TextSectionID);
  const uint64_t NeededLocalLabel =
      AddSymbol("needed_local_label", NEVERC_OBJECT_SYMBOL_BINDING_LOCAL,
                NEVERC_OBJECT_SYMBOL_TYPE_NO_TYPE,
                NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED, TextSectionID);
  const uint64_t Import =
      AddSymbol("external_import", NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL,
                NEVERC_OBJECT_SYMBOL_TYPE_NO_TYPE,
                NEVERC_OBJECT_SYMBOL_DEFINITION_UNDEFINED, 0);
  AddRelocation(0, NeededLocalLabel);
  AddRelocation(8, Import);

  std::vector<std::pair<uint64_t, std::string>> PreservedNames;
  for (StringRef Name :
       neverc::AndroidKernelModuleSymbolPolicy::PreservedSymbolNames) {
    const uint64_t ID =
        AddSymbol(Name, NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL,
                  NEVERC_OBJECT_SYMBOL_TYPE_FUNCTION,
                  NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED, TextSectionID);
    PreservedNames.emplace_back(ID, Name.str());
  }

  std::vector<std::pair<uint64_t, std::string>> PreservedSectionSymbols;
  unsigned MetadataIndex = 0;
  for (StringRef SectionName :
       neverc::AndroidKernelModuleSymbolPolicy::SymbolNamePreservedSections) {
    const uint64_t SectionID = AddSection(SectionName);
    std::string Name = "metadata_symbol_" + std::to_string(MetadataIndex++);
    const uint64_t ID =
        AddSymbol(Name, NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL,
                  NEVERC_OBJECT_SYMBOL_TYPE_OBJECT,
                  NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED, SectionID);
    PreservedSectionSymbols.emplace_back(ID, std::move(Name));
  }

  // .plt is structurally preserved by the merger, but it is deliberately not
  // one of the five sections whose symbol spellings are loader ABI.
  const uint64_t PLTSectionID = AddSection(".plt");
  const uint64_t PLTSymbol =
      AddSymbol("ordinary_plt_symbol", NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL,
                NEVERC_OBJECT_SYMBOL_TYPE_FUNCTION,
                NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED, PLTSectionID);

  ASSERT_FALSE(verifyPluginObjectGraph(*Graph));
  AndroidKernelModuleFinalizationPolicy Policy;
  Policy.StripUnneededSymbols = true;
  neverc::AndroidKernelReleaseSymbolMap SymbolMap;
  Error Finalize = finalizeAndroidKernelModuleObjectGraph(
      *Graph, Policy, "test release Android module structural names",
      &SymbolMap);
  ASSERT_FALSE(Finalize) << errorText(std::move(Finalize));

  ASSERT_NE(Graph->findSymbol(Ordinary), nullptr);
  ASSERT_NE(Graph->findSymbol(HexSpelledOriginal), nullptr);
  const auto MappedName = [&](StringRef Original) -> StringRef {
    auto It = llvm::find_if(
        SymbolMap.Symbols,
        [&](const neverc::AndroidKernelReleaseSymbolMapEntry &Entry) {
          return Entry.OriginalName == Original;
        });
    return It == SymbolMap.Symbols.end() ? StringRef() : It->ReleaseName;
  };
  EXPECT_EQ(SymbolMap.Symbols.size(), 5u);
  EXPECT_EQ(MappedName("ordinary_defined"), Graph->findSymbol(Ordinary)->Name);
  EXPECT_EQ(MappedName("0123456789abcdef"),
            Graph->findSymbol(HexSpelledOriginal)->Name);
  EXPECT_EQ(MappedName("ordinary_absolute"), Graph->findSymbol(Absolute)->Name);
  EXPECT_EQ(MappedName("needed_local_label"),
            Graph->findSymbol(NeededLocalLabel)->Name);
  EXPECT_EQ(MappedName("ordinary_plt_symbol"),
            Graph->findSymbol(PLTSymbol)->Name);
  EXPECT_TRUE(MappedName("external_import").empty());
  std::vector<std::string> OrdinaryAliasNames{
      Graph->findSymbol(Ordinary)->Name,
      Graph->findSymbol(HexSpelledOriginal)->Name};
  llvm::sort(OrdinaryAliasNames);
  EXPECT_EQ(OrdinaryAliasNames, (std::vector<std::string>{"fn_0", "fn_0_1"}));
  ASSERT_NE(Graph->findSymbol(Absolute), nullptr);
  EXPECT_EQ(Graph->findSymbol(Absolute)->Name, "abs_2A");
  EXPECT_NE(Graph->findSymbol(HexSpelledOriginal)->Name, "0123456789abcdef");
  ASSERT_NE(Graph->findSymbol(PLTSymbol), nullptr);
  EXPECT_EQ(Graph->findSymbol(PLTSymbol)->Name, "fn_45");
  ASSERT_NE(Graph->findSymbol(NeededLocalLabel), nullptr);
  EXPECT_EQ(Graph->findSymbol(NeededLocalLabel)->Name, "code_0");
  ASSERT_NE(Graph->findSymbol(Import), nullptr);
  EXPECT_EQ(Graph->findSymbol(Import)->Name, "external_import");
  for (const auto &[ID, Name] : PreservedNames) {
    ASSERT_NE(Graph->findSymbol(ID), nullptr);
    EXPECT_EQ(Graph->findSymbol(ID)->Name, Name);
  }
  for (const auto &[ID, Name] : PreservedSectionSymbols) {
    ASSERT_NE(Graph->findSymbol(ID), nullptr);
    EXPECT_EQ(Graph->findSymbol(ID)->Name, Name);
  }
  EXPECT_FALSE(verifyFinalAndroidKernelModuleObjectGraph(
      *Graph, Policy, "test release Android module structural names"));

  // A graph plugin cannot restore a readable ordinary definition after the
  // host finalizer and still pass the host-owned pre-write validator.
  Graph->findSymbol(Ordinary)->Name = "bypassed_readable_name";
  Graph->advanceGeneration();
  Error VerifyBypass = verifyFinalAndroidKernelModuleObjectGraph(
      *Graph, Policy, "test release Android module structural names");
  ASSERT_TRUE(static_cast<bool>(VerifyBypass));
  EXPECT_NE(errorText(std::move(VerifyBypass)).find("release symbol plan"),
            std::string::npos);
}

TEST(AndroidKernelModuleFinalizerTest,
     RejectsUnsupportedAndInvalidSymbolTypesWithoutMutation) {
  auto ExpectRejected =
      [&](NevercObjectSymbolType Type, NevercObjectSymbolDefinition Definition,
          StringRef Name, bool TLSSection, bool FormatExtension) {
        auto Graph = makeObject(1);
        ASSERT_NE(Graph, nullptr);
        PluginObjectSection &Section = Graph->sections().front();
        Section.Data.resize(8);
        if (TLSSection) {
          Section.Kind = NEVERC_OBJECT_SECTION_KIND_TLS_DATA;
          Section.Flags = NEVERC_OBJECT_SECTION_ALLOCATED |
                          NEVERC_OBJECT_SECTION_WRITABLE |
                          NEVERC_OBJECT_SECTION_TLS;
        }

        PluginObjectSymbol Symbol;
        Symbol.ID = Graph->allocateEntityID();
        Symbol.Name = Name.str();
        Symbol.Binding = NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL;
        Symbol.Type = Type;
        Symbol.Definition = Definition;
        if (Definition == NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED) {
          Symbol.SectionID = Section.ID;
          Symbol.Size = 1;
        }
        if (FormatExtension) {
          Symbol.Extension.Owner = TestFormatID;
          Symbol.Extension.Version = 1;
          Symbol.Extension.Bytes = {1};
        }
        Graph->symbols().push_back(std::move(Symbol));

        const uint64_t Generation = Graph->generation();
        const std::string Snapshot = dumpPluginObjectGraph(*Graph);
        AndroidKernelModuleFinalizationPolicy Policy;
        Policy.StripUnneededSymbols = true;
        Error Finalize = finalizeAndroidKernelModuleObjectGraph(
            *Graph, Policy, "test unsupported release symbol type");
        EXPECT_TRUE(static_cast<bool>(Finalize));
        if (Finalize)
          consumeError(std::move(Finalize));
        EXPECT_EQ(Graph->generation(), Generation);
        EXPECT_EQ(dumpPluginObjectGraph(*Graph), Snapshot);
      };

  ExpectRejected(NEVERC_OBJECT_SYMBOL_TYPE_TLS,
                 NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED,
                 "__kcfi_typeid_bad_tls", true, false);
  ExpectRejected(NEVERC_OBJECT_SYMBOL_TYPE_INDIRECT_FUNCTION,
                 NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED, "init_module", false,
                 false);
  ExpectRejected(NEVERC_OBJECT_SYMBOL_TYPE_FILE,
                 NEVERC_OBJECT_SYMBOL_DEFINITION_ABSOLUTE,
                 "__typeid__source_global_addr", false, false);
  ExpectRejected(NEVERC_OBJECT_SYMBOL_TYPE_FORMAT_EXTENSION,
                 NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED, "init_module", false,
                 true);
  ExpectRejected(static_cast<NevercObjectSymbolType>(UINT32_C(0xffffffff)),
                 NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED, "init_module", false,
                 false);
}

TEST(AndroidKernelModuleFinalizerTest,
     RejectsWriterLossyBindingAndVisibilityWithoutMutation) {
  auto ExpectRejected = [&](NevercObjectSymbolBinding Binding,
                            NevercObjectSymbolVisibility Visibility) {
    auto Graph = makeObject(1);
    ASSERT_NE(Graph, nullptr);
    Graph->sections().front().Data.resize(8);

    PluginObjectSymbol Symbol;
    Symbol.ID = Graph->allocateEntityID();
    Symbol.Name = "writer_round_trip_required";
    Symbol.Binding = Binding;
    Symbol.Visibility = Visibility;
    Symbol.Type = NEVERC_OBJECT_SYMBOL_TYPE_FUNCTION;
    Symbol.Definition = NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED;
    Symbol.SectionID = Graph->sections().front().ID;
    Symbol.Size = 1;
    if (Binding == NEVERC_OBJECT_SYMBOL_BINDING_FORMAT_EXTENSION ||
        Visibility == NEVERC_OBJECT_SYMBOL_VISIBILITY_FORMAT_EXTENSION) {
      Symbol.Extension.Owner = TestFormatID;
      Symbol.Extension.Version = 1;
      Symbol.Extension.Bytes = {1};
    }
    Graph->symbols().push_back(std::move(Symbol));
    ASSERT_FALSE(verifyPluginObjectGraph(*Graph));

    const uint64_t Generation = Graph->generation();
    const std::string Snapshot = dumpPluginObjectGraph(*Graph);
    AndroidKernelModuleFinalizationPolicy Policy;
    Policy.StripUnneededSymbols = true;
    Error Finalize = finalizeAndroidKernelModuleObjectGraph(
        *Graph, Policy, "test writer-lossy release symbol");
    EXPECT_TRUE(static_cast<bool>(Finalize));
    if (Finalize)
      consumeError(std::move(Finalize));
    EXPECT_EQ(Graph->generation(), Generation);
    EXPECT_EQ(dumpPluginObjectGraph(*Graph), Snapshot);
  };

  ExpectRejected(NEVERC_OBJECT_SYMBOL_BINDING_UNIQUE,
                 NEVERC_OBJECT_SYMBOL_VISIBILITY_DEFAULT);
  ExpectRejected(NEVERC_OBJECT_SYMBOL_BINDING_FORMAT_EXTENSION,
                 NEVERC_OBJECT_SYMBOL_VISIBILITY_DEFAULT);
  ExpectRejected(NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL,
                 NEVERC_OBJECT_SYMBOL_VISIBILITY_PROTECTED);
  ExpectRejected(NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL,
                 NEVERC_OBJECT_SYMBOL_VISIBILITY_INTERNAL);
  ExpectRejected(NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL,
                 NEVERC_OBJECT_SYMBOL_VISIBILITY_FORMAT_EXTENSION);
}

TEST(AndroidKernelModuleFinalizerTest,
     RejectsLossyNativeSymbolExtensionBeforeMutation) {
  initializeBuiltinTargets();
  const BuiltinTargetRoute *ELFRoute = nullptr;
  for (const BuiltinTargetRoute &Route : builtinTargetRoutes()) {
    const Triple Parsed(Triple::normalize(Route.CanonicalTriple));
    if (Route.SupportsObject &&
        Route.ObjectFormat == BuiltinObjectFormat::ELF &&
        Parsed.getArch() == Triple::aarch64) {
      ELFRoute = &Route;
      break;
    }
  }
  ASSERT_NE(ELFRoute, nullptr);

  auto Graph = makeBuiltinObject(
      *ELFRoute, "native_extension_requires_unique_and_protected.__pcg1234");
  ASSERT_NE(Graph, nullptr);
  Graph->sections().front().Data.resize(8);

  PluginObjectSymbol &Symbol = Graph->symbols().front();
  Symbol.Extension.Owner = ELFRoute->ObjectFormatID;
  Symbol.Extension.Version = neverc::plugin::builtinext::SymbolVersion;
  SmallVector<uint8_t, 48> NativeFacts;
  neverc::plugin::builtinext::appendHeader(
      NativeFacts, neverc::plugin::builtinext::SymbolTag,
      neverc::plugin::builtinext::SymbolVersion);
  neverc::plugin::builtinext::appendU64(NativeFacts, ELF::STT_FUNC);
  neverc::plugin::builtinext::appendU64(NativeFacts, ELF::STB_GNU_UNIQUE);
  neverc::plugin::builtinext::appendU64(NativeFacts, ELF::STV_PROTECTED);
  neverc::plugin::builtinext::appendU64(NativeFacts, 1);
  neverc::plugin::builtinext::appendU64(
      NativeFacts, neverc::plugin::builtinext::SymbolNameNonEmpty);
  Symbol.Extension.Bytes.assign(NativeFacts.begin(), NativeFacts.end());

  PluginObjectRelocation Relocation;
  Relocation.ID = Graph->allocateEntityID();
  Relocation.SectionID = Graph->sections().front().ID;
  Relocation.Kind = NEVERC_OBJECT_RELOCATION_ABSOLUTE;
  Relocation.TargetKind = NEVERC_OBJECT_RELOCATION_TARGET_SYMBOL;
  Relocation.Width = 64;
  Relocation.TargetSymbolID = Symbol.ID;
  Relocation.Extension.Owner = ELFRoute->ObjectFormatID;
  Relocation.Extension.Version = neverc::plugin::builtinext::RelocationVersion;
  const SmallVector<uint8_t, 80> RelocationFacts =
      makeELFRelocationExtension(neverc::plugin::builtinext::RelocationVersion,
                                 ELF::R_AARCH64_ABS64, "R_AARCH64_ABS64");
  Relocation.Extension.Bytes.assign(RelocationFacts.begin(),
                                    RelocationFacts.end());
  Graph->relocations().push_back(std::move(Relocation));
  Graph->advanceGeneration();
  Graph->issueLayoutProof();
  ASSERT_FALSE(verifyPluginObjectGraph(*Graph));

  const uint64_t Generation = Graph->generation();
  const std::string Snapshot = dumpPluginObjectGraph(*Graph);
  AndroidKernelModuleFinalizationPolicy Policy;
  Policy.StripUnneededSymbols = true;
  Error Finalize = finalizeAndroidKernelModuleObjectGraph(
      *Graph, Policy, "test lossy native symbol extension");
  ASSERT_TRUE(static_cast<bool>(Finalize));
  const std::string FinalizeMessage = errorText(std::move(Finalize));
  EXPECT_NE(FinalizeMessage.find("cannot round-trip"), std::string::npos)
      << FinalizeMessage;
  EXPECT_EQ(Graph->generation(), Generation);
  EXPECT_EQ(dumpPluginObjectGraph(*Graph), Snapshot);
}

TEST(AndroidKernelModuleFinalizerTest,
     RejectsMalformedNativeSymbolExtensionBeforeMutation) {
  initializeBuiltinTargets();
  const BuiltinTargetRoute *ELFRoute = nullptr;
  for (const BuiltinTargetRoute &Route : builtinTargetRoutes()) {
    const Triple Parsed(Triple::normalize(Route.CanonicalTriple));
    if (Route.SupportsObject &&
        Route.ObjectFormat == BuiltinObjectFormat::ELF &&
        Parsed.getArch() == Triple::aarch64) {
      ELFRoute = &Route;
      break;
    }
  }
  ASSERT_NE(ELFRoute, nullptr);

  const auto MakeNativeFacts = [](std::optional<uint64_t> Auxiliary) {
    SmallVector<uint8_t, 48> NativeFacts;
    neverc::plugin::builtinext::appendHeader(
        NativeFacts, neverc::plugin::builtinext::SymbolTag,
        neverc::plugin::builtinext::SymbolVersion);
    neverc::plugin::builtinext::appendU64(NativeFacts, ELF::STT_FUNC);
    neverc::plugin::builtinext::appendU64(NativeFacts, ELF::STB_GLOBAL);
    neverc::plugin::builtinext::appendU64(NativeFacts, ELF::STV_DEFAULT);
    if (Auxiliary) {
      neverc::plugin::builtinext::appendU64(NativeFacts, *Auxiliary);
      neverc::plugin::builtinext::appendU64(
          NativeFacts, neverc::plugin::builtinext::SymbolNameNonEmpty);
    }
    return NativeFacts;
  };
  const auto ExpectAtomicRejection = [&](ArrayRef<uint8_t> NativeFacts,
                                         StringRef ExpectedReason) {
    auto Graph = makeBuiltinObject(*ELFRoute, "native_extension_symbol");
    ASSERT_NE(Graph, nullptr);
    addAndroidKernelProfileContract(*Graph);
    PluginObjectSymbol &Symbol = Graph->symbols().front();
    Symbol.Extension.Owner = ELFRoute->ObjectFormatID;
    Symbol.Extension.Version = neverc::plugin::builtinext::SymbolVersion;
    Symbol.Extension.Bytes.assign(NativeFacts.begin(), NativeFacts.end());
    ASSERT_FALSE(verifyPluginObjectGraph(*Graph));

    const uint64_t Generation = Graph->generation();
    const size_t SectionCount = Graph->sectionCount();
    const size_t SymbolCount = Graph->symbolCount();
    const std::string Snapshot = dumpPluginObjectGraph(*Graph);
    AndroidKernelModuleFinalizationPolicy Policy;
    Policy.StripUnneededSymbols = true;
    Error Finalize = finalizeAndroidKernelModuleObjectGraph(
        *Graph, Policy, "test malformed native symbol extension");
    ASSERT_TRUE(static_cast<bool>(Finalize));
    EXPECT_NE(errorText(std::move(Finalize)).find(ExpectedReason.str()),
              std::string::npos);
    EXPECT_EQ(Graph->generation(), Generation);
    EXPECT_EQ(Graph->sectionCount(), SectionCount);
    EXPECT_EQ(Graph->symbolCount(), SymbolCount);
    EXPECT_EQ(dumpPluginObjectGraph(*Graph), Snapshot);
  };

  SmallVector<uint8_t, 48> MissingAuxiliary = MakeNativeFacts(std::nullopt);
  ExpectAtomicRejection(MissingAuxiliary, "exact version-2 payload");

  SmallVector<uint8_t, 48> ShortPayload = MissingAuxiliary;
  ShortPayload.pop_back();
  ExpectAtomicRejection(ShortPayload, "exact version-2 payload");

  SmallVector<uint8_t, 48> LongPayload = MakeNativeFacts(UINT64_C(1));
  LongPayload.push_back(UINT8_C(0));
  ExpectAtomicRejection(LongPayload, "exact version-2 payload");

  const SmallVector<uint8_t, 48> ConflictingAuxiliary =
      MakeNativeFacts(UINT64_C(2));
  ExpectAtomicRejection(ConflictingAuxiliary,
                        "native st_size differs from ObjectGraph size");
}

TEST(AndroidKernelModuleFinalizerTest,
     RejectsWriterUnrepresentableUndefinedAndAbsoluteSizesWithoutMutation) {
  const auto MakeGraph = [](NevercObjectSymbolDefinition Definition,
                            NevercObjectSymbolBinding Binding, uint64_t Size) {
    auto Graph = makeObject(1);
    if (!Graph)
      return Graph;
    PluginObjectSection &Text = Graph->sections().front();
    Text.Name = ".text";
    Text.Kind = NEVERC_OBJECT_SECTION_KIND_TEXT;
    Text.Flags =
        NEVERC_OBJECT_SECTION_ALLOCATED | NEVERC_OBJECT_SECTION_EXECUTABLE;
    Text.Data.resize(8);

    PluginObjectSymbol Symbol;
    Symbol.ID = Graph->allocateEntityID();
    Symbol.Name = Definition == NEVERC_OBJECT_SYMBOL_DEFINITION_UNDEFINED
                      ? "sized_import"
                      : "sized_absolute";
    Symbol.Binding = Binding;
    Symbol.Type = NEVERC_OBJECT_SYMBOL_TYPE_NO_TYPE;
    Symbol.Definition = Definition;
    Symbol.Value = Definition == NEVERC_OBJECT_SYMBOL_DEFINITION_ABSOLUTE
                       ? UINT64_C(0x2a)
                       : 0;
    Symbol.Size = Size;
    if (Definition == NEVERC_OBJECT_SYMBOL_DEFINITION_UNDEFINED)
      Symbol.Flags = NEVERC_OBJECT_SYMBOL_IMPORTED;
    const uint64_t SymbolID = Symbol.ID;
    Graph->symbols().push_back(std::move(Symbol));

    if (Definition == NEVERC_OBJECT_SYMBOL_DEFINITION_UNDEFINED) {
      PluginObjectRelocation Relocation;
      Relocation.ID = Graph->allocateEntityID();
      Relocation.SectionID = Text.ID;
      Relocation.Kind = NEVERC_OBJECT_RELOCATION_ABSOLUTE;
      Relocation.TargetKind = NEVERC_OBJECT_RELOCATION_TARGET_SYMBOL;
      Relocation.Width = 64;
      Relocation.TargetSymbolID = SymbolID;
      Graph->relocations().push_back(std::move(Relocation));
    }
    addAndroidKernelProfileContract(*Graph);
    return Graph;
  };

  for (NevercObjectSymbolDefinition Definition :
       {NEVERC_OBJECT_SYMBOL_DEFINITION_UNDEFINED,
        NEVERC_OBJECT_SYMBOL_DEFINITION_ABSOLUTE}) {
    auto Graph = MakeGraph(Definition, NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL, 1);
    ASSERT_NE(Graph, nullptr);
    ASSERT_FALSE(verifyPluginObjectGraph(*Graph));
    const uint64_t Generation = Graph->generation();
    const size_t SectionCount = Graph->sectionCount();
    const size_t SymbolCount = Graph->symbolCount();
    const std::string Snapshot = dumpPluginObjectGraph(*Graph);

    AndroidKernelModuleFinalizationPolicy Policy;
    Policy.StripUnneededSymbols = true;
    Error Finalize = finalizeAndroidKernelModuleObjectGraph(
        *Graph, Policy, "test writer-unrepresentable symbol size");
    ASSERT_TRUE(static_cast<bool>(Finalize));
    EXPECT_NE(errorText(std::move(Finalize)).find("nonzero size"),
              std::string::npos);
    EXPECT_EQ(Graph->generation(), Generation);
    EXPECT_EQ(Graph->sectionCount(), SectionCount);
    EXPECT_EQ(Graph->symbolCount(), SymbolCount);
    EXPECT_EQ(dumpPluginObjectGraph(*Graph), Snapshot);
  }

  for (NevercObjectSymbolDefinition Definition :
       {NEVERC_OBJECT_SYMBOL_DEFINITION_UNDEFINED,
        NEVERC_OBJECT_SYMBOL_DEFINITION_ABSOLUTE}) {
    auto Graph = MakeGraph(Definition, NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL, 0);
    ASSERT_NE(Graph, nullptr);
    ASSERT_FALSE(verifyPluginObjectGraph(*Graph));
    AndroidKernelModuleFinalizationPolicy Policy;
    Policy.StripUnneededSymbols = true;
    Error Finalize = finalizeAndroidKernelModuleObjectGraph(
        *Graph, Policy, "test writer-representable zero symbol size");
    ASSERT_FALSE(Finalize) << errorText(std::move(Finalize));
    Error Audit = verifyFinalAndroidKernelModuleObjectGraph(
        *Graph, Policy, "test writer-representable zero symbol size audit");
    ASSERT_FALSE(Audit) << errorText(std::move(Audit));
  }

  auto WeakUndefined = MakeGraph(NEVERC_OBJECT_SYMBOL_DEFINITION_UNDEFINED,
                                 NEVERC_OBJECT_SYMBOL_BINDING_WEAK, 0);
  ASSERT_NE(WeakUndefined, nullptr);
  AndroidKernelModuleFinalizationPolicy Policy;
  Policy.StripUnneededSymbols = true;
  Error WeakFinalize = finalizeAndroidKernelModuleObjectGraph(
      *WeakUndefined, Policy, "test writer-representable weak import");
  ASSERT_FALSE(WeakFinalize) << errorText(std::move(WeakFinalize));
  Error WeakAudit = verifyFinalAndroidKernelModuleObjectGraph(
      *WeakUndefined, Policy, "test writer-representable weak import audit");
  ASSERT_FALSE(WeakAudit) << errorText(std::move(WeakAudit));

  auto LocalUndefined = MakeGraph(NEVERC_OBJECT_SYMBOL_DEFINITION_UNDEFINED,
                                  NEVERC_OBJECT_SYMBOL_BINDING_LOCAL, 0);
  ASSERT_NE(LocalUndefined, nullptr);
  ASSERT_FALSE(verifyPluginObjectGraph(*LocalUndefined));
  const uint64_t LocalGeneration = LocalUndefined->generation();
  const std::string LocalSnapshot = dumpPluginObjectGraph(*LocalUndefined);
  Error LocalFinalize = finalizeAndroidKernelModuleObjectGraph(
      *LocalUndefined, Policy, "test writer-lossy local import");
  ASSERT_TRUE(static_cast<bool>(LocalFinalize));
  EXPECT_NE(errorText(std::move(LocalFinalize)).find("LOCAL binding"),
            std::string::npos);
  EXPECT_EQ(LocalUndefined->generation(), LocalGeneration);
  EXPECT_EQ(dumpPluginObjectGraph(*LocalUndefined), LocalSnapshot);
}

TEST(AndroidKernelModuleFinalizerTest,
     RejectsRetainedSymbolOnlyComdatBeforeMutationAndSink) {
  initializeBuiltinTargets();
  LinkTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());

  const BuiltinTargetRoute *ELFRoute = nullptr;
  for (const BuiltinTargetRoute &Route : builtinTargetRoutes()) {
    const Triple Parsed(Triple::normalize(Route.CanonicalTriple));
    if (Route.SupportsObject &&
        Route.ObjectFormat == BuiltinObjectFormat::ELF &&
        Parsed.getArch() == Triple::aarch64) {
      ELFRoute = &Route;
      break;
    }
  }
  ASSERT_NE(ELFRoute, nullptr);

  const auto MakeGraph = [&] {
    auto Graph = makeBuiltinObject(*ELFRoute, "symbol_only_comdat");
    if (!Graph)
      return Graph;
    PluginObjectComdat Comdat;
    Comdat.ID = Graph->allocateEntityID();
    Comdat.Name = "symbol_only_group";
    Comdat.Selection = NEVERC_OBJECT_COMDAT_ANY;
    Graph->symbols().front().ComdatID = Comdat.ID;
    Graph->comdats().push_back(std::move(Comdat));
    Graph->advanceGeneration();
    Graph->issueLayoutProof();
    return Graph;
  };

  auto FinalizedGraph = MakeGraph();
  ASSERT_NE(FinalizedGraph, nullptr);
  ASSERT_FALSE(verifyPluginObjectGraph(*FinalizedGraph));
  const uint64_t Generation = FinalizedGraph->generation();
  const std::string Before = dumpPluginObjectGraph(*FinalizedGraph);
  AndroidKernelModuleFinalizationPolicy Policy;
  Policy.StripUnneededSymbols = true;
  Error Finalize = finalizeAndroidKernelModuleObjectGraph(
      *FinalizedGraph, Policy, "test retained symbol-only COMDAT");
  ASSERT_TRUE(static_cast<bool>(Finalize));
  EXPECT_NE(errorText(std::move(Finalize)).find("COMDAT metadata"),
            std::string::npos);
  EXPECT_EQ(FinalizedGraph->generation(), Generation);
  EXPECT_EQ(dumpPluginObjectGraph(*FinalizedGraph), Before);

  auto DirectGraph = MakeGraph();
  ASSERT_NE(DirectGraph, nullptr);
  auto Snapshot = PluginTargetRegistry::freeze(
      ArrayRef<PluginTargetRegistrationView>(), PluginTargetRequest{});
  ASSERT_TRUE(static_cast<bool>(Snapshot)) << errorText(Snapshot.takeError());
  auto Writer = ObjectWriterProvider::create(*Snapshot);
  ASSERT_TRUE(static_cast<bool>(Writer)) << errorText(Writer.takeError());
  ObjectOutputDestination Destination = ObjectOutputDestination::memory(
      "direct-symbol-only-comdat.ko", UINT64_C(1) << 20);
  Destination.WritePolicy = ObjectWritePolicy::AndroidKernelRelease;
  auto Rejected =
      (*Writer)->beginWrite(Scope.task(), *DirectGraph, Destination);
  ASSERT_FALSE(static_cast<bool>(Rejected));
  EXPECT_NE(errorText(Rejected.takeError()).find("COMDAT metadata"),
            std::string::npos);
  EXPECT_FALSE(
      findPluginMemoryOutput(Scope.task(), Destination.Name).has_value());

  DirectGraph->symbols().front().ComdatID = 0;
  DirectGraph->comdats().clear();
  DirectGraph->advanceGeneration();
  DirectGraph->issueLayoutProof();
  auto Image = (*Writer)->beginWrite(Scope.task(), *DirectGraph, Destination);
  ASSERT_TRUE(static_cast<bool>(Image)) << errorText(Image.takeError());
  auto Pending = (*Image)->pendingBytes();
  ASSERT_TRUE(static_cast<bool>(Pending)) << errorText(Pending.takeError());
  EXPECT_FALSE(Pending->empty());
  EXPECT_FALSE((*Image)->abort());
}

TEST(AndroidKernelModuleFinalizerTest,
     DirectReleaseWriterRejectsMalformedSymbolFactsBeforeOpeningSink) {
  initializeBuiltinTargets();
  LinkTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());

  const BuiltinTargetRoute *ELFRoute = nullptr;
  for (const BuiltinTargetRoute &Route : builtinTargetRoutes()) {
    const Triple Parsed(Triple::normalize(Route.CanonicalTriple));
    if (Route.SupportsObject &&
        Route.ObjectFormat == BuiltinObjectFormat::ELF &&
        Parsed.getArch() == Triple::aarch64) {
      ELFRoute = &Route;
      break;
    }
  }
  ASSERT_NE(ELFRoute, nullptr);

  auto Graph = makeBuiltinObject(*ELFRoute, "direct_release_symbol");
  ASSERT_NE(Graph, nullptr);
  PluginObjectSymbol &Symbol = Graph->symbols().front();
  Symbol.Extension.Owner = ELFRoute->ObjectFormatID;
  Symbol.Extension.Version = neverc::plugin::builtinext::SymbolVersion;
  SmallVector<uint8_t, 48> NativeFacts;
  neverc::plugin::builtinext::appendHeader(
      NativeFacts, neverc::plugin::builtinext::SymbolTag,
      neverc::plugin::builtinext::SymbolVersion);
  neverc::plugin::builtinext::appendU64(NativeFacts, ELF::STT_FUNC);
  neverc::plugin::builtinext::appendU64(NativeFacts, ELF::STB_GLOBAL);
  neverc::plugin::builtinext::appendU64(NativeFacts, ELF::STV_DEFAULT);
  Symbol.Extension.Bytes.assign(NativeFacts.begin(), NativeFacts.end());

  Graph->sections().front().Data.resize(8);
  PluginObjectRelocation Relocation;
  Relocation.ID = Graph->allocateEntityID();
  Relocation.SectionID = Graph->sections().front().ID;
  Relocation.Kind = NEVERC_OBJECT_RELOCATION_ABSOLUTE;
  Relocation.TargetKind = NEVERC_OBJECT_RELOCATION_TARGET_SYMBOL;
  Relocation.Width = 64;
  Relocation.TargetSymbolID = Symbol.ID;
  Relocation.Extension.Owner = ELFRoute->ObjectFormatID;
  Relocation.Extension.Version = neverc::plugin::builtinext::RelocationVersion;
  const SmallVector<uint8_t, 80> RelocationFacts =
      makeELFRelocationExtension(neverc::plugin::builtinext::RelocationVersion,
                                 ELF::R_AARCH64_ABS64, "R_AARCH64_ABS64");
  Relocation.Extension.Bytes.assign(RelocationFacts.begin(),
                                    RelocationFacts.end());
  Graph->relocations().push_back(std::move(Relocation));
  Graph->advanceGeneration();
  Graph->issueLayoutProof();
  ASSERT_FALSE(verifyPluginObjectGraph(*Graph));

  auto Snapshot = PluginTargetRegistry::freeze(
      ArrayRef<PluginTargetRegistrationView>(), PluginTargetRequest{});
  ASSERT_TRUE(static_cast<bool>(Snapshot)) << errorText(Snapshot.takeError());
  auto Writer = ObjectWriterProvider::create(*Snapshot);
  ASSERT_TRUE(static_cast<bool>(Writer)) << errorText(Writer.takeError());

  ObjectOutputDestination Destination = ObjectOutputDestination::memory(
      "direct-malformed-release.ko", UINT64_C(1) << 20);
  Destination.WritePolicy = ObjectWritePolicy::AndroidKernelRelease;
  auto Rejected = (*Writer)->beginWrite(Scope.task(), *Graph, Destination);
  ASSERT_FALSE(static_cast<bool>(Rejected));
  EXPECT_NE(errorText(Rejected.takeError()).find("exact version-2 payload"),
            std::string::npos);
  EXPECT_FALSE(
      findPluginMemoryOutput(Scope.task(), Destination.Name).has_value());

  // Reuse the exact destination after repairing the graph. Success proves the
  // rejected request never opened (and therefore never reserved) its sink.
  neverc::plugin::builtinext::appendU64(NativeFacts, Symbol.Size);
  neverc::plugin::builtinext::appendU64(
      NativeFacts, neverc::plugin::builtinext::SymbolNameNonEmpty);
  Symbol.Extension.Bytes.assign(NativeFacts.begin(), NativeFacts.end());
  Graph->advanceGeneration();
  Graph->issueLayoutProof();
  auto Image = (*Writer)->beginWrite(Scope.task(), *Graph, Destination);
  ASSERT_TRUE(static_cast<bool>(Image)) << errorText(Image.takeError());
  auto Pending = (*Image)->pendingBytes();
  ASSERT_TRUE(static_cast<bool>(Pending)) << errorText(Pending.takeError());
  EXPECT_FALSE(Pending->empty());
  EXPECT_FALSE((*Image)->abort());
}

TEST(AndroidKernelModuleFinalizerTest,
     DirectReleaseWriterRejectsSectionAndRelocationLossBeforeOpeningSink) {
  initializeBuiltinTargets();
  LinkTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());

  const BuiltinTargetRoute *ELFRoute = nullptr;
  for (const BuiltinTargetRoute &Route : builtinTargetRoutes()) {
    const Triple Parsed(Triple::normalize(Route.CanonicalTriple));
    if (Route.SupportsObject &&
        Route.ObjectFormat == BuiltinObjectFormat::ELF &&
        Parsed.getArch() == Triple::aarch64) {
      ELFRoute = &Route;
      break;
    }
  }
  ASSERT_NE(ELFRoute, nullptr);

  auto Graph = makeBuiltinObject(*ELFRoute, "direct_release_definition");
  ASSERT_NE(Graph, nullptr);
  PluginObjectSection &Text = Graph->sections().front();
  Text.Data.resize(8);
  Text.Extension.Owner = ELFRoute->ObjectFormatID;
  Text.Extension.Version = neverc::plugin::builtinext::SectionVersion;
  const uint64_t TextFlags = ELF::SHF_ALLOC | ELF::SHF_EXECINSTR;
  const SmallVector<uint8_t, 64> LossySectionFacts = makeELFSectionExtension(
      neverc::plugin::builtinext::SectionVersion, ELF::SHT_NOTE, TextFlags, 0);
  Text.Extension.Bytes.assign(LossySectionFacts.begin(),
                              LossySectionFacts.end());

  PluginObjectSymbol Import;
  Import.ID = Graph->allocateEntityID();
  Import.Name = "direct_release_import";
  Import.Binding = NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL;
  Import.Definition = NEVERC_OBJECT_SYMBOL_DEFINITION_UNDEFINED;
  Import.Flags = NEVERC_OBJECT_SYMBOL_IMPORTED;
  const uint64_t ImportID = Import.ID;
  Graph->symbols().push_back(std::move(Import));

  PluginObjectRelocation Relocation;
  Relocation.ID = Graph->allocateEntityID();
  Relocation.SectionID = Text.ID;
  Relocation.Kind = NEVERC_OBJECT_RELOCATION_ABSOLUTE;
  Relocation.TargetKind = NEVERC_OBJECT_RELOCATION_TARGET_SYMBOL;
  Relocation.Width = 64;
  Relocation.TargetSymbolID = ImportID;
  Relocation.Extension.Owner = ELFRoute->ObjectFormatID;
  Relocation.Extension.Version = neverc::plugin::builtinext::RelocationVersion;
  const SmallVector<uint8_t, 80> LossyRelocationFacts =
      makeELFRelocationExtension(neverc::plugin::builtinext::RelocationVersion,
                                 ELF::R_AARCH64_ABS64, "R_AARCH64_ABS32");
  Relocation.Extension.Bytes.assign(LossyRelocationFacts.begin(),
                                    LossyRelocationFacts.end());
  const uint64_t RelocationID = Relocation.ID;
  Graph->relocations().push_back(std::move(Relocation));
  Graph->advanceGeneration();
  Graph->issueLayoutProof();
  ASSERT_FALSE(verifyPluginObjectGraph(*Graph));

  auto Snapshot = PluginTargetRegistry::freeze(
      ArrayRef<PluginTargetRegistrationView>(), PluginTargetRequest{});
  ASSERT_TRUE(static_cast<bool>(Snapshot)) << errorText(Snapshot.takeError());
  auto Writer = ObjectWriterProvider::create(*Snapshot);
  ASSERT_TRUE(static_cast<bool>(Writer)) << errorText(Writer.takeError());
  ObjectOutputDestination Destination = ObjectOutputDestination::memory(
      "direct-lossy-native-facts.ko", UINT64_C(1) << 20);
  Destination.WritePolicy = ObjectWritePolicy::AndroidKernelRelease;

  auto RejectedSection =
      (*Writer)->beginWrite(Scope.task(), *Graph, Destination);
  ASSERT_FALSE(static_cast<bool>(RejectedSection));
  EXPECT_NE(errorText(RejectedSection.takeError()).find("native section type"),
            std::string::npos);
  EXPECT_FALSE(
      findPluginMemoryOutput(Scope.task(), Destination.Name).has_value());

  const SmallVector<uint8_t, 64> ValidSectionFacts =
      makeELFSectionExtension(neverc::plugin::builtinext::SectionVersion,
                              ELF::SHT_PROGBITS, TextFlags, 0);
  Text.Extension.Bytes.assign(ValidSectionFacts.begin(),
                              ValidSectionFacts.end());
  Graph->advanceGeneration();
  Graph->issueLayoutProof();
  auto RejectedRelocation =
      (*Writer)->beginWrite(Scope.task(), *Graph, Destination);
  ASSERT_FALSE(static_cast<bool>(RejectedRelocation));
  EXPECT_NE(errorText(RejectedRelocation.takeError())
                .find("official relocation name"),
            std::string::npos);
  EXPECT_FALSE(
      findPluginMemoryOutput(Scope.task(), Destination.Name).has_value());

  PluginObjectRelocation *FinalRelocation = Graph->findRelocation(RelocationID);
  ASSERT_NE(FinalRelocation, nullptr);
  const SmallVector<uint8_t, 80> ValidRelocationFacts =
      makeELFRelocationExtension(neverc::plugin::builtinext::RelocationVersion,
                                 ELF::R_AARCH64_ABS64, "R_AARCH64_ABS64");
  FinalRelocation->Extension.Bytes.assign(ValidRelocationFacts.begin(),
                                          ValidRelocationFacts.end());
  Graph->advanceGeneration();
  Graph->issueLayoutProof();
  auto Image = (*Writer)->beginWrite(Scope.task(), *Graph, Destination);
  ASSERT_TRUE(static_cast<bool>(Image)) << errorText(Image.takeError());
  auto Pending = (*Image)->pendingBytes();
  ASSERT_TRUE(static_cast<bool>(Pending)) << errorText(Pending.takeError());
  EXPECT_FALSE(Pending->empty());
  EXPECT_FALSE((*Image)->abort());
}

TEST(AndroidKernelModuleFinalizerTest,
     DemotedPCGNativeSymbolFactsRemainWriterConsistent) {
  initializeBuiltinTargets();
  LinkTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());
  const BuiltinTargetRoute *ELFRoute = nullptr;
  for (const BuiltinTargetRoute &Route : builtinTargetRoutes()) {
    const Triple Parsed(Triple::normalize(Route.CanonicalTriple));
    if (Route.SupportsObject &&
        Route.ObjectFormat == BuiltinObjectFormat::ELF &&
        Parsed.getArch() == Triple::aarch64) {
      ELFRoute = &Route;
      break;
    }
  }
  ASSERT_NE(ELFRoute, nullptr);

  auto Graph = makeBuiltinObject(*ELFRoute, "pcg_entry.__pcg1234");
  ASSERT_NE(Graph, nullptr);
  PluginObjectSymbol &Symbol = Graph->symbols().front();
  Symbol.Flags = NEVERC_OBJECT_SYMBOL_EXPORTED;
  Symbol.Extension.Owner = ELFRoute->ObjectFormatID;
  Symbol.Extension.Version = neverc::plugin::builtinext::SymbolVersion;
  const SmallVector<uint8_t, 48> NativeFacts = makeELFSymbolExtension(
      neverc::plugin::builtinext::SymbolVersion, ELF::STT_FUNC, ELF::STB_GLOBAL,
      ELF::STV_DEFAULT, Symbol.Size);
  Symbol.Extension.Bytes.assign(NativeFacts.begin(), NativeFacts.end());

  Graph->sections().front().Data.resize(8);
  PluginObjectRelocation RequiredReference;
  RequiredReference.ID = Graph->allocateEntityID();
  RequiredReference.SectionID = Graph->sections().front().ID;
  RequiredReference.Kind = NEVERC_OBJECT_RELOCATION_ABSOLUTE;
  RequiredReference.TargetKind = NEVERC_OBJECT_RELOCATION_TARGET_SYMBOL;
  RequiredReference.Width = 64;
  RequiredReference.TargetSymbolID = Symbol.ID;
  RequiredReference.Extension.Owner = ELFRoute->ObjectFormatID;
  RequiredReference.Extension.Version =
      neverc::plugin::builtinext::RelocationVersion;
  const SmallVector<uint8_t, 80> RequiredReferenceFacts =
      makeELFRelocationExtension(neverc::plugin::builtinext::RelocationVersion,
                                 ELF::R_AARCH64_ABS64, "R_AARCH64_ABS64");
  RequiredReference.Extension.Bytes.assign(RequiredReferenceFacts.begin(),
                                           RequiredReferenceFacts.end());
  Graph->relocations().push_back(std::move(RequiredReference));

  AndroidKernelModuleFinalizationPolicy Policy;
  Policy.StripUnneededSymbols = true;
  Error Finalize = finalizeAndroidKernelModuleObjectGraph(
      *Graph, Policy, "test PCG native binding demotion");
  ASSERT_FALSE(Finalize) << errorText(std::move(Finalize));
  ASSERT_EQ(Graph->symbolCount(), 1U);
  const PluginObjectSymbol &Demoted = Graph->symbols().front();
  EXPECT_EQ(Demoted.Binding, NEVERC_OBJECT_SYMBOL_BINDING_LOCAL);
  EXPECT_EQ(Demoted.Flags & NEVERC_OBJECT_SYMBOL_EXPORTED, 0U);
  const std::optional<uint64_t> ExtendedBinding =
      neverc::plugin::builtinext::field(
          Demoted.Extension.Bytes, neverc::plugin::builtinext::SymbolBinding);
  ASSERT_TRUE(ExtendedBinding.has_value());
  EXPECT_EQ(*ExtendedBinding, ELF::STB_LOCAL);
  EXPECT_NE(Demoted.Name, "pcg_entry.__pcg1234");
  EXPECT_TRUE(neverc::hasCanonicalReleaseNameShape(Demoted.Name));

  Graph->issueLayoutProof();
  auto Snapshot = PluginTargetRegistry::freeze(
      ArrayRef<PluginTargetRegistrationView>(), PluginTargetRequest{});
  ASSERT_TRUE(static_cast<bool>(Snapshot)) << errorText(Snapshot.takeError());
  auto Writer = ObjectWriterProvider::create(*Snapshot);
  ASSERT_TRUE(static_cast<bool>(Writer)) << errorText(Writer.takeError());
  ObjectOutputDestination Destination = ObjectOutputDestination::memory(
      "pcg-native-binding-release.ko", UINT64_C(1) << 20);
  Destination.WritePolicy = ObjectWritePolicy::AndroidKernelRelease;
  auto Image = (*Writer)->beginWrite(Scope.task(), *Graph, Destination);
  ASSERT_TRUE(static_cast<bool>(Image)) << errorText(Image.takeError());
  auto Bytes = (*Image)->pendingBytes();
  ASSERT_TRUE(static_cast<bool>(Bytes)) << errorText(Bytes.takeError());
  auto Semantics = readELFSemantics(*Bytes);
  ASSERT_TRUE(static_cast<bool>(Semantics)) << errorText(Semantics.takeError());
  // MC may fold a local defined relocation target into its section symbol.
  // That is semantically equivalent and intentionally omits even the
  // canonical local PCG spelling from the serialized table.
  EXPECT_EQ(llvm::find_if(Semantics->Symbols,
                          [&](const ELFSymbolSemantics &Candidate) {
                            return Candidate.Name == Demoted.Name;
                          }),
            Semantics->Symbols.end());
  EXPECT_EQ(llvm::find_if(Semantics->Symbols,
                          [&](const ELFSymbolSemantics &Candidate) {
                            return Candidate.Name == "pcg_entry.__pcg1234";
                          }),
            Semantics->Symbols.end());
  EXPECT_FALSE(containsBytes(*Bytes, "pcg_entry.__pcg1234"));

  std::vector<const ELFSymbolSemantics *> NonMappingSymbols;
  for (const ELFSymbolSemantics &Candidate : Semantics->Symbols)
    if (!isAArch64MappingSymbol(Candidate.Name))
      NonMappingSymbols.push_back(&Candidate);
  ASSERT_EQ(NonMappingSymbols.size(), 2U);
  llvm::sort(NonMappingSymbols, [](const ELFSymbolSemantics *Left,
                                   const ELFSymbolSemantics *Right) {
    return Left->Name < Right->Name;
  });
  EXPECT_EQ(NonMappingSymbols[0]->Name, "__start_alloc_tags");
  EXPECT_EQ(NonMappingSymbols[1]->Name, "__stop_alloc_tags");
  for (const ELFSymbolSemantics *BoundarySymbol : NonMappingSymbols) {
    EXPECT_EQ(BoundarySymbol->Type, ELF::STT_NOTYPE);
    EXPECT_EQ(BoundarySymbol->Binding, ELF::STB_GLOBAL);
    EXPECT_EQ(BoundarySymbol->Section, ".codetag.alloc_tags");
    EXPECT_EQ(BoundarySymbol->Value, 0U);
  }
  ASSERT_EQ(Semantics->Relocations.size(), 1U);
  const ELFRelocationSemantics &SerializedRelocation =
      Semantics->Relocations.front();
  EXPECT_EQ(SerializedRelocation.Section, ".text");
  EXPECT_EQ(SerializedRelocation.Offset, 0U);
  EXPECT_EQ(SerializedRelocation.Type, ELF::R_AARCH64_ABS64);
  EXPECT_TRUE(SerializedRelocation.Target.empty());
  EXPECT_EQ(SerializedRelocation.Addend, 0);
  EXPECT_FALSE((*Image)->abort());
}

TEST(AndroidKernelModuleFinalizerTest,
     RejectsLossyNativeSectionExtensionBeforeMutation) {
  initializeBuiltinTargets();
  const BuiltinTargetRoute *ELFRoute = nullptr;
  for (const BuiltinTargetRoute &Route : builtinTargetRoutes()) {
    const Triple Parsed(Triple::normalize(Route.CanonicalTriple));
    if (Route.SupportsObject &&
        Route.ObjectFormat == BuiltinObjectFormat::ELF &&
        Parsed.getArch() == Triple::aarch64) {
      ELFRoute = &Route;
      break;
    }
  }
  ASSERT_NE(ELFRoute, nullptr);

  const uint64_t TextFlags = ELF::SHF_ALLOC | ELF::SHF_EXECINSTR;
  const auto ExpectAtomicRejection = [&](ArrayRef<uint8_t> NativeFacts,
                                         uint32_t OuterVersion,
                                         StringRef ExpectedReason,
                                         NevercObjectSectionFlags StableFlags =
                                             NEVERC_OBJECT_SECTION_ALLOCATED |
                                             NEVERC_OBJECT_SECTION_EXECUTABLE,
                                         bool WithComdat = false) {
    auto Graph = makeBuiltinObject(*ELFRoute, "section_extension_symbol");
    ASSERT_NE(Graph, nullptr);
    PluginObjectSection &Section = Graph->sections().front();
    Section.Flags = StableFlags;
    Section.Extension.Owner = ELFRoute->ObjectFormatID;
    Section.Extension.Version = OuterVersion;
    Section.Extension.Bytes.assign(NativeFacts.begin(), NativeFacts.end());
    if (WithComdat) {
      PluginObjectComdat Comdat;
      Comdat.ID = Graph->allocateEntityID();
      Comdat.Name = "section_extension_group";
      Comdat.Selection = NEVERC_OBJECT_COMDAT_ANY;
      Section.ComdatID = Comdat.ID;
      Graph->comdats().push_back(std::move(Comdat));
      Graph->symbols().front().ComdatID = Section.ComdatID;
    }
    addAndroidKernelProfileContract(*Graph);
    Graph->advanceGeneration();
    Graph->issueLayoutProof();
    ASSERT_FALSE(verifyPluginObjectGraph(*Graph));

    const uint64_t Generation = Graph->generation();
    const size_t SectionCount = Graph->sectionCount();
    const size_t SymbolCount = Graph->symbolCount();
    const size_t ComdatCount = Graph->comdatCount();
    const std::string Snapshot = dumpPluginObjectGraph(*Graph);
    AndroidKernelModuleFinalizationPolicy Policy;
    Policy.StripUnneededSymbols = true;
    Error Finalize = finalizeAndroidKernelModuleObjectGraph(
        *Graph, Policy, "test lossy native section extension");
    ASSERT_TRUE(static_cast<bool>(Finalize));
    EXPECT_NE(errorText(std::move(Finalize)).find(ExpectedReason.str()),
              std::string::npos);
    EXPECT_EQ(Graph->generation(), Generation);
    EXPECT_EQ(Graph->sectionCount(), SectionCount);
    EXPECT_EQ(Graph->symbolCount(), SymbolCount);
    EXPECT_EQ(Graph->comdatCount(), ComdatCount);
    EXPECT_EQ(dumpPluginObjectGraph(*Graph), Snapshot);
  };

  SmallVector<uint8_t, 64> Short =
      makeELFSectionExtension(neverc::plugin::builtinext::SectionVersion,
                              ELF::SHT_PROGBITS, TextFlags, 0);
  Short.pop_back();
  ExpectAtomicRejection(Short, neverc::plugin::builtinext::SectionVersion,
                        "exact version-2 payload");

  SmallVector<uint8_t, 64> Long =
      makeELFSectionExtension(neverc::plugin::builtinext::SectionVersion,
                              ELF::SHT_PROGBITS, TextFlags, 0);
  Long.push_back(0);
  ExpectAtomicRejection(Long, neverc::plugin::builtinext::SectionVersion,
                        "exact version-2 payload");

  SmallVector<uint8_t, 64> InnerV1 = makeELFSectionExtension(
      1, ELF::SHT_PROGBITS, TextFlags, 0, /*IncludeEntrySize=*/false);
  ExpectAtomicRejection(InnerV1, neverc::plugin::builtinext::SectionVersion,
                        "version metadata disagrees");

  ExpectAtomicRejection(
      makeELFSectionExtension(neverc::plugin::builtinext::SectionVersion,
                              ELF::SHT_NOTE, TextFlags, 0),
      neverc::plugin::builtinext::SectionVersion, "native section type");
  ExpectAtomicRejection(
      makeELFSectionExtension(neverc::plugin::builtinext::SectionVersion,
                              ELF::SHT_LLVM_ADDRSIG, TextFlags, 0),
      neverc::plugin::builtinext::SectionVersion, "native section type");
  ExpectAtomicRejection(
      makeELFSectionExtension(neverc::plugin::builtinext::SectionVersion,
                              ELF::SHT_PROGBITS,
                              TextFlags | ELF::SHF_LINK_ORDER, 0),
      neverc::plugin::builtinext::SectionVersion, "native section flags");
  ExpectAtomicRejection(
      makeELFSectionExtension(neverc::plugin::builtinext::SectionVersion,
                              ELF::SHT_PROGBITS, TextFlags | ELF::SHF_MERGE, 0),
      neverc::plugin::builtinext::SectionVersion, "nonzero entry size",
      NEVERC_OBJECT_SECTION_ALLOCATED | NEVERC_OBJECT_SECTION_EXECUTABLE |
          NEVERC_OBJECT_SECTION_MERGEABLE);
  ExpectAtomicRejection(
      makeELFSectionExtension(neverc::plugin::builtinext::SectionVersion,
                              ELF::SHT_PROGBITS, TextFlags | ELF::SHF_GROUP, 0),
      neverc::plugin::builtinext::SectionVersion, "COMDAT metadata",
      NEVERC_OBJECT_SECTION_ALLOCATED | NEVERC_OBJECT_SECTION_EXECUTABLE,
      /*WithComdat=*/true);

  auto Valid = makeBuiltinObject(*ELFRoute, "valid_section_extension_symbol");
  ASSERT_NE(Valid, nullptr);
  PluginObjectSection &ValidSection = Valid->sections().front();
  ValidSection.Extension.Owner = ELFRoute->ObjectFormatID;
  ValidSection.Extension.Version = neverc::plugin::builtinext::SectionVersion;
  const SmallVector<uint8_t, 64> ValidFacts =
      makeELFSectionExtension(neverc::plugin::builtinext::SectionVersion,
                              ELF::SHT_PROGBITS, TextFlags, 0);
  ValidSection.Extension.Bytes.assign(ValidFacts.begin(), ValidFacts.end());
  AndroidKernelModuleFinalizationPolicy Policy;
  Policy.StripUnneededSymbols = true;
  Error Finalize = finalizeAndroidKernelModuleObjectGraph(
      *Valid, Policy, "test lossless native section extension");
  ASSERT_FALSE(Finalize) << errorText(std::move(Finalize));

  auto ValidV1 = makeBuiltinObject(*ELFRoute, "valid_v1_section_extension");
  ASSERT_NE(ValidV1, nullptr);
  PluginObjectSection &ValidV1Section = ValidV1->sections().front();
  ValidV1Section.Extension.Owner = ELFRoute->ObjectFormatID;
  ValidV1Section.Extension.Version = 1;
  const SmallVector<uint8_t, 64> ValidV1Facts = makeELFSectionExtension(
      1, ELF::SHT_PROGBITS, TextFlags, 0, /*IncludeEntrySize=*/false);
  ValidV1Section.Extension.Bytes.assign(ValidV1Facts.begin(),
                                        ValidV1Facts.end());
  Error FinalizeV1 = finalizeAndroidKernelModuleObjectGraph(
      *ValidV1, Policy, "test lossless version-1 native section extension");
  ASSERT_FALSE(FinalizeV1) << errorText(std::move(FinalizeV1));
}

TEST(AndroidKernelModuleFinalizerTest,
     RejectsContradictoryNativeRelocationExtensionBeforeMutation) {
  initializeBuiltinTargets();
  const BuiltinTargetRoute *ELFRoute = nullptr;
  for (const BuiltinTargetRoute &Route : builtinTargetRoutes()) {
    const Triple Parsed(Triple::normalize(Route.CanonicalTriple));
    if (Route.SupportsObject &&
        Route.ObjectFormat == BuiltinObjectFormat::ELF &&
        Parsed.getArch() == Triple::aarch64) {
      ELFRoute = &Route;
      break;
    }
  }
  ASSERT_NE(ELFRoute, nullptr);

  const auto MakeGraph = [&](ArrayRef<uint8_t> NativeFacts,
                             uint32_t OuterVersion,
                             NevercObjectRelocationKind Kind, uint32_t Width,
                             bool PCRelative, bool Signed) {
    auto Graph = makeBuiltinObject(*ELFRoute, "relocation_source");
    if (!Graph)
      return Graph;
    PluginObjectSection &Text = Graph->sections().front();
    Text.Data.resize(8);

    PluginObjectSymbol Import;
    Import.ID = Graph->allocateEntityID();
    Import.Name = "relocation_import";
    Import.Binding = NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL;
    Import.Definition = NEVERC_OBJECT_SYMBOL_DEFINITION_UNDEFINED;
    Import.Flags = NEVERC_OBJECT_SYMBOL_IMPORTED;
    const uint64_t ImportID = Import.ID;
    Graph->symbols().push_back(std::move(Import));

    PluginObjectRelocation Relocation;
    Relocation.ID = Graph->allocateEntityID();
    Relocation.SectionID = Text.ID;
    Relocation.Kind = Kind;
    Relocation.TargetKind = NEVERC_OBJECT_RELOCATION_TARGET_SYMBOL;
    Relocation.Width = Width;
    Relocation.IsPCRelative = PCRelative;
    Relocation.IsSigned = Signed;
    Relocation.TargetSymbolID = ImportID;
    Relocation.Extension.Owner = ELFRoute->ObjectFormatID;
    Relocation.Extension.Version = OuterVersion;
    Relocation.Extension.Bytes.assign(NativeFacts.begin(), NativeFacts.end());
    Graph->relocations().push_back(std::move(Relocation));
    addAndroidKernelProfileContract(*Graph);
    Graph->advanceGeneration();
    Graph->issueLayoutProof();
    return Graph;
  };
  const auto ExpectAtomicRejection =
      [&](ArrayRef<uint8_t> NativeFacts, StringRef ExpectedReason,
          NevercObjectRelocationKind Kind = NEVERC_OBJECT_RELOCATION_ABSOLUTE,
          uint32_t Width = 64, bool PCRelative = false, bool Signed = false,
          uint32_t OuterVersion =
              neverc::plugin::builtinext::RelocationVersion) {
        auto Graph = MakeGraph(NativeFacts, OuterVersion, Kind, Width,
                               PCRelative, Signed);
        ASSERT_NE(Graph, nullptr);
        ASSERT_FALSE(verifyPluginObjectGraph(*Graph));
        const uint64_t Generation = Graph->generation();
        const size_t SectionCount = Graph->sectionCount();
        const size_t SymbolCount = Graph->symbolCount();
        const size_t RelocationCount = Graph->relocationCount();
        const std::string Snapshot = dumpPluginObjectGraph(*Graph);

        AndroidKernelModuleFinalizationPolicy Policy;
        Policy.StripUnneededSymbols = true;
        Error Finalize = finalizeAndroidKernelModuleObjectGraph(
            *Graph, Policy, "test contradictory native relocation extension");
        ASSERT_TRUE(static_cast<bool>(Finalize));
        EXPECT_NE(errorText(std::move(Finalize)).find(ExpectedReason.str()),
                  std::string::npos);
        EXPECT_EQ(Graph->generation(), Generation);
        EXPECT_EQ(Graph->sectionCount(), SectionCount);
        EXPECT_EQ(Graph->symbolCount(), SymbolCount);
        EXPECT_EQ(Graph->relocationCount(), RelocationCount);
        EXPECT_EQ(dumpPluginObjectGraph(*Graph), Snapshot);
      };

  SmallVector<uint8_t, 80> Short =
      makeELFRelocationExtension(neverc::plugin::builtinext::RelocationVersion,
                                 ELF::R_AARCH64_ABS64, "R_AARCH64_ABS64");
  Short.pop_back();
  ExpectAtomicRejection(Short, "exact version-1 payload");

  SmallVector<uint8_t, 80> Long =
      makeELFRelocationExtension(neverc::plugin::builtinext::RelocationVersion,
                                 ELF::R_AARCH64_ABS64, "R_AARCH64_ABS64");
  Long.push_back(0);
  ExpectAtomicRejection(Long, "exact version-1 payload");

  ExpectAtomicRejection(
      makeELFRelocationExtension(0, ELF::R_AARCH64_ABS64, "R_AARCH64_ABS64"),
      "version metadata disagrees");
  ExpectAtomicRejection(
      makeELFRelocationExtension(neverc::plugin::builtinext::RelocationVersion,
                                 ELF::R_AARCH64_ABS64, "R_AARCH64_ABS32"),
      "official relocation name");
  ExpectAtomicRejection(
      makeELFRelocationExtension(neverc::plugin::builtinext::RelocationVersion,
                                 ELF::R_AARCH64_TLS_DTPMOD64,
                                 "R_AARCH64_TLS_DTPMOD64"),
      "Android AArch64 module loader", NEVERC_OBJECT_RELOCATION_TLS, 64, false,
      false);

  const SmallVector<uint8_t, 80> ABS64 =
      makeELFRelocationExtension(neverc::plugin::builtinext::RelocationVersion,
                                 ELF::R_AARCH64_ABS64, "R_AARCH64_ABS64");
  ExpectAtomicRejection(ABS64, "stable relocation facts",
                        NEVERC_OBJECT_RELOCATION_GOT_RELATIVE, 64, false,
                        false);
  ExpectAtomicRejection(ABS64, "stable relocation facts",
                        NEVERC_OBJECT_RELOCATION_ABSOLUTE, 32, false, false);
  ExpectAtomicRejection(ABS64, "stable relocation facts",
                        NEVERC_OBJECT_RELOCATION_ABSOLUTE, 64, true, false);
  ExpectAtomicRejection(ABS64, "stable relocation facts",
                        NEVERC_OBJECT_RELOCATION_ABSOLUTE, 64, false, true);

  auto Valid = MakeGraph(ABS64, neverc::plugin::builtinext::RelocationVersion,
                         NEVERC_OBJECT_RELOCATION_ABSOLUTE, 64, false, false);
  ASSERT_NE(Valid, nullptr);
  ASSERT_FALSE(verifyPluginObjectGraph(*Valid));
  AndroidKernelModuleFinalizationPolicy Policy;
  Policy.StripUnneededSymbols = true;
  Error Finalize = finalizeAndroidKernelModuleObjectGraph(
      *Valid, Policy, "test lossless native relocation extension");
  ASSERT_FALSE(Finalize) << errorText(std::move(Finalize));
}

TEST(AndroidKernelModuleFinalizerTest,
     IgnoresUnrepresentableNativeFactsOwnedOnlyByDroppedDebugSection) {
  initializeBuiltinTargets();
  const BuiltinTargetRoute *ELFRoute = nullptr;
  for (const BuiltinTargetRoute &Route : builtinTargetRoutes()) {
    const Triple Parsed(Triple::normalize(Route.CanonicalTriple));
    if (Route.SupportsObject &&
        Route.ObjectFormat == BuiltinObjectFormat::ELF &&
        Parsed.getArch() == Triple::aarch64) {
      ELFRoute = &Route;
      break;
    }
  }
  ASSERT_NE(ELFRoute, nullptr);

  auto Graph = makeBuiltinObject(*ELFRoute, "retained_debug_reference");
  ASSERT_NE(Graph, nullptr);
  Graph->sections().front().Data.resize(8);
  const uint64_t RetainedSymbolID = Graph->symbols().front().ID;

  PluginObjectSection Debug;
  Debug.ID = Graph->allocateEntityID();
  Debug.Name = ".debug_info";
  Debug.Kind = NEVERC_OBJECT_SECTION_KIND_DEBUG;
  Debug.Flags = NEVERC_OBJECT_SECTION_DEBUG;
  Debug.Alignment = 1;
  Debug.Data.resize(8);
  Debug.Extension.Owner = ELFRoute->ObjectFormatID;
  Debug.Extension.Version = neverc::plugin::builtinext::SectionVersion;
  Debug.Extension.Bytes = {UINT8_C(0x62), UINT8_C(0x61), UINT8_C(0x64)};
  const uint64_t DebugSectionID = Debug.ID;
  Graph->sections().push_back(std::move(Debug));

  PluginObjectRelocation DebugRelocation;
  DebugRelocation.ID = Graph->allocateEntityID();
  DebugRelocation.SectionID = DebugSectionID;
  DebugRelocation.Kind = NEVERC_OBJECT_RELOCATION_ABSOLUTE;
  DebugRelocation.TargetKind = NEVERC_OBJECT_RELOCATION_TARGET_SYMBOL;
  DebugRelocation.Width = 64;
  DebugRelocation.TargetSymbolID = RetainedSymbolID;
  DebugRelocation.Extension.Owner = ELFRoute->ObjectFormatID;
  DebugRelocation.Extension.Version =
      neverc::plugin::builtinext::RelocationVersion;
  DebugRelocation.Extension.Bytes = {UINT8_C(0x62), UINT8_C(0x61),
                                     UINT8_C(0x64)};
  Graph->relocations().push_back(std::move(DebugRelocation));
  Graph->advanceGeneration();
  Graph->issueLayoutProof();
  ASSERT_FALSE(verifyPluginObjectGraph(*Graph));

  const uint64_t Generation = Graph->generation();
  AndroidKernelModuleFinalizationPolicy Policy;
  Policy.DropDebugInfo = true;
  Policy.StripUnneededSymbols = true;
  Error Finalize = finalizeAndroidKernelModuleObjectGraph(
      *Graph, Policy, "test dropped debug native facts");
  ASSERT_FALSE(Finalize) << errorText(std::move(Finalize));
  EXPECT_EQ(Graph->generation(), Generation + 1);
  EXPECT_EQ(Graph->sectionCount(), 1U);
  EXPECT_EQ(Graph->relocationCount(), 0U);
  EXPECT_EQ(Graph->sections().front().Name, ".text");
}

TEST(AndroidKernelModuleFinalizerTest,
     SerializedSectionRelocationPreservesTargetValue) {
  initializeBuiltinTargets();
  LinkTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());
  const BuiltinTargetRoute *ELFRoute = nullptr;
  for (const BuiltinTargetRoute &Route : builtinTargetRoutes()) {
    const Triple Parsed(Triple::normalize(Route.CanonicalTriple));
    if (Route.SupportsObject &&
        Route.ObjectFormat == BuiltinObjectFormat::ELF &&
        Parsed.getArch() == Triple::aarch64) {
      ELFRoute = &Route;
      break;
    }
  }
  ASSERT_NE(ELFRoute, nullptr);

  auto MakeGraph = [&](uint64_t TargetValue, int64_t Addend) {
    auto Graph = makeBuiltinObject(*ELFRoute, "section_target_source");
    if (!Graph)
      return Graph;
    PluginObjectSection &Text = Graph->sections().front();
    Text.Data.resize(8);
    PluginObjectSection Data;
    Data.ID = Graph->allocateEntityID();
    Data.Name = ".data";
    Data.Kind = NEVERC_OBJECT_SECTION_KIND_DATA;
    Data.Flags =
        NEVERC_OBJECT_SECTION_ALLOCATED | NEVERC_OBJECT_SECTION_WRITABLE;
    Data.Alignment = 8;
    Data.Data.resize(16);
    const uint64_t DataID = Data.ID;
    Graph->sections().push_back(std::move(Data));

    PluginObjectRelocation Relocation;
    Relocation.ID = Graph->allocateEntityID();
    Relocation.SectionID = Text.ID;
    Relocation.Kind = NEVERC_OBJECT_RELOCATION_ABSOLUTE;
    Relocation.TargetKind = NEVERC_OBJECT_RELOCATION_TARGET_SECTION;
    Relocation.Width = 64;
    Relocation.TargetSectionID = DataID;
    Relocation.TargetValue = TargetValue;
    Relocation.Addend = Addend;
    Relocation.Extension.Owner = ELFRoute->ObjectFormatID;
    Relocation.Extension.Version =
        neverc::plugin::builtinext::RelocationVersion;
    const SmallVector<uint8_t, 80> NativeFacts = makeELFRelocationExtension(
        neverc::plugin::builtinext::RelocationVersion, ELF::R_AARCH64_ABS64,
        "R_AARCH64_ABS64");
    Relocation.Extension.Bytes.assign(NativeFacts.begin(), NativeFacts.end());
    Graph->relocations().push_back(std::move(Relocation));
    Graph->advanceGeneration();
    Graph->issueLayoutProof();
    return Graph;
  };

  auto Graph = MakeGraph(4, 3);
  ASSERT_NE(Graph, nullptr);
  ASSERT_FALSE(verifyPluginObjectGraph(*Graph));
  auto Snapshot = PluginTargetRegistry::freeze(
      ArrayRef<PluginTargetRegistrationView>(), PluginTargetRequest{});
  ASSERT_TRUE(static_cast<bool>(Snapshot)) << errorText(Snapshot.takeError());
  auto Writer = ObjectWriterProvider::create(*Snapshot);
  ASSERT_TRUE(static_cast<bool>(Writer)) << errorText(Writer.takeError());
  ObjectOutputDestination Destination = ObjectOutputDestination::memory(
      "section-target-value.ko", UINT64_C(1) << 20);
  Destination.WritePolicy = ObjectWritePolicy::AndroidKernelRelease;
  auto Image = (*Writer)->beginWrite(Scope.task(), *Graph, Destination);
  ASSERT_TRUE(static_cast<bool>(Image)) << errorText(Image.takeError());
  auto Bytes = (*Image)->pendingBytes();
  ASSERT_TRUE(static_cast<bool>(Bytes)) << errorText(Bytes.takeError());
  auto Semantics = readELFSemantics(*Bytes);
  ASSERT_TRUE(static_cast<bool>(Semantics)) << errorText(Semantics.takeError());
  ASSERT_EQ(Semantics->Relocations.size(), 1U);
  EXPECT_EQ(Semantics->Relocations.front().Section, ".text");
  EXPECT_EQ(Semantics->Relocations.front().Type, ELF::R_AARCH64_ABS64);
  EXPECT_TRUE(Semantics->Relocations.front().Target.empty());
  EXPECT_EQ(Semantics->Relocations.front().Addend, 7);
  EXPECT_FALSE((*Image)->abort());

  auto Outside = MakeGraph(17, 0);
  ASSERT_NE(Outside, nullptr);
  const uint64_t OutsideGeneration = Outside->generation();
  const std::string OutsideSnapshot = dumpPluginObjectGraph(*Outside);
  AndroidKernelModuleFinalizationPolicy Policy;
  Policy.StripUnneededSymbols = true;
  Error OutsideFinalize = finalizeAndroidKernelModuleObjectGraph(
      *Outside, Policy, "test outside section target value");
  ASSERT_TRUE(static_cast<bool>(OutsideFinalize));
  EXPECT_NE(errorText(std::move(OutsideFinalize)).find("target section"),
            std::string::npos);
  EXPECT_EQ(Outside->generation(), OutsideGeneration);
  EXPECT_EQ(dumpPluginObjectGraph(*Outside), OutsideSnapshot);

  auto Overflow = MakeGraph(1, std::numeric_limits<int64_t>::max());
  ASSERT_NE(Overflow, nullptr);
  const uint64_t OverflowGeneration = Overflow->generation();
  const std::string OverflowSnapshot = dumpPluginObjectGraph(*Overflow);
  Error OverflowFinalize = finalizeAndroidKernelModuleObjectGraph(
      *Overflow, Policy, "test overflowing section target value");
  ASSERT_TRUE(static_cast<bool>(OverflowFinalize));
  EXPECT_NE(errorText(std::move(OverflowFinalize)).find("overflows"),
            std::string::npos);
  EXPECT_EQ(Overflow->generation(), OverflowGeneration);
  EXPECT_EQ(dumpPluginObjectGraph(*Overflow), OverflowSnapshot);
}

TEST(AndroidKernelModuleFinalizerTest,
     RejectsReservedNameCollisionWithoutMutation) {
  auto Graph = makeObject(1);
  ASSERT_NE(Graph, nullptr);
  PluginObjectSection &Text = Graph->sections().front();
  Text.Kind = NEVERC_OBJECT_SECTION_KIND_TEXT;
  Text.Flags =
      NEVERC_OBJECT_SECTION_ALLOCATED | NEVERC_OBJECT_SECTION_EXECUTABLE;
  Text.Data.resize(8);

  PluginObjectSymbol Function;
  Function.ID = Graph->allocateEntityID();
  Function.Name = "ordinary_function";
  Function.Binding = NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL;
  Function.Type = NEVERC_OBJECT_SYMBOL_TYPE_FUNCTION;
  Function.Definition = NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED;
  Function.SectionID = Text.ID;
  Function.Size = 1;
  Graph->symbols().push_back(std::move(Function));

  PluginObjectSymbol Import;
  Import.ID = Graph->allocateEntityID();
  Import.Name = "fn_0";
  Import.Binding = NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL;
  Import.Definition = NEVERC_OBJECT_SYMBOL_DEFINITION_UNDEFINED;
  const uint64_t ImportID = Import.ID;
  Graph->symbols().push_back(std::move(Import));

  PluginObjectRelocation Relocation;
  Relocation.ID = Graph->allocateEntityID();
  Relocation.SectionID = Text.ID;
  Relocation.Kind = NEVERC_OBJECT_RELOCATION_ABSOLUTE;
  Relocation.TargetKind = NEVERC_OBJECT_RELOCATION_TARGET_SYMBOL;
  Relocation.Width = 64;
  Relocation.TargetSymbolID = ImportID;
  Graph->relocations().push_back(std::move(Relocation));
  ASSERT_FALSE(verifyPluginObjectGraph(*Graph));

  const uint64_t Generation = Graph->generation();
  const std::string Snapshot = dumpPluginObjectGraph(*Graph);
  AndroidKernelModuleFinalizationPolicy Policy;
  Policy.StripUnneededSymbols = true;
  Error Finalize = finalizeAndroidKernelModuleObjectGraph(
      *Graph, Policy, "test reserved release name collision");
  ASSERT_TRUE(static_cast<bool>(Finalize));
  EXPECT_NE(errorText(std::move(Finalize)).find("collides with reserved"),
            std::string::npos);
  EXPECT_EQ(Graph->generation(), Generation);
  EXPECT_EQ(dumpPluginObjectGraph(*Graph), Snapshot);
}

TEST(AndroidKernelModuleFinalizerTest,
     RejectsUnrepresentablePlannedNameOwnershipWithoutMutation) {
  const auto ExpectAtomicRejection =
      [](std::unique_ptr<PluginObjectGraph> Graph, StringRef ExpectedReason) {
        ASSERT_NE(Graph, nullptr);
        Graph->advanceGeneration();
        Error Verify = verifyPluginObjectGraph(*Graph);
        ASSERT_FALSE(Verify) << errorText(std::move(Verify));
        const uint64_t Generation = Graph->generation();
        const size_t SectionCount = Graph->sectionCount();
        const size_t SymbolCount = Graph->symbolCount();
        const std::string Snapshot = dumpPluginObjectGraph(*Graph);

        AndroidKernelModuleFinalizationPolicy Policy;
        Policy.StripUnneededSymbols = true;
        Error Finalize = finalizeAndroidKernelModuleObjectGraph(
            *Graph, Policy, "test duplicate planned release name ownership");
        ASSERT_TRUE(static_cast<bool>(Finalize));
        EXPECT_NE(errorText(std::move(Finalize)).find(ExpectedReason.str()),
                  std::string::npos);
        EXPECT_EQ(Graph->generation(), Generation);
        EXPECT_EQ(Graph->sectionCount(), SectionCount);
        EXPECT_EQ(Graph->symbolCount(), SymbolCount);
        EXPECT_EQ(dumpPluginObjectGraph(*Graph), Snapshot);
      };

  auto DuplicateDefinitions = makeObject(1);
  ASSERT_NE(DuplicateDefinitions, nullptr);
  PluginObjectSection &Metadata = DuplicateDefinitions->sections().front();
  Metadata.Name = ".modinfo";
  Metadata.Data.resize(8);
  for (unsigned I = 0; I != 2; ++I) {
    PluginObjectSymbol Symbol;
    Symbol.ID = DuplicateDefinitions->allocateEntityID();
    Symbol.Name = "duplicate_metadata";
    Symbol.Binding = NEVERC_OBJECT_SYMBOL_BINDING_WEAK;
    Symbol.Type = NEVERC_OBJECT_SYMBOL_TYPE_OBJECT;
    Symbol.Definition = NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED;
    Symbol.SectionID = Metadata.ID;
    Symbol.Value = I * 4;
    Symbol.Size = 4;
    DuplicateDefinitions->symbols().push_back(std::move(Symbol));
  }
  addAndroidKernelProfileContract(*DuplicateDefinitions);
  ExpectAtomicRejection(std::move(DuplicateDefinitions),
                        "multiple retained symbols");

  auto ConflictingImports = makeObject(1);
  ASSERT_NE(ConflictingImports, nullptr);
  PluginObjectSection &Text = ConflictingImports->sections().front();
  Text.Name = ".text";
  Text.Kind = NEVERC_OBJECT_SECTION_KIND_TEXT;
  Text.Flags =
      NEVERC_OBJECT_SECTION_ALLOCATED | NEVERC_OBJECT_SECTION_EXECUTABLE;
  Text.Data.resize(16);
  for (NevercObjectSymbolBinding Binding :
       {NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL,
        NEVERC_OBJECT_SYMBOL_BINDING_WEAK}) {
    PluginObjectSymbol Import;
    Import.ID = ConflictingImports->allocateEntityID();
    Import.Name = "duplicate_import";
    Import.Binding = Binding;
    Import.Definition = NEVERC_OBJECT_SYMBOL_DEFINITION_UNDEFINED;
    Import.Flags = NEVERC_OBJECT_SYMBOL_IMPORTED;
    const uint64_t ImportID = Import.ID;
    ConflictingImports->symbols().push_back(std::move(Import));

    PluginObjectRelocation Relocation;
    Relocation.ID = ConflictingImports->allocateEntityID();
    Relocation.SectionID = Text.ID;
    Relocation.Offset = Binding == NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL ? 0 : 8;
    Relocation.Kind = NEVERC_OBJECT_RELOCATION_ABSOLUTE;
    Relocation.TargetKind = NEVERC_OBJECT_RELOCATION_TARGET_SYMBOL;
    Relocation.Width = 64;
    Relocation.TargetSymbolID = ImportID;
    ConflictingImports->relocations().push_back(std::move(Relocation));
  }
  ExpectAtomicRejection(std::move(ConflictingImports),
                        "different observable attributes");
}

TEST(AndroidKernelModuleFinalizerTest,
     CanonicalProvenanceRequiresReleaseStripWithoutMutation) {
  auto Graph = makeObject(1);
  ASSERT_NE(Graph, nullptr);
  const uint64_t Generation = Graph->generation();
  const std::string Snapshot = dumpPluginObjectGraph(*Graph);

  AndroidKernelModuleFinalizationPolicy Policy;
  Policy.SymbolNameState = AndroidKernelSymbolNameState::CanonicalRelease;
  Error Finalize = finalizeAndroidKernelModuleObjectGraph(
      *Graph, Policy, "test canonical provenance without release strip");
  ASSERT_TRUE(static_cast<bool>(Finalize));
  EXPECT_NE(errorText(std::move(Finalize)).find("requires the release"),
            std::string::npos);
  EXPECT_EQ(Graph->generation(), Generation);
  EXPECT_EQ(dumpPluginObjectGraph(*Graph), Snapshot);
}

TEST(AndroidKernelModuleFinalizerTest,
     PreservedNamesCannotBypassMalformedGraphValidation) {
  auto ExpectRejected = [&](std::unique_ptr<PluginObjectGraph> Graph) {
    ASSERT_NE(Graph, nullptr);
    const uint64_t Generation = Graph->generation();
    const std::string Snapshot = dumpPluginObjectGraph(*Graph);
    AndroidKernelModuleFinalizationPolicy Policy;
    Policy.StripUnneededSymbols = true;
    Error Finalize = finalizeAndroidKernelModuleObjectGraph(
        *Graph, Policy, "test malformed preserved release symbol");
    EXPECT_TRUE(static_cast<bool>(Finalize));
    if (Finalize)
      consumeError(std::move(Finalize));
    EXPECT_EQ(Graph->generation(), Generation);
    EXPECT_EQ(dumpPluginObjectGraph(*Graph), Snapshot);
  };

  auto MissingSection = makeObject(0);
  ASSERT_NE(MissingSection, nullptr);
  PluginObjectSymbol Missing;
  Missing.ID = MissingSection->allocateEntityID();
  Missing.Name = "init_module";
  Missing.Binding = NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL;
  Missing.Type = NEVERC_OBJECT_SYMBOL_TYPE_FUNCTION;
  Missing.Definition = NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED;
  Missing.SectionID = UINT64_C(0xdead);
  MissingSection->symbols().push_back(std::move(Missing));
  ExpectRejected(std::move(MissingSection));

  auto OutOfRange = makeObject(1);
  ASSERT_NE(OutOfRange, nullptr);
  PluginObjectSymbol PastEnd;
  PastEnd.ID = OutOfRange->allocateEntityID();
  PastEnd.Name = "__kcfi_typeid_past_end";
  PastEnd.Binding = NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL;
  PastEnd.Type = NEVERC_OBJECT_SYMBOL_TYPE_OBJECT;
  PastEnd.Definition = NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED;
  PastEnd.SectionID = OutOfRange->sections().front().ID;
  PastEnd.Value = 2;
  PastEnd.Size = 1;
  OutOfRange->symbols().push_back(std::move(PastEnd));
  ExpectRejected(std::move(OutOfRange));

  auto Overflow = makeObject(1);
  ASSERT_NE(Overflow, nullptr);
  PluginObjectSection &OverflowSection = Overflow->sections().front();
  OverflowSection.Kind = NEVERC_OBJECT_SECTION_KIND_ZERO_FILL;
  OverflowSection.ZeroFillSize = std::numeric_limits<uint64_t>::max();
  ExpectRejected(std::move(Overflow));
}

TEST(AndroidKernelModuleFinalizerTest, CommonSymbolRefusalIsAtomic) {
  auto Graph = makeObject(1);
  ASSERT_NE(Graph, nullptr);
  Graph->sections().front().Data.resize(8);
  const uint64_t SectionID = Graph->sections().front().ID;

  PluginObjectSymbol Ordinary;
  Ordinary.ID = Graph->allocateEntityID();
  Ordinary.Name = "ordinary_before_common_failure";
  Ordinary.Binding = NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL;
  Ordinary.Type = NEVERC_OBJECT_SYMBOL_TYPE_OBJECT;
  Ordinary.Definition = NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED;
  Ordinary.SectionID = SectionID;
  Ordinary.Size = 1;
  const uint64_t OrdinaryID = Ordinary.ID;
  Graph->symbols().push_back(std::move(Ordinary));

  PluginObjectSymbol Common;
  Common.ID = Graph->allocateEntityID();
  Common.Name = "unsupported_common";
  // Exercise the prune-candidate case: COMMON is an unsupported input class,
  // not an unneeded symbol that may disappear before policy validation.
  Common.Binding = NEVERC_OBJECT_SYMBOL_BINDING_LOCAL;
  Common.Type = NEVERC_OBJECT_SYMBOL_TYPE_OBJECT;
  Common.Definition = NEVERC_OBJECT_SYMBOL_DEFINITION_COMMON;
  Common.Size = 8;
  Common.Alignment = 8;
  Graph->symbols().push_back(std::move(Common));
  ASSERT_FALSE(verifyPluginObjectGraph(*Graph));

  const uint64_t Generation = Graph->generation();
  const size_t Symbols = Graph->symbolCount();
  AndroidKernelModuleFinalizationPolicy Policy;
  Policy.StripUnneededSymbols = true;
  Error Finalize = finalizeAndroidKernelModuleObjectGraph(
      *Graph, Policy, "test release Android module COMMON");
  ASSERT_TRUE(static_cast<bool>(Finalize));
  EXPECT_NE(errorText(std::move(Finalize)).find("COMMON symbol"),
            std::string::npos);
  EXPECT_EQ(Graph->generation(), Generation);
  EXPECT_EQ(Graph->symbolCount(), Symbols);
  ASSERT_NE(Graph->findSymbol(OrdinaryID), nullptr);
  EXPECT_EQ(Graph->findSymbol(OrdinaryID)->Name,
            "ordinary_before_common_failure");
}

TEST(AndroidKernelModuleFinalizerTest, LivePatchSectionRefusalIsAtomic) {
  auto Graph = makeObject(1);
  ASSERT_NE(Graph, nullptr);

  PluginObjectSection LivePatch;
  LivePatch.ID = Graph->allocateEntityID();
  LivePatch.Name = ".klp.rela.example";
  LivePatch.Alignment = 1;
  LivePatch.Data = {0};
  Graph->sections().push_back(std::move(LivePatch));
  ASSERT_FALSE(verifyPluginObjectGraph(*Graph));

  const uint64_t Generation = Graph->generation();
  const size_t Sections = Graph->sectionCount();
  AndroidKernelModuleFinalizationPolicy Policy;
  Policy.StripUnneededSymbols = true;
  Error Finalize = finalizeAndroidKernelModuleObjectGraph(
      *Graph, Policy, "test release Android module livepatch");
  ASSERT_TRUE(static_cast<bool>(Finalize));
  EXPECT_NE(errorText(std::move(Finalize)).find("livepatch section"),
            std::string::npos);
  EXPECT_EQ(Graph->generation(), Generation);
  EXPECT_EQ(Graph->sectionCount(), Sections);
}

TEST(AndroidKernelModuleFinalizerTest, LivePatchModInfoRefusalIsAtomic) {
  auto Graph = makeObject(1);
  ASSERT_NE(Graph, nullptr);

  PluginObjectSection ModInfo;
  ModInfo.ID = Graph->allocateEntityID();
  ModInfo.Name = ".modinfo";
  ModInfo.Alignment = 1;
  constexpr char Marker[] = "license=GPL\0livepatch=Y\0";
  ModInfo.Data.assign(Marker, Marker + sizeof(Marker));
  Graph->sections().push_back(std::move(ModInfo));
  ASSERT_FALSE(verifyPluginObjectGraph(*Graph));

  const uint64_t Generation = Graph->generation();
  const size_t Sections = Graph->sectionCount();
  AndroidKernelModuleFinalizationPolicy Policy;
  Policy.StripUnneededSymbols = true;
  Error Finalize = finalizeAndroidKernelModuleObjectGraph(
      *Graph, Policy, "test release Android module livepatch metadata");
  ASSERT_TRUE(static_cast<bool>(Finalize));
  EXPECT_NE(errorText(std::move(Finalize)).find("marked livepatch"),
            std::string::npos);
  EXPECT_EQ(Graph->generation(), Generation);
  EXPECT_EQ(Graph->sectionCount(), Sections);
}

TEST(AndroidKernelModuleFinalizerTest,
     BuiltinAdapterAuditsNativeLivePatchBeforeGraphSerialization) {
  initializeBuiltinTargets();
  LinkTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());

  const BuiltinTargetRoute *ELFRoute = nullptr;
  for (const BuiltinTargetRoute &Route : builtinTargetRoutes()) {
    const Triple Parsed(Triple::normalize(Route.CanonicalTriple));
    if (Route.SupportsObject &&
        Route.ObjectFormat == BuiltinObjectFormat::ELF &&
        Parsed.getArch() == Triple::aarch64) {
      ELFRoute = &Route;
      break;
    }
  }
  ASSERT_NE(ELFRoute, nullptr);

  auto Input = makeBuiltinObject(*ELFRoute, "livepatch_source");
  auto PartialTarget = makeBuiltinTargetKey(*ELFRoute);
  ASSERT_NE(Input, nullptr);
  ASSERT_TRUE(static_cast<bool>(PartialTarget))
      << errorText(PartialTarget.takeError());
  addAndroidKernelProfileContract(*Input);

  auto Snapshot = PluginTargetRegistry::freeze(
      ArrayRef<PluginTargetRegistrationView>(), PluginTargetRequest{});
  ASSERT_TRUE(static_cast<bool>(Snapshot)) << errorText(Snapshot.takeError());
  std::array<PluginObjectGraph *, 1> Inputs{Input.get()};
  BuiltinObjectMergeConfig PartialConfig;
  PartialConfig.AndroidKernelModule = true;
  auto Partial = executeBuiltinObjectMergeAdapter(
      Scope.task(), *Snapshot, std::move(*PartialTarget), Inputs,
      ArrayRef<ArrayRef<uint8_t>>{}, NEVERC_LINK_OPTION_NONE, PartialConfig);
  ASSERT_TRUE(static_cast<bool>(Partial)) << errorText(Partial.takeError());

  std::vector<uint8_t> LivePatchImage(Partial->MergedImage.begin(),
                                      Partial->MergedImage.end());
  auto Parsed = object::ObjectFile::createObjectFile(MemoryBufferRef(
      StringRef(reinterpret_cast<const char *>(LivePatchImage.data()),
                LivePatchImage.size()),
      "livepatch adapter test input"));
  ASSERT_TRUE(static_cast<bool>(Parsed)) << errorText(Parsed.takeError());
  auto *ELFObject = dyn_cast<object::ELF64LEObjectFile>(Parsed->get());
  ASSERT_NE(ELFObject, nullptr);
  bool Patched = false;
  for (const object::SymbolRef &Symbol : ELFObject->symbols()) {
    auto Name = Symbol.getName();
    ASSERT_TRUE(static_cast<bool>(Name)) << errorText(Name.takeError());
    if (*Name != "livepatch_source")
      continue;
    auto Native = ELFObject->getSymbol(Symbol.getRawDataRefImpl());
    ASSERT_TRUE(static_cast<bool>(Native)) << errorText(Native.takeError());
    object::ELF64LE::Sym Replacement = **Native;
    Replacement.st_shndx =
        neverc::AndroidKernelModuleSymbolPolicy::LivePatchSectionIndex;
    const auto *NativeBytes = reinterpret_cast<const uint8_t *>(*Native);
    ASSERT_GE(NativeBytes, LivePatchImage.data());
    const size_t Offset = NativeBytes - LivePatchImage.data();
    ASSERT_LE(Offset + sizeof(Replacement), LivePatchImage.size());
    std::memcpy(LivePatchImage.data() + Offset, &Replacement,
                sizeof(Replacement));
    Patched = true;
    break;
  }
  ASSERT_TRUE(Patched);

  auto ReleaseTarget = makeBuiltinTargetKey(*ELFRoute);
  ASSERT_TRUE(static_cast<bool>(ReleaseTarget))
      << errorText(ReleaseTarget.takeError());
  std::array<ArrayRef<uint8_t>, 1> NativeInputs{
      ArrayRef<uint8_t>(LivePatchImage)};
  std::array<PluginObjectGraph *, 1> ReleaseInputs{Partial->Object.get()};
  BuiltinObjectMergeConfig ReleaseConfig;
  ReleaseConfig.AndroidKernelModule = true;
  ReleaseConfig.FinalizeAndroidKernelModule = true;
  ReleaseConfig.StripUnneededSymbols = true;
  auto Release = executeBuiltinObjectMergeAdapter(
      Scope.task(), *Snapshot, std::move(*ReleaseTarget),
      ArrayRef<PluginObjectGraph *>(ReleaseInputs), NativeInputs,
      NEVERC_LINK_OPTION_NONE, ReleaseConfig);
  ASSERT_FALSE(static_cast<bool>(Release));
  const std::string Message = errorText(Release.takeError());
  EXPECT_NE(Message.find("native input image 0"), std::string::npos);
  EXPECT_NE(Message.find("livepatch symbol 'livepatch_source'"),
            std::string::npos);
}

TEST(AndroidKernelModuleFinalizerTest,
     ReleaseStripRejectsRetainedRelocationToDroppedDebugEntity) {
  auto Graph = makeObject(1);
  ASSERT_NE(Graph, nullptr);
  const uint64_t TextSectionID = Graph->sections().front().ID;

  PluginObjectSection Debug;
  Debug.ID = Graph->allocateEntityID();
  Debug.Name = ".debug_info";
  Debug.Kind = NEVERC_OBJECT_SECTION_KIND_DEBUG;
  Debug.Flags = NEVERC_OBJECT_SECTION_DEBUG;
  Debug.Alignment = 1;
  Debug.Data = {0};
  const uint64_t DebugSectionID = Debug.ID;
  Graph->sections().push_back(std::move(Debug));

  PluginObjectSymbol DebugSymbol;
  DebugSymbol.ID = Graph->allocateEntityID();
  DebugSymbol.Name = "release_debug_target";
  DebugSymbol.Definition = NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED;
  DebugSymbol.SectionID = DebugSectionID;
  DebugSymbol.Size = 1;
  const uint64_t DebugSymbolID = DebugSymbol.ID;
  Graph->symbols().push_back(std::move(DebugSymbol));

  PluginObjectRelocation Relocation;
  Relocation.ID = Graph->allocateEntityID();
  Relocation.SectionID = TextSectionID;
  Relocation.Kind = NEVERC_OBJECT_RELOCATION_ABSOLUTE;
  Relocation.TargetKind = NEVERC_OBJECT_RELOCATION_TARGET_SYMBOL;
  Relocation.Width = 8;
  Relocation.TargetSymbolID = DebugSymbolID;
  Graph->relocations().push_back(std::move(Relocation));
  ASSERT_FALSE(verifyPluginObjectGraph(*Graph));

  const uint64_t Generation = Graph->generation();
  const size_t Sections = Graph->sectionCount();
  const size_t Symbols = Graph->symbolCount();
  const size_t Relocations = Graph->relocationCount();
  AndroidKernelModuleFinalizationPolicy Policy;
  Policy.DropDebugInfo = true;
  Policy.StripUnneededSymbols = true;
  Error Finalize = finalizeAndroidKernelModuleObjectGraph(
      *Graph, Policy, "test release Android module");
  ASSERT_TRUE(static_cast<bool>(Finalize));
  EXPECT_NE(errorText(std::move(Finalize)).find("retained section references"),
            std::string::npos);
  EXPECT_EQ(Graph->generation(), Generation);
  EXPECT_EQ(Graph->sectionCount(), Sections);
  EXPECT_EQ(Graph->symbolCount(), Symbols);
  EXPECT_EQ(Graph->relocationCount(), Relocations);
}

TEST(AndroidKernelModuleFinalizerTest,
     ImageVerifierRejectsSymtabLinkedToSectionNameTable) {
  initializeBuiltinTargets();
  LinkTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());

  const BuiltinTargetRoute *ELFRoute = nullptr;
  for (const BuiltinTargetRoute &Route : builtinTargetRoutes()) {
    const Triple Parsed(Triple::normalize(Route.CanonicalTriple));
    if (Route.SupportsObject &&
        Route.ObjectFormat == BuiltinObjectFormat::ELF &&
        Parsed.getArch() == Triple::aarch64) {
      ELFRoute = &Route;
      break;
    }
  }
  ASSERT_NE(ELFRoute, nullptr);

  auto Input = makeBuiltinObject(*ELFRoute, "release_public_definition");
  auto Target = makeBuiltinTargetKey(*ELFRoute);
  ASSERT_NE(Input, nullptr);
  ASSERT_TRUE(static_cast<bool>(Target)) << errorText(Target.takeError());
  addAndroidKernelProfileContract(*Input);

  auto AddSection = [&](StringRef Name, NevercObjectSectionFlags Flags,
                        uint64_t Alignment, size_t Size) {
    PluginObjectSection Section;
    Section.ID = Input->allocateEntityID();
    Section.Name = Name.str();
    Section.Kind = NEVERC_OBJECT_SECTION_KIND_DATA;
    Section.Flags = Flags;
    Section.Alignment = Alignment;
    Section.Data.resize(Size);
    Input->sections().push_back(std::move(Section));
  };
  AddSection("__versions", NEVERC_OBJECT_SECTION_ALLOCATED, 8, 0);
  AddSection(".codetag.alloc_tags",
             NEVERC_OBJECT_SECTION_ALLOCATED | NEVERC_OBJECT_SECTION_WRITABLE,
             8, 0);
  AddSection(".gnu.linkonce.this_module",
             NEVERC_OBJECT_SECTION_ALLOCATED | NEVERC_OBJECT_SECTION_WRITABLE,
             64, 1024);
  Input->advanceGeneration();
  Input->issueLayoutProof();
  ASSERT_FALSE(verifyPluginObjectGraph(*Input));

  auto Snapshot = PluginTargetRegistry::freeze(
      ArrayRef<PluginTargetRegistrationView>(), PluginTargetRequest{});
  ASSERT_TRUE(static_cast<bool>(Snapshot)) << errorText(Snapshot.takeError());
  auto Writer = ObjectWriterProvider::create(*Snapshot);
  ASSERT_TRUE(static_cast<bool>(Writer)) << errorText(Writer.takeError());
  auto NativeInput =
      (*Writer)->beginWrite(Scope.task(), *Input,
                            ObjectOutputDestination::memory(
                                "symtab-link-input.o", UINT64_C(1) << 20));
  ASSERT_TRUE(static_cast<bool>(NativeInput))
      << errorText(NativeInput.takeError());
  auto NativeInputBytes = (*NativeInput)->pendingBytes();
  ASSERT_TRUE(static_cast<bool>(NativeInputBytes))
      << errorText(NativeInputBytes.takeError());
  std::vector<uint8_t> ImmutableInput(NativeInputBytes->begin(),
                                      NativeInputBytes->end());
  EXPECT_FALSE((*NativeInput)->abort());

  std::array<PluginObjectGraph *, 1> Inputs{Input.get()};
  std::array<ArrayRef<uint8_t>, 1> InputImages{ImmutableInput};
  BuiltinObjectMergeConfig Config;
  Config.AndroidKernelModule = true;
  Config.FinalizeAndroidKernelModule = true;
  Config.DropDebugInfo = true;
  Config.StripUnneededSymbols = true;
  auto Merged = executeBuiltinObjectMergeAdapter(
      Scope.task(), *Snapshot, std::move(*Target), Inputs, InputImages,
      NEVERC_LINK_OPTION_NONE, Config);
  ASSERT_TRUE(static_cast<bool>(Merged)) << errorText(Merged.takeError());

  AndroidKernelModuleFinalizationPolicy Policy;
  Policy.DropDebugInfo = true;
  Policy.StripUnneededSymbols = true;
  Policy.SymbolNameState = AndroidKernelSymbolNameState::CanonicalRelease;
  const uint64_t MergedGeneration = Merged->Object->generation();
  const SmallVector<char, 0> NativeImageBeforeFinalizer = Merged->MergedImage;
  Error Finalize = finalizeAndroidKernelModuleObjectGraph(
      *Merged->Object, Policy, "verified built-in Android module");
  ASSERT_FALSE(Finalize) << errorText(std::move(Finalize));
  EXPECT_EQ(Merged->Object->generation(), MergedGeneration);
  EXPECT_EQ(Merged->MergedImage, NativeImageBeforeFinalizer);

  constexpr StringLiteral PublicReleaseName = "fn_0";
  const auto HasMergedSymbol = [&](StringRef Name) {
    return std::any_of(
        Merged->Object->symbols().begin(), Merged->Object->symbols().end(),
        [&](const PluginObjectSymbol &Symbol) { return Symbol.Name == Name; });
  };
  EXPECT_TRUE(HasMergedSymbol(PublicReleaseName));
  EXPECT_FALSE(HasMergedSymbol("release_public_definition"));

  ArrayRef<uint8_t> ValidImage(
      reinterpret_cast<const uint8_t *>(Merged->MergedImage.data()),
      Merged->MergedImage.size());
  EXPECT_FALSE(verifyFinalAndroidKernelModuleImage(
      ValidImage, Policy, "valid final Android module"));

  std::vector<uint8_t> ReadableNameBypass(ValidImage.begin(), ValidImage.end());
  auto ReleaseNamePosition =
      std::search(ReadableNameBypass.begin(), ReadableNameBypass.end(),
                  PublicReleaseName.begin(), PublicReleaseName.end());
  ASSERT_NE(ReleaseNamePosition, ReadableNameBypass.end());
  std::fill_n(ReleaseNamePosition, PublicReleaseName.size(),
              static_cast<uint8_t>('x'));
  Error NameBypass = verifyFinalAndroidKernelModuleImage(
      ReadableNameBypass, Policy,
      "final Android module with readable symbol bypass");
  ASSERT_TRUE(static_cast<bool>(NameBypass));
  EXPECT_NE(errorText(std::move(NameBypass)).find("release symbol"),
            std::string::npos);

  std::vector<uint8_t> Corrupted(ValidImage.begin(), ValidImage.end());
  StringRef CorruptedBytes(reinterpret_cast<const char *>(Corrupted.data()),
                           Corrupted.size());
  auto Parsed = object::ELFFile<object::ELF64LE>::create(CorruptedBytes);
  ASSERT_TRUE(static_cast<bool>(Parsed)) << errorText(Parsed.takeError());
  auto Sections = Parsed->sections();
  ASSERT_TRUE(static_cast<bool>(Sections)) << errorText(Sections.takeError());
  std::optional<unsigned> SymtabIndex;
  std::optional<unsigned> ShstrtabIndex;
  for (unsigned I = 0; I < Sections->size(); ++I) {
    auto Name = Parsed->getSectionName((*Sections)[I]);
    ASSERT_TRUE(static_cast<bool>(Name)) << errorText(Name.takeError());
    if (*Name == ".symtab")
      SymtabIndex = I;
    else if (*Name == ".shstrtab")
      ShstrtabIndex = I;
  }
  ASSERT_TRUE(SymtabIndex.has_value());
  ASSERT_TRUE(ShstrtabIndex.has_value());

  object::ELF64LE::Shdr CorruptedSymtab = (*Sections)[*SymtabIndex];
  CorruptedSymtab.sh_link = *ShstrtabIndex;
  const auto *OriginalSymtabBytes =
      reinterpret_cast<const uint8_t *>(&(*Sections)[*SymtabIndex]);
  ASSERT_GE(OriginalSymtabBytes, Corrupted.data());
  const size_t SymtabOffset = OriginalSymtabBytes - Corrupted.data();
  ASSERT_LE(SymtabOffset + sizeof(CorruptedSymtab), Corrupted.size());
  std::memcpy(Corrupted.data() + SymtabOffset, &CorruptedSymtab,
              sizeof(CorruptedSymtab));

  Error Verify = verifyFinalAndroidKernelModuleImage(
      Corrupted, Policy, "corrupted final Android module");
  ASSERT_TRUE(static_cast<bool>(Verify));
  const std::string VerifyMessage = errorText(std::move(Verify));
  EXPECT_NE(VerifyMessage.find("not a parseable ELF64LE object"),
            std::string::npos)
      << VerifyMessage;
}

TEST(AndroidKernelModuleFinalizerTest,
     OriginalProviderGraphSurvivesWriterReorderAndStandaloneAudit) {
  initializeBuiltinTargets();
  LinkTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());

  const BuiltinTargetRoute *ELFRoute = nullptr;
  for (const BuiltinTargetRoute &Route : builtinTargetRoutes()) {
    const Triple Parsed(Triple::normalize(Route.CanonicalTriple));
    if (Route.SupportsObject &&
        Route.ObjectFormat == BuiltinObjectFormat::ELF &&
        Parsed.getArch() == Triple::aarch64) {
      ELFRoute = &Route;
      break;
    }
  }
  ASSERT_NE(ELFRoute, nullptr);

  auto Input = makeBuiltinObject(*ELFRoute, "custom_release_entry");
  auto Target = makeBuiltinTargetKey(*ELFRoute);
  ASSERT_NE(Input, nullptr);
  ASSERT_TRUE(static_cast<bool>(Target)) << errorText(Target.takeError());
  PluginObjectSection &Text = Input->sections().front();
  Text.Data.resize(32);
  const uint64_t TextID = Text.ID;

  auto AddSymbol = [&](StringRef Name, NevercObjectSymbolBinding Binding,
                       NevercObjectSymbolType Type,
                       NevercObjectSymbolDefinition Definition, uint64_t Value,
                       uint64_t Size) {
    PluginObjectSymbol Symbol;
    Symbol.ID = Input->allocateEntityID();
    Symbol.Name = Name.str();
    Symbol.Binding = Binding;
    Symbol.Type = Type;
    Symbol.Definition = Definition;
    Symbol.SectionID =
        Definition == NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED ? TextID : 0;
    Symbol.Value = Value;
    Symbol.Size = Size;
    const uint64_t ID = Symbol.ID;
    Input->symbols().push_back(std::move(Symbol));
    return ID;
  };
  const uint64_t LocalA =
      AddSymbol("writer_local_b", NEVERC_OBJECT_SYMBOL_BINDING_LOCAL,
                NEVERC_OBJECT_SYMBOL_TYPE_NO_TYPE,
                NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED, 8, 0);
  const uint64_t LocalB =
      AddSymbol("writer_local_a", NEVERC_OBJECT_SYMBOL_BINDING_LOCAL,
                NEVERC_OBJECT_SYMBOL_TYPE_NO_TYPE,
                NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED, 8, 0);
  const uint64_t KCFI =
      AddSymbol("__kcfi_typeid_sample", NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL,
                NEVERC_OBJECT_SYMBOL_TYPE_OBJECT,
                NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED, 16, 4);
  AddSymbol("init_module", NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL,
            NEVERC_OBJECT_SYMBOL_TYPE_FUNCTION,
            NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED, 0, 4);
  AddSymbol("cleanup_module", NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL,
            NEVERC_OBJECT_SYMBOL_TYPE_FUNCTION,
            NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED, 4, 4);
  AddSymbol("__cfi_check", NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL,
            NEVERC_OBJECT_SYMBOL_TYPE_FUNCTION,
            NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED, 12, 4);
  AddSymbol("__cfi_check_fail", NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL,
            NEVERC_OBJECT_SYMBOL_TYPE_FUNCTION,
            NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED, 20, 4);
  AddSymbol("__typeid__sample_global_addr", NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL,
            NEVERC_OBJECT_SYMBOL_TYPE_NO_TYPE,
            NEVERC_OBJECT_SYMBOL_DEFINITION_ABSOLUTE, 0x2a, 0);
  const uint64_t Import =
      AddSymbol("printk", NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL,
                NEVERC_OBJECT_SYMBOL_TYPE_NO_TYPE,
                NEVERC_OBJECT_SYMBOL_DEFINITION_UNDEFINED, 0, 0);
  const uint64_t EquivalentImport =
      AddSymbol("printk", NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL,
                NEVERC_OBJECT_SYMBOL_TYPE_NO_TYPE,
                NEVERC_OBJECT_SYMBOL_DEFINITION_UNDEFINED, 0, 0);

  auto AddRelocation = [&](uint64_t Offset, uint32_t Width,
                           uint64_t TargetSymbolID) {
    PluginObjectRelocation Relocation;
    Relocation.ID = Input->allocateEntityID();
    Relocation.SectionID = TextID;
    Relocation.Offset = Offset;
    Relocation.Kind = NEVERC_OBJECT_RELOCATION_ABSOLUTE;
    Relocation.TargetKind = NEVERC_OBJECT_RELOCATION_TARGET_SYMBOL;
    Relocation.Width = Width;
    Relocation.TargetSymbolID = TargetSymbolID;
    Relocation.Extension.Owner = ELFRoute->ObjectFormatID;
    Relocation.Extension.Version =
        neverc::plugin::builtinext::RelocationVersion;
    const uint64_t NativeType =
        Width == 64 ? ELF::R_AARCH64_ABS64 : ELF::R_AARCH64_ABS32;
    const StringRef NativeName =
        Width == 64 ? "R_AARCH64_ABS64" : "R_AARCH64_ABS32";
    SmallVector<uint8_t, 48> NativeFacts;
    neverc::plugin::builtinext::appendHeader(
        NativeFacts, neverc::plugin::builtinext::RelocationTag,
        neverc::plugin::builtinext::RelocationVersion);
    neverc::plugin::builtinext::appendU64(NativeFacts, NativeType);
    neverc::plugin::builtinext::appendU32(NativeFacts, NativeName.size());
    neverc::plugin::builtinext::appendBytes(NativeFacts, NativeName);
    Relocation.Extension.Bytes.assign(NativeFacts.begin(), NativeFacts.end());
    Input->relocations().push_back(std::move(Relocation));
  };
  AddRelocation(0, 64, LocalA);
  AddRelocation(8, 64, LocalB);
  AddRelocation(16, 32, KCFI);
  AddRelocation(20, 32, Import);
  AddRelocation(24, 64, EquivalentImport);
  addAndroidKernelProfileContract(*Input);

  auto AddABISection = [&](StringRef Name, NevercObjectSectionFlags Flags,
                           uint64_t Alignment, size_t Size) {
    PluginObjectSection Section;
    Section.ID = Input->allocateEntityID();
    Section.Name = Name.str();
    Section.Kind = NEVERC_OBJECT_SECTION_KIND_DATA;
    Section.Flags = Flags;
    Section.Alignment = Alignment;
    Section.Data.resize(Size);
    const uint64_t ID = Section.ID;
    Input->sections().push_back(std::move(Section));
    return ID;
  };
  AddABISection("__versions", NEVERC_OBJECT_SECTION_ALLOCATED, 8, 0);
  AddABISection(
      ".codetag.alloc_tags",
      NEVERC_OBJECT_SECTION_ALLOCATED | NEVERC_OBJECT_SECTION_WRITABLE, 8, 0);
  AddABISection(".gnu.linkonce.this_module",
                NEVERC_OBJECT_SECTION_ALLOCATED |
                    NEVERC_OBJECT_SECTION_WRITABLE,
                64, 1024);
  const uint64_t DataID = AddABISection(
      ".data", NEVERC_OBJECT_SECTION_ALLOCATED | NEVERC_OBJECT_SECTION_WRITABLE,
      8, 8);
  PluginObjectSection BSS;
  BSS.ID = Input->allocateEntityID();
  BSS.Name = ".bss";
  BSS.Kind = NEVERC_OBJECT_SECTION_KIND_ZERO_FILL;
  BSS.Flags = NEVERC_OBJECT_SECTION_ALLOCATED | NEVERC_OBJECT_SECTION_WRITABLE;
  BSS.Alignment = 16;
  BSS.ZeroFillSize = 16;
  const uint64_t BSSID = BSS.ID;
  Input->sections().push_back(std::move(BSS));
  AddABISection(".debug_info", NEVERC_OBJECT_SECTION_DEBUG, 1, 4);

  auto AddDataSymbol =
      [&](StringRef Name, uint64_t SectionID, NevercObjectSymbolBinding Binding,
          NevercObjectSymbolVisibility Visibility, uint64_t Size) {
        PluginObjectSymbol Symbol;
        Symbol.ID = Input->allocateEntityID();
        Symbol.Name = Name.str();
        Symbol.Binding = Binding;
        Symbol.Visibility = Visibility;
        Symbol.Type = NEVERC_OBJECT_SYMBOL_TYPE_OBJECT;
        Symbol.Definition = NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED;
        Symbol.SectionID = SectionID;
        Symbol.Size = Size;
        Symbol.Alignment = Size;
        Input->symbols().push_back(std::move(Symbol));
      };
  AddDataSymbol("ordinary_hidden_weak", DataID,
                NEVERC_OBJECT_SYMBOL_BINDING_WEAK,
                NEVERC_OBJECT_SYMBOL_VISIBILITY_HIDDEN, 8);
  AddDataSymbol("ordinary_bss", BSSID, NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL,
                NEVERC_OBJECT_SYMBOL_VISIBILITY_DEFAULT, 16);
  Input->advanceGeneration();
  Input->issueLayoutProof();
  Error VerifyInput = verifyPluginObjectGraph(*Input);
  ASSERT_FALSE(VerifyInput) << errorText(std::move(VerifyInput));

  auto Snapshot = PluginTargetRegistry::freeze(
      ArrayRef<PluginTargetRegistrationView>(), PluginTargetRequest{});
  ASSERT_TRUE(static_cast<bool>(Snapshot)) << errorText(Snapshot.takeError());
  auto InputWriter = ObjectWriterProvider::create(*Snapshot);
  ASSERT_TRUE(static_cast<bool>(InputWriter))
      << errorText(InputWriter.takeError());
  auto InputImage =
      (*InputWriter)
          ->beginWrite(Scope.task(), *Input,
                       ObjectOutputDestination::memory("custom-release-input.o",
                                                       UINT64_C(1) << 20));
  ASSERT_TRUE(static_cast<bool>(InputImage))
      << errorText(InputImage.takeError());
  auto DefaultInputBytes = (*InputImage)->pendingBytes();
  ASSERT_TRUE(static_cast<bool>(DefaultInputBytes))
      << errorText(DefaultInputBytes.takeError());
  auto DefaultInputSemantics = readELFSemantics(*DefaultInputBytes);
  ASSERT_TRUE(static_cast<bool>(DefaultInputSemantics))
      << errorText(DefaultInputSemantics.takeError());
  // This is the historical LLVM MC path: valid ELF with one shared string
  // table. The explicit release policy must not affect ordinary writes.
  EXPECT_EQ(DefaultInputSemantics->StringTableCount, 1U);
  EXPECT_TRUE(DefaultInputSemantics->HasSymbolStringTable);
  EXPECT_FALSE(DefaultInputSemantics->HasSectionStringTable);
  EXPECT_FALSE((*InputImage)->abort());

  ObjectOutputDestination CanonicalInputDestination =
      ObjectOutputDestination::memory("custom-canonical-input.o", UINT64_C(1)
                                                                      << 20);
  CanonicalInputDestination.WritePolicy = ObjectWritePolicy::CanonicalELFTables;
  auto CanonicalInputImage =
      (*InputWriter)
          ->beginWrite(Scope.task(), *Input, CanonicalInputDestination);
  ASSERT_TRUE(static_cast<bool>(CanonicalInputImage))
      << errorText(CanonicalInputImage.takeError());
  auto CanonicalInputBytes = (*CanonicalInputImage)->pendingBytes();
  ASSERT_TRUE(static_cast<bool>(CanonicalInputBytes))
      << errorText(CanonicalInputBytes.takeError());
  auto CanonicalInputSemantics = readELFSemantics(*CanonicalInputBytes);
  ASSERT_TRUE(static_cast<bool>(CanonicalInputSemantics))
      << errorText(CanonicalInputSemantics.takeError());
  EXPECT_EQ(CanonicalInputSemantics->StringTableCount, 2U);
  EXPECT_TRUE(CanonicalInputSemantics->HasSymbolStringTable);
  EXPECT_TRUE(CanonicalInputSemantics->HasSectionStringTable);
  EXPECT_TRUE(CanonicalInputSemantics->SymtabLinksSymbolStringTable);
  EXPECT_TRUE(llvm::any_of(CanonicalInputSemantics->Symbols,
                           [](const ELFSymbolSemantics &Symbol) {
                             return isAArch64MappingSymbol(Symbol.Name);
                           }));
  EXPECT_TRUE(llvm::any_of(CanonicalInputSemantics->Symbols,
                           [](const ELFSymbolSemantics &Symbol) {
                             return Symbol.Name == "custom_release_entry";
                           }));
  EXPECT_NE(CanonicalInputSemantics->SectionNames.find(".debug_info"),
            CanonicalInputSemantics->SectionNames.end());
  EXPECT_FALSE((*CanonicalInputImage)->abort());

  ObjectOutputDestination DebugStrippedInputDestination =
      ObjectOutputDestination::memory("custom-canonical-debug-strip.o",
                                      UINT64_C(1) << 20);
  DebugStrippedInputDestination.WritePolicy =
      ObjectWritePolicy::CanonicalELFTables;
  DebugStrippedInputDestination.DropDebugInfo = true;
  auto DebugStrippedInputImage =
      (*InputWriter)
          ->beginWrite(Scope.task(), *Input, DebugStrippedInputDestination);
  ASSERT_TRUE(static_cast<bool>(DebugStrippedInputImage))
      << errorText(DebugStrippedInputImage.takeError());
  auto DebugStrippedInputBytes = (*DebugStrippedInputImage)->pendingBytes();
  ASSERT_TRUE(static_cast<bool>(DebugStrippedInputBytes))
      << errorText(DebugStrippedInputBytes.takeError());
  auto DebugStrippedInputSemantics = readELFSemantics(*DebugStrippedInputBytes);
  ASSERT_TRUE(static_cast<bool>(DebugStrippedInputSemantics))
      << errorText(DebugStrippedInputSemantics.takeError());
  EXPECT_EQ(DebugStrippedInputSemantics->SectionNames.find(".debug_info"),
            DebugStrippedInputSemantics->SectionNames.end());
  EXPECT_TRUE(llvm::any_of(DebugStrippedInputSemantics->Symbols,
                           [](const ELFSymbolSemantics &Symbol) {
                             return isAArch64MappingSymbol(Symbol.Name);
                           }));
  EXPECT_TRUE(llvm::any_of(DebugStrippedInputSemantics->Symbols,
                           [](const ELFSymbolSemantics &Symbol) {
                             return Symbol.Name == "custom_release_entry";
                           }));
  EXPECT_FALSE((*DebugStrippedInputImage)->abort());

  ObjectOutputDestination InvalidDebugPolicy = ObjectOutputDestination::memory(
      "invalid-debug-policy.o", UINT64_C(1) << 20);
  InvalidDebugPolicy.DropDebugInfo = true;
  auto InvalidDebugImage =
      (*InputWriter)->beginWrite(Scope.task(), *Input, InvalidDebugPolicy);
  ASSERT_FALSE(static_cast<bool>(InvalidDebugImage));
  EXPECT_NE(errorText(InvalidDebugImage.takeError())
                .find("explicit ELF object write policy"),
            std::string::npos);
  EXPECT_FALSE(findPluginMemoryOutput(Scope.task(), "invalid-debug-policy.o")
                   .has_value());

  std::array<PluginObjectGraph *, 1> Inputs{Input.get()};
  BuiltinObjectMergeConfig PartialConfig;
  PartialConfig.AndroidKernelModule = true;
  auto Partial = executeBuiltinObjectMergeAdapter(
      Scope.task(), *Snapshot, std::move(*Target), Inputs,
      ArrayRef<ArrayRef<uint8_t>>{}, NEVERC_LINK_OPTION_NONE, PartialConfig);
  ASSERT_TRUE(static_cast<bool>(Partial)) << errorText(Partial.takeError());

  // A custom ObjectMergeProvider may return symbols in any list order. Keep
  // the graph deliberately unlike the ELF writer's local-first/value/name
  // ordering; exact structural ties own a name multiset, not one list slot.
  auto AddCustomLocal = [&](StringRef Name) {
    PluginObjectSymbol Symbol;
    Symbol.ID = Partial->Object->allocateEntityID();
    Symbol.Name = Name.str();
    Symbol.Binding = NEVERC_OBJECT_SYMBOL_BINDING_LOCAL;
    Symbol.Type = NEVERC_OBJECT_SYMBOL_TYPE_NO_TYPE;
    Symbol.Definition = NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED;
    Symbol.SectionID = Partial->Object->sections().front().ID;
    Symbol.Value = 8;
    const uint64_t ID = Symbol.ID;
    Partial->Object->symbols().push_back(std::move(Symbol));
    return ID;
  };
  const uint64_t CustomLocalA = AddCustomLocal("custom_local_b");
  const uint64_t CustomLocalB = AddCustomLocal("custom_local_a");
  auto Relocation = Partial->Object->relocations().begin();
  ASSERT_NE(Relocation, Partial->Object->relocations().end());
  Relocation->TargetKind = NEVERC_OBJECT_RELOCATION_TARGET_SYMBOL;
  Relocation->TargetSymbolID = CustomLocalA;
  Relocation->TargetSectionID = 0;
  ++Relocation;
  ASSERT_NE(Relocation, Partial->Object->relocations().end());
  Relocation->TargetKind = NEVERC_OBJECT_RELOCATION_TARGET_SYMBOL;
  Relocation->TargetSymbolID = CustomLocalB;
  Relocation->TargetSectionID = 0;
  Partial->Object->symbols().reverse();
  Partial->Object->advanceGeneration();
  AndroidKernelModuleFinalizationPolicy Policy;
  Policy.DropDebugInfo = true;
  Policy.StripUnneededSymbols = true;
  Policy.SymbolNameState = AndroidKernelSymbolNameState::Original;
  neverc::AndroidKernelReleaseSymbolMap ReleaseSymbolMap;
  Error Finalize = finalizeAndroidKernelModuleObjectGraph(
      *Partial->Object, Policy, "test custom-provider release graph",
      &ReleaseSymbolMap);
  ASSERT_FALSE(Finalize) << errorText(std::move(Finalize));
  Error GraphAudit = verifyFinalAndroidKernelModuleObjectGraph(
      *Partial->Object, Policy,
      "test custom-provider release graph standalone audit");
  ASSERT_FALSE(GraphAudit) << errorText(std::move(GraphAudit));
  SCOPED_TRACE(dumpPluginObjectGraph(*Partial->Object));

  std::vector<std::string> LocalNames;
  for (const PluginObjectSymbol &Symbol : Partial->Object->symbols())
    if (Symbol.Value == 8 &&
        Symbol.Binding == NEVERC_OBJECT_SYMBOL_BINDING_LOCAL)
      LocalNames.push_back(Symbol.Name);
  llvm::sort(LocalNames);
  EXPECT_EQ(LocalNames, (std::vector<std::string>{"code_8", "code_8_1"}));
  const auto HasGraphName = [&](StringRef Name) {
    return llvm::any_of(
        Partial->Object->symbols(),
        [&](const PluginObjectSymbol &Symbol) { return Symbol.Name == Name; });
  };
  EXPECT_TRUE(HasGraphName("__kcfi_typeid_sample"));
  EXPECT_TRUE(HasGraphName("__typeid__sample_global_addr"));
  EXPECT_TRUE(HasGraphName("init_module"));
  EXPECT_TRUE(HasGraphName("cleanup_module"));
  EXPECT_TRUE(HasGraphName("__cfi_check"));
  EXPECT_TRUE(HasGraphName("__cfi_check_fail"));
  EXPECT_TRUE(HasGraphName("printk"));
  EXPECT_FALSE(HasGraphName("custom_release_entry"));
  EXPECT_FALSE(HasGraphName("writer_local_a"));
  EXPECT_FALSE(HasGraphName("writer_local_b"));

  Partial->Object->issueLayoutProof();
  auto Writer = ObjectWriterProvider::create(*Snapshot);
  ASSERT_TRUE(static_cast<bool>(Writer)) << errorText(Writer.takeError());

  ObjectOutputDestination SerializedReferenceDestination =
      ObjectOutputDestination::memory("custom-release-reference.ko", UINT64_C(1)
                                                                         << 20);
  SerializedReferenceDestination.WritePolicy =
      ObjectWritePolicy::CanonicalELFTables;
  SerializedReferenceDestination.DropDebugInfo = true;
  auto SerializedReferenceImage = (*Writer)->beginWrite(
      Scope.task(), *Partial->Object, SerializedReferenceDestination);
  ASSERT_TRUE(static_cast<bool>(SerializedReferenceImage))
      << errorText(SerializedReferenceImage.takeError());
  auto SerializedReferenceBytes = (*SerializedReferenceImage)->pendingBytes();
  ASSERT_TRUE(static_cast<bool>(SerializedReferenceBytes))
      << errorText(SerializedReferenceBytes.takeError());
  auto SerializedReferenceSemantics =
      readELFSemantics(*SerializedReferenceBytes);
  ASSERT_TRUE(static_cast<bool>(SerializedReferenceSemantics))
      << errorText(SerializedReferenceSemantics.takeError());
  EXPECT_TRUE(llvm::any_of(SerializedReferenceSemantics->Symbols,
                           [](const ELFSymbolSemantics &Symbol) {
                             return isAArch64MappingSymbol(Symbol.Name);
                           }));

  ObjectOutputDestination ReleaseDestination =
      ObjectOutputDestination::memory("custom-release.ko", UINT64_C(1) << 20);
  ReleaseDestination.WritePolicy = ObjectWritePolicy::AndroidKernelRelease;
  ReleaseDestination.DropDebugInfo = true;
  auto Image =
      (*Writer)->beginWrite(Scope.task(), *Partial->Object, ReleaseDestination);
  ASSERT_TRUE(static_cast<bool>(Image)) << errorText(Image.takeError());
  auto Pending = (*Image)->pendingBytes();
  ASSERT_TRUE(static_cast<bool>(Pending)) << errorText(Pending.takeError());
  std::vector<uint8_t> Serialized(Pending->begin(), Pending->end());
  auto SerializedSemantics = readELFSemantics(Serialized);
  ASSERT_TRUE(static_cast<bool>(SerializedSemantics))
      << errorText(SerializedSemantics.takeError());
  Error BoundMap = bindAndroidKernelReleaseSymbolMapToImage(
      ReleaseSymbolMap, Serialized,
      "test custom-provider final release symbol map");
  ASSERT_FALSE(BoundMap) << errorText(std::move(BoundMap));
  for (const neverc::AndroidKernelReleaseSymbolMapEntry &Entry :
       ReleaseSymbolMap.Symbols)
    EXPECT_TRUE(llvm::any_of(SerializedSemantics->Symbols,
                             [&](const ELFSymbolSemantics &Symbol) {
                               return Symbol.Name == Entry.ReleaseName;
                             }))
        << Entry.OriginalName << " -> " << Entry.ReleaseName;
  EXPECT_TRUE(llvm::none_of(
      ReleaseSymbolMap.Symbols,
      [](const neverc::AndroidKernelReleaseSymbolMapEntry &Entry) {
        return Entry.OriginalName == "custom_local_a" ||
               Entry.OriginalName == "custom_local_b";
      }));
  EXPECT_TRUE(
      llvm::any_of(ReleaseSymbolMap.Symbols,
                   [](const neverc::AndroidKernelReleaseSymbolMapEntry &Entry) {
                     return Entry.OriginalName == "ordinary_hidden_weak";
                   }));

  EXPECT_EQ(SerializedReferenceSemantics->Machine,
            SerializedSemantics->Machine);
  EXPECT_EQ(SerializedReferenceSemantics->Flags, SerializedSemantics->Flags);
  EXPECT_EQ(SerializedReferenceSemantics->OSABI, SerializedSemantics->OSABI);
  EXPECT_EQ(SerializedReferenceSemantics->ABIVersion,
            SerializedSemantics->ABIVersion);
  EXPECT_EQ(SerializedReferenceSemantics->OrdinarySections,
            SerializedSemantics->OrdinarySections);
  EXPECT_EQ(SerializedReferenceSemantics->Relocations,
            SerializedSemantics->Relocations);

  // The MC writer may lower a symbol-target relocation to the equivalent
  // section-symbol-plus-addend form.  The exact relocation comparison above
  // proves that lowering preserved meaning; derive the release keep-set from
  // those serialized targets, where the native finalizer actually runs.
  std::set<std::string> SerializedRelocationTargets;
  for (const ELFRelocationSemantics &Relocation :
       SerializedReferenceSemantics->Relocations)
    SerializedRelocationTargets.insert(Relocation.Target);
  std::vector<ELFSymbolSemantics> ExpectedReleaseSymbols;
  for (const ELFSymbolSemantics &Symbol :
       SerializedReferenceSemantics->Symbols) {
    const bool RelocationRequired =
        SerializedRelocationTargets.find(Symbol.Name) !=
        SerializedRelocationTargets.end();
    const bool DefinedNonLocal =
        Symbol.Binding != ELF::STB_LOCAL && Symbol.Section != "<undefined>";
    if (RelocationRequired || DefinedNonLocal) {
      ExpectedReleaseSymbols.push_back(Symbol);
      continue;
    }
    EXPECT_TRUE(isAArch64MappingSymbol(Symbol.Name) ||
                Symbol.Binding == ELF::STB_LOCAL ||
                Symbol.Section == "<undefined>")
        << Symbol.Name;
  }
  EXPECT_EQ(ExpectedReleaseSymbols, SerializedSemantics->Symbols);
  for (StringRef ExactName :
       {"init_module", "cleanup_module", "__cfi_check", "__cfi_check_fail",
        "__kcfi_typeid_sample", "__typeid__sample_global_addr", "printk",
        "__start_alloc_tags", "__stop_alloc_tags"})
    EXPECT_TRUE(llvm::any_of(SerializedSemantics->Symbols,
                             [&](const ELFSymbolSemantics &Symbol) {
                               return Symbol.Name == ExactName;
                             }))
        << ExactName.str();
  EXPECT_EQ(llvm::count_if(SerializedSemantics->Symbols,
                           [](const ELFSymbolSemantics &Symbol) {
                             return Symbol.Name == "printk";
                           }),
            1U);
  EXPECT_EQ(llvm::count_if(SerializedSemantics->Relocations,
                           [](const ELFRelocationSemantics &Relocation) {
                             return Relocation.Target == "printk";
                           }),
            2U);
  EXPECT_TRUE(llvm::any_of(
      SerializedSemantics->Symbols, [](const ELFSymbolSemantics &Symbol) {
        return Symbol.Type == ELF::STT_OBJECT &&
               Symbol.Binding == ELF::STB_WEAK &&
               Symbol.Visibility == ELF::STV_HIDDEN && Symbol.Size == 8;
      }));
  EXPECT_TRUE(llvm::any_of(
      SerializedSemantics->Symbols, [](const ELFSymbolSemantics &Symbol) {
        return Symbol.Type == ELF::STT_OBJECT &&
               Symbol.Binding == ELF::STB_GLOBAL &&
               Symbol.Visibility == ELF::STV_DEFAULT && Symbol.Size == 16 &&
               Symbol.Section == ".bss";
      }));
  EXPECT_FALSE(llvm::any_of(SerializedSemantics->Symbols,
                            [](const ELFSymbolSemantics &Symbol) {
                              return isAArch64MappingSymbol(Symbol.Name);
                            }));
  EXPECT_FALSE(containsBytes(Serialized, "$d."));
  EXPECT_FALSE(containsBytes(Serialized, "$x."));
  for (StringRef Stale :
       {"custom_release_entry", "custom_local_a", "custom_local_b",
        "writer_local_a", "writer_local_b", "code_8", "code_8_1",
        "ordinary_hidden_weak", "ordinary_bss"})
    EXPECT_FALSE(containsBytes(Serialized, Stale)) << Stale.str();

  Error SerializedAudit = verifyFinalAndroidKernelModuleImage(
      Serialized, Policy, "custom-provider serialized release image");
  ASSERT_FALSE(SerializedAudit) << errorText(std::move(SerializedAudit));

  ObjectOutputDestination RepeatDestination = ObjectOutputDestination::memory(
      "custom-release-repeat.ko", UINT64_C(1) << 20);
  RepeatDestination.WritePolicy = ObjectWritePolicy::AndroidKernelRelease;
  RepeatDestination.DropDebugInfo = true;
  auto RepeatImage =
      (*Writer)->beginWrite(Scope.task(), *Partial->Object, RepeatDestination);
  ASSERT_TRUE(static_cast<bool>(RepeatImage))
      << errorText(RepeatImage.takeError());
  auto RepeatBytes = (*RepeatImage)->pendingBytes();
  ASSERT_TRUE(static_cast<bool>(RepeatBytes))
      << errorText(RepeatBytes.takeError());
  EXPECT_EQ(Serialized,
            (std::vector<uint8_t>(RepeatBytes->begin(), RepeatBytes->end())));
  EXPECT_FALSE((*RepeatImage)->abort());
  EXPECT_FALSE((*SerializedReferenceImage)->abort());
  EXPECT_FALSE((*Image)->finish());
  EXPECT_FALSE((*Image)->verify());
  EXPECT_FALSE((*Image)->abort());
}

TEST(AndroidKernelModuleFinalizerTest,
     DirectReleaseWriterRejectsUnsupportedArchitectureBeforeOpeningSink) {
  initializeBuiltinTargets();
  LinkTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());

  const BuiltinTargetRoute *X86ELFRoute = nullptr;
  for (const BuiltinTargetRoute &Route : builtinTargetRoutes()) {
    const Triple Parsed(Triple::normalize(Route.CanonicalTriple));
    if (Route.SupportsObject &&
        Route.ObjectFormat == BuiltinObjectFormat::ELF &&
        Parsed.getArch() == Triple::x86_64) {
      X86ELFRoute = &Route;
      break;
    }
  }
  ASSERT_NE(X86ELFRoute, nullptr);

  auto Graph = makeBuiltinObject(*X86ELFRoute, "ordinary_name");
  ASSERT_NE(Graph, nullptr);
  auto Snapshot = PluginTargetRegistry::freeze(
      ArrayRef<PluginTargetRegistrationView>(), PluginTargetRequest{});
  ASSERT_TRUE(static_cast<bool>(Snapshot)) << errorText(Snapshot.takeError());
  auto Writer = ObjectWriterProvider::create(*Snapshot);
  ASSERT_TRUE(static_cast<bool>(Writer)) << errorText(Writer.takeError());

  ObjectOutputDestination Destination = ObjectOutputDestination::memory(
      "failed-serialized-release.ko", UINT64_C(1) << 20);
  Destination.WritePolicy = ObjectWritePolicy::AndroidKernelRelease;
  auto Image = (*Writer)->beginWrite(Scope.task(), *Graph, Destination);
  ASSERT_FALSE(static_cast<bool>(Image));
  EXPECT_NE(errorText(Image.takeError()).find("requires AArch64 ELF"),
            std::string::npos);
  EXPECT_FALSE(
      findPluginMemoryOutput(Scope.task(), "failed-serialized-release.ko")
          .has_value());
}

} // namespace
