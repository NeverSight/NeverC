#include "PluginObjectMergeTestSupport.h"

namespace {

TEST(AndroidKernelReleaseIdentitySealTest,
     GraphRejectsOtherwiseValidExactABINameReplacement) {
  auto Graph = makeAndroidObject(1);
  ASSERT_NE(Graph, nullptr);
  PluginObjectSection &Text = Graph->sections().front();
  Text.Name = ".text";
  Text.Kind = NEVERC_OBJECT_SECTION_KIND_TEXT;
  Text.Flags =
      NEVERC_OBJECT_SECTION_ALLOCATED | NEVERC_OBJECT_SECTION_EXECUTABLE;
  Text.Data = {UINT8_C(0)};

  PluginObjectSymbol Entry;
  Entry.ID = Graph->allocateEntityID();
  Entry.Name = "init_module";
  Entry.Binding = NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL;
  Entry.Type = NEVERC_OBJECT_SYMBOL_TYPE_FUNCTION;
  Entry.Definition = NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED;
  Entry.SectionID = Text.ID;
  Entry.Size = 1;
  const uint64_t EntryID = Entry.ID;
  Graph->symbols().push_back(std::move(Entry));
  ASSERT_FALSE(verifyPluginObjectGraph(*Graph));

  AndroidKernelModuleFinalizationPolicy Policy;
  Policy.StripUnneededSymbols = true;
  Error Finalize = finalizeAndroidKernelModuleObjectGraph(
      *Graph, Policy, "test graph exact-name manifest");
  ASSERT_FALSE(Finalize) << errorText(std::move(Finalize));

  auto Seal = captureAndroidKernelReleaseGraphIdentitySeal(
      *Graph, Policy.SymbolNameState, "test finalized authoritative graph");
  ASSERT_TRUE(static_cast<bool>(Seal)) << errorText(Seal.takeError());

  PluginObjectGraph Mutated(*Graph);
  PluginObjectSymbol *MutatedEntry = Mutated.findSymbol(EntryID);
  ASSERT_NE(MutatedEntry, nullptr);
  MutatedEntry->Name = "__cfi_check";
  Mutated.advanceGeneration();

  Error Standalone = verifyFinalAndroidKernelModuleObjectGraph(
      Mutated, Policy, "test structurally valid exact-name replacement");
  EXPECT_FALSE(Standalone) << errorText(std::move(Standalone));

  Error ExactName = verifyAndroidKernelReleaseGraphIdentitySeal(
      Mutated, Policy.SymbolNameState, *Seal,
      "test immutable graph exact-name contract");
  ASSERT_TRUE(static_cast<bool>(ExactName));
  const std::string Message = errorText(std::move(ExactName));
  EXPECT_NE(Message.find("immutable release identity seal"), std::string::npos)
      << Message;
  EXPECT_NE(Message.find("init_module"), std::string::npos) << Message;
  EXPECT_NE(Message.find("__cfi_check"), std::string::npos) << Message;
}

TEST(AndroidKernelReleaseIdentitySealTest,
     GraphRejectsOtherwiseValidMappedSymbolRelayout) {
  auto Graph = makeAndroidObject(1);
  ASSERT_NE(Graph, nullptr);
  PluginObjectSection &Text = Graph->sections().front();
  Text.Name = ".text";
  Text.Kind = NEVERC_OBJECT_SECTION_KIND_TEXT;
  Text.Flags =
      NEVERC_OBJECT_SECTION_ALLOCATED | NEVERC_OBJECT_SECTION_EXECUTABLE;
  Text.Data.resize(32);

  PluginObjectSymbol Function;
  Function.ID = Graph->allocateEntityID();
  Function.Name = "ordinary_worker";
  Function.Binding = NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL;
  Function.Type = NEVERC_OBJECT_SYMBOL_TYPE_FUNCTION;
  Function.Definition = NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED;
  Function.SectionID = Text.ID;
  Function.Size = 4;
  const uint64_t FunctionID = Function.ID;
  Graph->symbols().push_back(std::move(Function));
  ASSERT_FALSE(verifyPluginObjectGraph(*Graph));

  AndroidKernelModuleFinalizationPolicy Policy;
  Policy.StripUnneededSymbols = true;
  neverc::AndroidKernelReleaseSymbolMap SymbolMap;
  Error Finalize = finalizeAndroidKernelModuleObjectGraph(
      *Graph, Policy, "test mapped graph identity baseline", &SymbolMap);
  ASSERT_FALSE(Finalize) << errorText(std::move(Finalize));
  ASSERT_EQ(SymbolMap.Symbols.size(), 1u);
  EXPECT_EQ(SymbolMap.Symbols.front().OriginalName, "ordinary_worker");
  EXPECT_EQ(SymbolMap.Symbols.front().ReleaseName, "fn_0");

  auto Seal = captureAndroidKernelReleaseGraphIdentitySeal(
      *Graph, Policy.SymbolNameState, "test mapped graph identity baseline");
  ASSERT_TRUE(static_cast<bool>(Seal)) << errorText(Seal.takeError());

  PluginObjectGraph Mutated(*Graph);
  PluginObjectSymbol *MutatedFunction = Mutated.findSymbol(FunctionID);
  ASSERT_NE(MutatedFunction, nullptr);
  MutatedFunction->Value = 8;
  MutatedFunction->Name = "fn_8";
  Mutated.advanceGeneration();

  Error Standalone = verifyFinalAndroidKernelModuleObjectGraph(
      Mutated, Policy, "test structurally valid mapped symbol relayout");
  ASSERT_FALSE(Standalone) << errorText(std::move(Standalone));

  Error ImmutableIdentity = verifyAndroidKernelReleaseGraphIdentitySeal(
      Mutated, Policy.SymbolNameState, *Seal,
      "test immutable mapped graph identity contract");
  ASSERT_TRUE(static_cast<bool>(ImmutableIdentity));
  const std::string Message = errorText(std::move(ImmutableIdentity));
  EXPECT_NE(Message.find("immutable release identity seal"), std::string::npos)
      << Message;
  EXPECT_NE(Message.find("fn_0"), std::string::npos) << Message;
  EXPECT_NE(Message.find("fn_8"), std::string::npos) << Message;
}

TEST(AndroidKernelReleaseIdentitySealTest,
     GraphBindsExactUndefinedNamesToTheirRelocationOwners) {
  auto Graph = makeAndroidObject(1);
  ASSERT_NE(Graph, nullptr);
  PluginObjectSection &Text = Graph->sections().front();
  Text.Name = ".text";
  Text.Kind = NEVERC_OBJECT_SECTION_KIND_TEXT;
  Text.Flags =
      NEVERC_OBJECT_SECTION_ALLOCATED | NEVERC_OBJECT_SECTION_EXECUTABLE;
  Text.Data.resize(16);

  const auto AddImport = [&](StringRef Name) {
    PluginObjectSymbol Symbol;
    Symbol.ID = Graph->allocateEntityID();
    Symbol.Name = Name.str();
    Symbol.Binding = NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL;
    Symbol.Type = NEVERC_OBJECT_SYMBOL_TYPE_NO_TYPE;
    Symbol.Definition = NEVERC_OBJECT_SYMBOL_DEFINITION_UNDEFINED;
    const uint64_t ID = Symbol.ID;
    Graph->symbols().push_back(std::move(Symbol));
    return ID;
  };
  const uint64_t FirstImport = AddImport("kernel_one");
  const uint64_t SecondImport = AddImport("kernel_two");

  const auto AddRelocation = [&](uint64_t Offset, uint64_t TargetSymbolID) {
    PluginObjectRelocation Relocation;
    Relocation.ID = Graph->allocateEntityID();
    Relocation.SectionID = Text.ID;
    Relocation.Offset = Offset;
    Relocation.Kind = NEVERC_OBJECT_RELOCATION_ABSOLUTE;
    Relocation.TargetKind = NEVERC_OBJECT_RELOCATION_TARGET_SYMBOL;
    Relocation.TargetSymbolID = TargetSymbolID;
    Relocation.Width = 64;
    Relocation.Extension.Owner = TestFormatID;
    Relocation.Extension.Version =
        neverc::plugin::builtinext::RelocationVersion;
    const SmallVector<uint8_t, 80> NativeFacts = makeELFRelocationExtension(
        neverc::plugin::builtinext::RelocationVersion, ELF::R_AARCH64_ABS64,
        "R_AARCH64_ABS64");
    Relocation.Extension.Bytes.assign(NativeFacts.begin(), NativeFacts.end());
    Graph->relocations().push_back(std::move(Relocation));
  };
  AddRelocation(0, FirstImport);
  AddRelocation(8, SecondImport);
  ASSERT_FALSE(verifyPluginObjectGraph(*Graph));

  AndroidKernelModuleFinalizationPolicy Policy;
  Policy.StripUnneededSymbols = true;
  Error Finalize = finalizeAndroidKernelModuleObjectGraph(
      *Graph, Policy, "test exact undefined owner identities");
  ASSERT_FALSE(Finalize) << errorText(std::move(Finalize));

  auto Seal = captureAndroidKernelReleaseGraphIdentitySeal(
      *Graph, Policy.SymbolNameState,
      "test exact undefined owner identity baseline");
  ASSERT_TRUE(static_cast<bool>(Seal)) << errorText(Seal.takeError());

  PluginObjectGraph Mutated(*Graph);
  std::swap(Mutated.findSymbol(FirstImport)->Name,
            Mutated.findSymbol(SecondImport)->Name);
  Mutated.advanceGeneration();

  Error Standalone = verifyFinalAndroidKernelModuleObjectGraph(
      Mutated, Policy, "test structurally valid undefined-name exchange");
  ASSERT_FALSE(Standalone) << errorText(std::move(Standalone));

  Error ImmutableIdentity = verifyAndroidKernelReleaseGraphIdentitySeal(
      Mutated, Policy.SymbolNameState, *Seal,
      "test immutable graph owner identity contract");
  ASSERT_TRUE(static_cast<bool>(ImmutableIdentity));
  const std::string Message = errorText(std::move(ImmutableIdentity));
  EXPECT_NE(Message.find("immutable release identity seal"), std::string::npos)
      << Message;
  EXPECT_NE(Message.find("kernel_one"), std::string::npos) << Message;
  EXPECT_NE(Message.find("kernel_two"), std::string::npos) << Message;
}

TEST(AndroidKernelReleaseIdentitySealTest,
     GraphBindsEveryRetainedSectionOwnerNameAndFinalOrdinal) {
  auto Graph = makeAndroidObject(2);
  ASSERT_NE(Graph, nullptr);
  auto Section = Graph->sections().begin();
  Section->Name = ".text";
  Section->Kind = NEVERC_OBJECT_SECTION_KIND_TEXT;
  Section->Flags =
      NEVERC_OBJECT_SECTION_ALLOCATED | NEVERC_OBJECT_SECTION_EXECUTABLE;
  ++Section;
  Section->Name = ".rodata";
  Section->Kind = NEVERC_OBJECT_SECTION_KIND_READ_ONLY_DATA;
  Section->Flags = NEVERC_OBJECT_SECTION_ALLOCATED;

  AndroidKernelModuleFinalizationPolicy Policy;
  Policy.StripUnneededSymbols = true;
  Error Finalize = finalizeAndroidKernelModuleObjectGraph(
      *Graph, Policy, "test retained section identity baseline");
  ASSERT_FALSE(Finalize) << errorText(std::move(Finalize));
  auto Seal = captureAndroidKernelReleaseGraphIdentitySeal(
      *Graph, Policy.SymbolNameState,
      "test retained section identity baseline");
  ASSERT_TRUE(static_cast<bool>(Seal)) << errorText(Seal.takeError());

  PluginObjectGraph Renamed(*Graph);
  Renamed.sections().front().Name = ".code";
  Renamed.advanceGeneration();
  Error StandaloneRename = verifyFinalAndroidKernelModuleObjectGraph(
      Renamed, Policy, "test structurally valid section rename");
  ASSERT_FALSE(StandaloneRename) << errorText(std::move(StandaloneRename));
  Error Rename = verifyAndroidKernelReleaseGraphIdentitySeal(
      Renamed, Policy.SymbolNameState, *Seal,
      "test immutable graph section-name contract");
  ASSERT_TRUE(static_cast<bool>(Rename));
  const std::string RenameMessage = errorText(std::move(Rename));
  EXPECT_NE(RenameMessage.find("release layout identity seal"),
            std::string::npos)
      << RenameMessage;
  EXPECT_NE(RenameMessage.find(".text"), std::string::npos) << RenameMessage;
  EXPECT_NE(RenameMessage.find(".code"), std::string::npos) << RenameMessage;

  PluginObjectGraph Reordered(*Graph);
  auto Second = std::next(Reordered.sections().begin());
  Reordered.sections().splice(Reordered.sections().begin(),
                              Reordered.sections(), Second);
  Reordered.advanceGeneration();
  Error StandaloneOrder = verifyFinalAndroidKernelModuleObjectGraph(
      Reordered, Policy, "test structurally valid section reorder");
  ASSERT_FALSE(StandaloneOrder) << errorText(std::move(StandaloneOrder));
  Error Order = verifyAndroidKernelReleaseGraphIdentitySeal(
      Reordered, Policy.SymbolNameState, *Seal,
      "test immutable graph section-order contract");
  ASSERT_TRUE(static_cast<bool>(Order));
  EXPECT_NE(errorText(std::move(Order)).find("final ordinal"),
            std::string::npos);
}

TEST(AndroidKernelReleaseIdentitySealTest,
     GraphAuthorityRejectsNamedSectionSymbolItCannotSerialize) {
  auto Graph = makeAndroidObject(1);
  ASSERT_NE(Graph, nullptr);
  PluginObjectSection &Text = Graph->sections().front();
  Text.Name = ".text";
  Text.Kind = NEVERC_OBJECT_SECTION_KIND_TEXT;
  Text.Flags =
      NEVERC_OBJECT_SECTION_ALLOCATED | NEVERC_OBJECT_SECTION_EXECUTABLE;
  Text.Data.resize(8);

  PluginObjectSymbol SectionSymbol;
  SectionSymbol.ID = Graph->allocateEntityID();
  SectionSymbol.Name = "section_key";
  SectionSymbol.Binding = NEVERC_OBJECT_SYMBOL_BINDING_LOCAL;
  SectionSymbol.Type = NEVERC_OBJECT_SYMBOL_TYPE_SECTION;
  SectionSymbol.Definition = NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED;
  SectionSymbol.SectionID = Text.ID;
  const uint64_t SectionSymbolID = SectionSymbol.ID;
  Graph->symbols().push_back(std::move(SectionSymbol));

  PluginObjectRelocation Relocation;
  Relocation.ID = Graph->allocateEntityID();
  Relocation.SectionID = Text.ID;
  Relocation.Kind = NEVERC_OBJECT_RELOCATION_ABSOLUTE;
  Relocation.TargetKind = NEVERC_OBJECT_RELOCATION_TARGET_SYMBOL;
  Relocation.TargetSymbolID = SectionSymbolID;
  Relocation.Width = 64;
  Graph->relocations().push_back(std::move(Relocation));
  ASSERT_FALSE(verifyPluginObjectGraph(*Graph));

  AndroidKernelModuleFinalizationPolicy Policy;
  Policy.StripUnneededSymbols = true;
  Error Standalone = verifyFinalAndroidKernelModuleObjectGraph(
      *Graph, Policy, "test named SECTION structural graph");
  ASSERT_FALSE(Standalone) << errorText(std::move(Standalone));

  auto Seal = captureAndroidKernelReleaseGraphIdentitySeal(
      *Graph, Policy.SymbolNameState,
      "test portable named SECTION authority boundary");
  ASSERT_FALSE(static_cast<bool>(Seal));
  const std::string Message = errorText(Seal.takeError());
  EXPECT_NE(Message.find("section_key"), std::string::npos) << Message;
  EXPECT_NE(Message.find("SECTION type"), std::string::npos) << Message;
  EXPECT_NE(Message.find("cannot round-trip"), std::string::npos) << Message;
}

TEST(AndroidKernelReleaseIdentitySealTest,
     ImageBindsExactUndefinedNamesToRawSymbolTableSlots) {
  initializeBuiltinTargets();
  LinkTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());
  const BuiltinTargetRoute *AndroidRoute = findAndroidAArch64ObjectRoute();
  ASSERT_NE(AndroidRoute, nullptr);
  auto Input = assembleAndroidReleaseInputWithTwoImports(*AndroidRoute);
  ASSERT_TRUE(static_cast<bool>(Input)) << errorText(Input.takeError());
  auto Image = mergeFinalAndroidReleaseImage(
      Scope, *AndroidRoute, *Input, "memory://identity-slot-imports.o");
  ASSERT_TRUE(static_cast<bool>(Image)) << errorText(Image.takeError());

  AndroidKernelModuleFinalizationPolicy Policy;
  Policy.DropDebugInfo = true;
  Policy.StripUnneededSymbols = true;
  Policy.SymbolNameState = AndroidKernelSymbolNameState::CanonicalRelease;
  ASSERT_FALSE(verifyFinalAndroidKernelModuleImage(
      *Image, Policy, "test exact import-slot baseline"));
  auto Seal = captureAndroidKernelReleaseImageIdentitySeal(
      *Image, "test exact import-slot baseline");
  ASSERT_TRUE(static_cast<bool>(Seal)) << errorText(Seal.takeError());

  auto Before = readELFSemantics(*Image);
  ASSERT_TRUE(static_cast<bool>(Before)) << errorText(Before.takeError());
  Error Swap = swapELF64SymbolNameBytes(*Image, "kernel_one", "kernel_two");
  ASSERT_FALSE(Swap) << errorText(std::move(Swap));
  auto After = readELFSemantics(*Image);
  ASSERT_TRUE(static_cast<bool>(After)) << errorText(After.takeError());
  const auto RelocationTarget = [](const ELFSemantics &Semantics,
                                   uint64_t Offset) -> StringRef {
    const auto Found = llvm::find_if(
        Semantics.Relocations,
        [Offset](const ELFRelocationSemantics &Relocation) {
          return Relocation.Section == ".text" && Relocation.Offset == Offset;
        });
    return Found == Semantics.Relocations.end() ? StringRef() : Found->Target;
  };
  EXPECT_EQ(RelocationTarget(*Before, 0), "kernel_one");
  EXPECT_EQ(RelocationTarget(*Before, 8), "kernel_two");
  EXPECT_EQ(RelocationTarget(*After, 0), "kernel_two");
  EXPECT_EQ(RelocationTarget(*After, 8), "kernel_one");

  Error Standalone = verifyFinalAndroidKernelModuleImage(
      *Image, Policy, "test structurally valid exact import-slot exchange");
  ASSERT_FALSE(Standalone) << errorText(std::move(Standalone));
  Error ImmutableIdentity = verifyAndroidKernelReleaseImageIdentitySeal(
      *Image, *Seal, "test immutable image symbol-slot contract");
  ASSERT_TRUE(static_cast<bool>(ImmutableIdentity));
  const std::string Message = errorText(std::move(ImmutableIdentity));
  EXPECT_NE(Message.find("symbol-table slot"), std::string::npos) << Message;
  EXPECT_NE(Message.find("kernel_one"), std::string::npos) << Message;
  EXPECT_NE(Message.find("kernel_two"), std::string::npos) << Message;
}

TEST(AndroidKernelReleaseIdentitySealTest,
     ImageBindsNamedSectionSymbolsToRawSymbolTableSlots) {
  initializeBuiltinTargets();
  LinkTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());
  const BuiltinTargetRoute *AndroidRoute = findAndroidAArch64ObjectRoute();
  ASSERT_NE(AndroidRoute, nullptr);
  auto Input = assembleAndroidReleaseInputWithNamedSection(*AndroidRoute);
  ASSERT_TRUE(static_cast<bool>(Input)) << errorText(Input.takeError());
  auto Image = mergeFinalAndroidReleaseImage(
      Scope, *AndroidRoute, *Input, "memory://identity-named-section.o");
  ASSERT_TRUE(static_cast<bool>(Image)) << errorText(Image.takeError());
  auto ReadOnlySection = findELF64SectionIndex(*Image, ".rodata");
  ASSERT_TRUE(static_cast<bool>(ReadOnlySection))
      << errorText(ReadOnlySection.takeError());
  Error MakeNamedSection = patchELF64Symbol(
      *Image, "section_key",
      [SectionIndex = *ReadOnlySection](object::ELF64LE::Sym &Symbol) {
        Symbol.setBindingAndType(Symbol.getBinding(), ELF::STT_SECTION);
        Symbol.st_shndx = SectionIndex;
        Symbol.st_value = 0;
        Symbol.st_size = 0;
      });
  ASSERT_FALSE(MakeNamedSection) << errorText(std::move(MakeNamedSection));

  AndroidKernelModuleFinalizationPolicy Policy;
  Policy.DropDebugInfo = true;
  Policy.StripUnneededSymbols = true;
  Policy.SymbolNameState = AndroidKernelSymbolNameState::CanonicalRelease;
  ASSERT_FALSE(verifyFinalAndroidKernelModuleImage(
      *Image, Policy, "test named SECTION baseline"));
  auto Before = readELFSemantics(*Image);
  ASSERT_TRUE(static_cast<bool>(Before)) << errorText(Before.takeError());
  const auto NamedSection =
      llvm::find_if(Before->Symbols, [](const ELFSymbolSemantics &Symbol) {
        return Symbol.Name == "section_key";
      });
  ASSERT_NE(NamedSection, Before->Symbols.end());
  EXPECT_EQ(NamedSection->Type, ELF::STT_SECTION);
  auto Seal = captureAndroidKernelReleaseImageIdentitySeal(
      *Image, "test named SECTION baseline");
  ASSERT_TRUE(static_cast<bool>(Seal)) << errorText(Seal.takeError());

  Error Rename =
      replaceELF64SymbolNameBytes(*Image, "section_key", "segment_key");
  ASSERT_FALSE(Rename) << errorText(std::move(Rename));
  Error Standalone = verifyFinalAndroidKernelModuleImage(
      *Image, Policy, "test structurally valid named SECTION replacement");
  ASSERT_FALSE(Standalone) << errorText(std::move(Standalone));
  Error ImmutableIdentity = verifyAndroidKernelReleaseImageIdentitySeal(
      *Image, *Seal, "test immutable named SECTION slot contract");
  ASSERT_TRUE(static_cast<bool>(ImmutableIdentity));
  const std::string Message = errorText(std::move(ImmutableIdentity));
  EXPECT_NE(Message.find("symbol-table slot"), std::string::npos) << Message;
  EXPECT_NE(Message.find("section_key"), std::string::npos) << Message;
  EXPECT_NE(Message.find("segment_key"), std::string::npos) << Message;
}

TEST(AndroidKernelReleaseIdentitySealTest,
     PrePostWritePipelineAcceptsStableNamedSectionAndRejectsItsRename) {
  initializeBuiltinTargets();
  const BuiltinTargetRoute *AndroidRoute = findAndroidAArch64ObjectRoute();
  ASSERT_NE(AndroidRoute, nullptr);
  LinkTaskScope BuildScope;
  ASSERT_TRUE(BuildScope.initialize());
  auto Input = assembleAndroidReleaseInputWithNamedSection(*AndroidRoute);
  ASSERT_TRUE(static_cast<bool>(Input)) << errorText(Input.takeError());
  auto Image = mergeFinalAndroidReleaseImage(
      BuildScope, *AndroidRoute, *Input,
      "memory://pipeline-named-section-baseline.o");
  ASSERT_TRUE(static_cast<bool>(Image)) << errorText(Image.takeError());
  auto ReadOnlySection = findELF64SectionIndex(*Image, ".rodata");
  ASSERT_TRUE(static_cast<bool>(ReadOnlySection))
      << errorText(ReadOnlySection.takeError());
  Error MakeNamedSection = patchELF64Symbol(
      *Image, "section_key",
      [SectionIndex = *ReadOnlySection](object::ELF64LE::Sym &Symbol) {
        Symbol.setBindingAndType(Symbol.getBinding(), ELF::STT_SECTION);
        Symbol.st_shndx = SectionIndex;
        Symbol.st_value = 0;
        Symbol.st_size = 0;
      });
  ASSERT_FALSE(MakeNamedSection) << errorText(std::move(MakeNamedSection));

  AndroidKernelModuleFinalizationPolicy Policy;
  Policy.DropDebugInfo = true;
  Policy.StripUnneededSymbols = true;
  Policy.SymbolNameState = AndroidKernelSymbolNameState::CanonicalRelease;
  ASSERT_FALSE(verifyFinalAndroidKernelModuleImage(
      *Image, Policy, "test stable named SECTION pipeline baseline"));

  const auto Run = [&](LinkTaskScope &Scope, StringRef OutputName)
      -> Expected<std::shared_ptr<PluginObjectImage>> {
    auto Snapshot = PluginTargetRegistry::freeze(
        ArrayRef<PluginTargetRegistrationView>(), PluginTargetRequest{});
    if (!Snapshot)
      return Snapshot.takeError();
    auto Reader = ObjectReaderProvider::create(*Snapshot);
    if (!Reader)
      return Reader.takeError();
    auto Target = makeBuiltinTargetKey(*AndroidRoute);
    if (!Target)
      return Target.takeError();
    auto Graph = (*Reader)->read(Scope.task(), *Image,
                                 "memory://named-section-pipeline-input.ko",
                                 *Target, AndroidRoute->ObjectFormatID);
    if (!Graph)
      return Graph.takeError();
    auto Pipeline = ObjectPhasePipeline::create(Scope.task(), *Snapshot);
    if (!Pipeline)
      return Pipeline.takeError();
    ObjectPhaseSemanticValidators Validators;
    Validators.BindPrePostWriteImage = [Policy](ArrayRef<uint8_t> Baseline)
        -> Expected<ObjectImageSemanticValidator> {
      if (Error E = verifyFinalAndroidKernelModuleImage(
              Baseline, Policy, "test trusted named SECTION baseline"))
        return std::move(E);
      auto Seal = captureAndroidKernelReleaseImageIdentitySeal(
          Baseline, "test trusted named SECTION baseline");
      if (!Seal)
        return Seal.takeError();
      return ObjectImageSemanticValidator([Policy, Seal = std::move(*Seal)](
                                              ArrayRef<uint8_t> Candidate) {
        if (Error E = verifyFinalAndroidKernelModuleImage(
                Candidate, Policy, "test named SECTION post-write candidate"))
          return E;
        return verifyAndroidKernelReleaseImageIdentitySeal(
            Candidate, Seal, "test immutable named SECTION pipeline contract");
      });
    };
    return (*Pipeline)->executeNative(
        **Graph, *Image,
        ObjectOutputDestination::memory(OutputName, UINT64_C(1) << 20),
        std::move(Validators));
  };

  LinkTaskScope StableScope;
  ASSERT_TRUE(StableScope.initialize());
  auto Stable = Run(StableScope, "stable-named-section.ko");
  ASSERT_TRUE(static_cast<bool>(Stable)) << errorText(Stable.takeError());
  EXPECT_TRUE(
      findPluginMemoryOutput(StableScope.task(), "stable-named-section.ko")
          .has_value());

  LinkTaskScope MutatingScope;
  ASSERT_TRUE(MutatingScope.initialize(
      NEVERC_TEST_OBJECT_SECTION_SYMBOL_CORRUPT_PLUGIN));
  auto Mutated = Run(MutatingScope, "mutated-named-section.ko");
  ASSERT_FALSE(static_cast<bool>(Mutated));
  const std::string Message = errorText(Mutated.takeError());
  EXPECT_NE(Message.find("immutable release identity seal"), std::string::npos)
      << Message;
  EXPECT_FALSE(
      findPluginMemoryOutput(MutatingScope.task(), "mutated-named-section.ko")
          .has_value());
}

TEST(AndroidKernelReleaseIdentitySealTest,
     ImageBindsEveryRetainedLogicalSectionNameToItsOrdinal) {
  initializeBuiltinTargets();
  LinkTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());
  const BuiltinTargetRoute *AndroidRoute = findAndroidAArch64ObjectRoute();
  ASSERT_NE(AndroidRoute, nullptr);
  auto Input = assembleAndroidReleaseInputWithInitPLT(*AndroidRoute);
  ASSERT_TRUE(static_cast<bool>(Input)) << errorText(Input.takeError());
  auto Image = mergeFinalAndroidReleaseImage(Scope, *AndroidRoute, *Input,
                                             "memory://identity-init-plt.o");
  ASSERT_TRUE(static_cast<bool>(Image)) << errorText(Image.takeError());

  AndroidKernelModuleFinalizationPolicy Policy;
  Policy.DropDebugInfo = true;
  Policy.StripUnneededSymbols = true;
  Policy.SymbolNameState = AndroidKernelSymbolNameState::CanonicalRelease;
  ASSERT_FALSE(verifyFinalAndroidKernelModuleImage(
      *Image, Policy, "test retained image section baseline"));
  auto Seal = captureAndroidKernelReleaseImageIdentitySeal(
      *Image, "test retained image section baseline");
  ASSERT_TRUE(static_cast<bool>(Seal)) << errorText(Seal.takeError());

  Error Rename = replaceELF64SectionNameBytes(*Image, ".init.plt", ".hide.plt");
  ASSERT_FALSE(Rename) << errorText(std::move(Rename));
  Error Standalone = verifyFinalAndroidKernelModuleImage(
      *Image, Policy, "test structurally valid image section rename");
  ASSERT_FALSE(Standalone) << errorText(std::move(Standalone));
  Error ImmutableIdentity = verifyAndroidKernelReleaseImageIdentitySeal(
      *Image, *Seal, "test immutable image section-name contract");
  ASSERT_TRUE(static_cast<bool>(ImmutableIdentity));
  const std::string Message = errorText(std::move(ImmutableIdentity));
  EXPECT_NE(Message.find("release layout identity seal"), std::string::npos)
      << Message;
  EXPECT_NE(Message.find(".init.plt"), std::string::npos) << Message;
  EXPECT_NE(Message.find(".hide.plt"), std::string::npos) << Message;
}

} // namespace
