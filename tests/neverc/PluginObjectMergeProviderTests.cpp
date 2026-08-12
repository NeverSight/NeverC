#include "PluginObjectMergeTestSupport.h"

namespace {

TEST(PluginObjectGraphImportTest,
     PreservesNormalizedEntitiesExtensionsAndOrigins) {
  auto SourceTarget = makeTargetKey();
  auto LinkTarget = makeTargetKey();
  ASSERT_TRUE(static_cast<bool>(SourceTarget))
      << errorText(SourceTarget.takeError());
  ASSERT_TRUE(static_cast<bool>(LinkTarget))
      << errorText(LinkTarget.takeError());
  PluginObjectGraph Source(std::move(*SourceTarget));

  PluginObjectComdat Comdat;
  Comdat.ID = Source.allocateEntityID();
  Comdat.Name = "answer";
  Comdat.Selection = NEVERC_OBJECT_COMDAT_EXACT_MATCH;
  Comdat.Extension.Owner = TestFormatID;
  Comdat.Extension.Version = 3;
  Comdat.Extension.Bytes = {0xaa, 0xbb};
  const uint64_t ObjectComdatID = Comdat.ID;
  Source.comdats().push_back(std::move(Comdat));

  PluginObjectSection Text;
  Text.ID = Source.allocateEntityID();
  Text.Name = ".text";
  Text.Kind = NEVERC_OBJECT_SECTION_KIND_TEXT;
  Text.Flags = NEVERC_OBJECT_SECTION_ALLOCATED |
               NEVERC_OBJECT_SECTION_EXECUTABLE | NEVERC_OBJECT_SECTION_RETAIN;
  Text.Alignment = 16;
  Text.Data = {0, 0, 0, 0, 0, 0, 0, 0};
  Text.ComdatID = ObjectComdatID;
  Text.Extension.Owner = TestFormatID;
  Text.Extension.Version = 7;
  Text.Extension.Bytes = {1, 2, 3};
  const uint64_t ObjectSectionID = Text.ID;
  Source.sections().push_back(std::move(Text));

  PluginObjectSymbol Defined;
  Defined.ID = Source.allocateEntityID();
  Defined.Name = "answer";
  Defined.Binding = NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL;
  Defined.Visibility = NEVERC_OBJECT_SYMBOL_VISIBILITY_PROTECTED;
  Defined.Type = NEVERC_OBJECT_SYMBOL_TYPE_FUNCTION;
  Defined.Definition = NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED;
  Defined.SectionID = ObjectSectionID;
  Defined.Value = 0;
  Defined.Size = 8;
  Defined.Alignment = 1;
  Defined.ComdatID = ObjectComdatID;
  Defined.Flags = NEVERC_OBJECT_SYMBOL_EXPORTED;
  const uint64_t DefinedID = Defined.ID;
  Source.symbols().push_back(std::move(Defined));

  PluginObjectSymbol Undefined;
  Undefined.ID = Source.allocateEntityID();
  Undefined.Name = "external";
  Undefined.Binding = NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL;
  Undefined.Definition = NEVERC_OBJECT_SYMBOL_DEFINITION_UNDEFINED;
  Undefined.Alignment = 1;
  Undefined.Flags = NEVERC_OBJECT_SYMBOL_IMPORTED;
  const uint64_t UndefinedID = Undefined.ID;
  Source.symbols().push_back(std::move(Undefined));

  PluginObjectRelocation Relocation;
  Relocation.ID = Source.allocateEntityID();
  Relocation.SectionID = ObjectSectionID;
  Relocation.Offset = 0;
  Relocation.Kind = NEVERC_OBJECT_RELOCATION_PC_RELATIVE;
  Relocation.TargetKind = NEVERC_OBJECT_RELOCATION_TARGET_SYMBOL;
  Relocation.Width = 32;
  Relocation.IsPCRelative = true;
  Relocation.IsSigned = true;
  Relocation.Addend = -4;
  Relocation.TargetSymbolID = UndefinedID;
  const uint64_t RelocationID = Relocation.ID;
  Source.relocations().push_back(std::move(Relocation));

  ASSERT_FALSE(verifyPluginObjectGraph(Source));

  PluginLinkGraph Link(std::move(*LinkTarget));
  PluginLinkInput Input;
  Input.Kind = NEVERC_LINK_INPUT_OBJECT;
  Input.LogicalURI = "vfs:///answer.o";
  const uint64_t InputID = Link.addInput(std::move(Input)).ID;
  ObjectGraphImportOptions Options;
  Options.InputID = InputID;
  Options.ObjectGraph = {UINT64_C(7), UINT64_C(11)};
  auto Imported = importObjectGraph(Link, Source, Options);
  ASSERT_TRUE(static_cast<bool>(Imported)) << errorText(Imported.takeError());
  ASSERT_FALSE(verifyPluginLinkGraph(Link));

  const PluginLinkSection *Section =
      Link.findSection(Imported->Sections.at(ObjectSectionID));
  ASSERT_NE(Section, nullptr);
  EXPECT_EQ(Section->Kind, NEVERC_OBJECT_SECTION_KIND_TEXT);
  EXPECT_EQ(Section->Flags, NEVERC_OBJECT_SECTION_ALLOCATED |
                                NEVERC_OBJECT_SECTION_EXECUTABLE |
                                NEVERC_OBJECT_SECTION_RETAIN);
  EXPECT_EQ(Section->ComdatID, Imported->Comdats.at(ObjectComdatID));
  ASSERT_EQ(Section->Extensions.values().size(), 1u);
  EXPECT_EQ(Section->Extensions.values()[0].Payload,
            (std::vector<uint8_t>{1, 2, 3}));

  const PluginLinkAtom *Atom =
      Link.findAtom(Imported->Atoms.at(ObjectSectionID));
  ASSERT_NE(Atom, nullptr);
  EXPECT_EQ(Atom->Content.size(), 8u);
  EXPECT_NE(Atom->Flags & NEVERC_LINK_ATOM_ROOT, 0u);
  EXPECT_EQ(Atom->Origin.InputID, InputID);
  EXPECT_EQ(Atom->Origin.ObjectEntityID, ObjectSectionID);
  EXPECT_EQ(Atom->Origin.ObjectGraph.Owner, UINT64_C(7));

  const PluginLinkSymbol *DefinedLink =
      Link.findSymbol(Imported->Symbols.at(DefinedID));
  ASSERT_NE(DefinedLink, nullptr);
  EXPECT_EQ(DefinedLink->AtomID, Atom->ID);
  EXPECT_EQ(DefinedLink->Type, NEVERC_OBJECT_SYMBOL_TYPE_FUNCTION);
  EXPECT_TRUE(DefinedLink->IsExported);
  const PluginLinkSymbol *UndefinedLink =
      Link.findSymbol(Imported->Symbols.at(UndefinedID));
  ASSERT_NE(UndefinedLink, nullptr);
  EXPECT_TRUE(UndefinedLink->IsImported);
  ASSERT_EQ(Link.imports().size(), 1u);
  ASSERT_EQ(Link.exports().size(), 1u);

  const PluginLinkEdge *Edge =
      Link.findEdge(Imported->Relocations.at(RelocationID));
  ASSERT_NE(Edge, nullptr);
  EXPECT_EQ(Edge->SourceAtomID, Atom->ID);
  EXPECT_EQ(Edge->TargetSymbolID, UndefinedLink->ID);
  EXPECT_EQ(Edge->RelocationKind, NEVERC_OBJECT_RELOCATION_PC_RELATIVE);
  EXPECT_TRUE(Edge->IsPCRelative);
  EXPECT_TRUE(Edge->IsSigned);
  EXPECT_EQ(Edge->Addend, -4);
}

TEST(PluginObjectGraphImportTest, RejectsForeignTargetWithoutMutation) {
  auto SourceTarget = makeTargetKey();
  auto OtherTarget = makeTargetKey({UINT64_C(0xdead), UINT64_C(0xbeef)});
  ASSERT_TRUE(static_cast<bool>(SourceTarget))
      << errorText(SourceTarget.takeError());
  ASSERT_TRUE(static_cast<bool>(OtherTarget))
      << errorText(OtherTarget.takeError());
  PluginObjectGraph Source(std::move(*SourceTarget));
  PluginLinkGraph Link(std::move(*OtherTarget));

  auto Imported = importObjectGraph(Link, Source);
  ASSERT_FALSE(Imported);
  EXPECT_NE(errorText(Imported.takeError()).find("does not match"),
            std::string::npos);
  EXPECT_TRUE(Link.sections().empty());
  EXPECT_TRUE(Link.symbols().empty());
}

TEST(PluginObjectMergeProviderTest,
     ExposesEveryInputAndCommitsHostOwnedOutput) {
  LinkTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());
  auto First = makeObject(1);
  auto Second = makeObject(2);
  ASSERT_NE(First, nullptr);
  ASSERT_NE(Second, nullptr);
  auto Target = makeTargetKey();
  ASSERT_TRUE(static_cast<bool>(Target)) << errorText(Target.takeError());

  MergeCallbackState State;
  PluginLinkSnapshot::ObjectMergeProviderRecord Provider;
  Provider.PluginID = "org.neverc.builtin.test";
  Provider.ProviderID = "merge";
  Provider.TargetID = TestTargetID;
  Provider.FormatID = TestFormatID;
  Provider.ProductID = TestProductID;
  Provider.Merge = mergeObjects;
  Provider.UserData = &State;
  Provider.Builtin = true;
  std::array<PluginObjectGraph *, 2> Inputs{First.get(), Second.get()};

  auto Merged = executeObjectMergeProvider(Scope.task(), Provider,
                                           std::move(*Target), Inputs);
  ASSERT_TRUE(static_cast<bool>(Merged)) << errorText(Merged.takeError());
  ASSERT_NE(Merged->Object, nullptr);
  ASSERT_FALSE(verifyPluginObjectGraph(*Merged->Object));
  ASSERT_EQ(Merged->Object->sections().size(), 1u);
  EXPECT_EQ(Merged->Object->sections().front().Name, ".merged");
  EXPECT_EQ(Merged->Object->sections().front().Data, (std::vector<uint8_t>{3}));
  EXPECT_EQ(Merged->ProducerRouteDigest[0], 0x42);
  EXPECT_EQ(State.Calls, 1u);
}

TEST(PluginObjectMergeProviderTest, RejectsForeignOutputHandle) {
  LinkTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());
  auto Input = makeObject(1);
  auto Target = makeTargetKey();
  ASSERT_NE(Input, nullptr);
  ASSERT_TRUE(static_cast<bool>(Target)) << errorText(Target.takeError());

  MergeCallbackState State;
  State.ReturnForeignObject = true;
  PluginLinkSnapshot::ObjectMergeProviderRecord Provider;
  Provider.PluginID = "org.neverc.builtin.test";
  Provider.ProviderID = "foreign-output";
  Provider.TargetID = TestTargetID;
  Provider.FormatID = TestFormatID;
  Provider.ProductID = TestProductID;
  Provider.Merge = mergeObjects;
  Provider.UserData = &State;
  Provider.Builtin = true;
  PluginObjectGraph *InputPointer = Input.get();

  auto Merged = executeObjectMergeProvider(
      Scope.task(), Provider, std::move(*Target),
      ArrayRef<PluginObjectGraph *>(&InputPointer, 1));
  ASSERT_FALSE(Merged);
  EXPECT_NE(errorText(Merged.takeError()).find("foreign output"),
            std::string::npos);
  EXPECT_EQ(State.Calls, 1u);
}

TEST(PluginObjectMergeProviderTest,
     DispatchesRegisteredPluginThroughPlannedRoute) {
  LinkTaskScope Scope;
  ASSERT_TRUE(Scope.initialize(NEVERC_TEST_OBJECT_MERGE_PLUGIN));
  auto First = makeObject(2);
  auto Second = makeObject(3);
  auto Target = makeTargetKey();
  ASSERT_NE(First, nullptr);
  ASSERT_NE(Second, nullptr);
  ASSERT_TRUE(static_cast<bool>(Target)) << errorText(Target.takeError());

  auto Snapshot = PluginLinkRegistry::freeze(Scope.session().plugins());
  ASSERT_TRUE(static_cast<bool>(Snapshot)) << errorText(Snapshot.takeError());
  ASSERT_EQ((*Snapshot)->objectMergeProviders().size(), 2u);

  LinkRouteRequest Request;
  Request.TargetID = TestTargetID;
  Request.InputFormat = TestFormatID;
  Request.OutputFormat = TestFormatID;
  Request.OutputKind = NEVERC_LINK_OUTPUT_RELOCATABLE;
  auto Route =
      LinkRoutePlanner::plan((*Snapshot)->linkerProviders(),
                             (*Snapshot)->objectMergeProviders(), Request);
  ASSERT_TRUE(static_cast<bool>(Route)) << errorText(Route.takeError());
  ASSERT_EQ(Route->kind(), PlannedLinkRoute::Kind::ObjectMerge);
  ASSERT_NE(Route->objectMergeProvider(), nullptr);

  std::array<PluginObjectGraph *, 2> Inputs{First.get(), Second.get()};
  auto Merged = executeObjectMergeProvider(
      Scope.task(), *Route->objectMergeProvider(), std::move(*Target), Inputs);
  ASSERT_TRUE(static_cast<bool>(Merged)) << errorText(Merged.takeError());
  ASSERT_EQ(Merged->Object->sections().size(), 1u);
  EXPECT_EQ(Merged->Object->sections().front().Name, ".plugin-merged");
  EXPECT_EQ(Merged->Object->sections().front().Data, (std::vector<uint8_t>{5}));
  EXPECT_EQ(Merged->ProducerRouteDigest[0], 0x63);
  EXPECT_EQ(Merged->PluginID, "org.neverc.test.object-merge");
}

TEST(PluginObjectMergeProviderTest,
     NativeAndroidReleaseAuditValidatesEveryVersionsContribution) {
  initializeBuiltinTargets();
  const BuiltinTargetRoute *AndroidRoute = nullptr;
  for (const BuiltinTargetRoute &Route : builtinTargetRoutes()) {
    const Triple Parsed(Triple::normalize(Route.CanonicalTriple));
    if (Route.SupportsObject &&
        Route.ObjectFormat == BuiltinObjectFormat::ELF &&
        Parsed.getArch() == Triple::aarch64 && Parsed.isAndroid()) {
      AndroidRoute = &Route;
      break;
    }
  }
  ASSERT_NE(AndroidRoute, nullptr);

  constexpr StringLiteral Assembly = R"(
    .text
    .globl release_input_entry
    .type release_input_entry, %function
release_input_entry:
    nop
    .size release_input_entry, .-release_input_entry

    .section __versions,"a",%progbits,unique,1
    .balign 8
    .space 64
    .section __versions,"a",%progbits,unique,2
    .balign 16
    .space 128

    .section .native_extra,"",%progbits
    .byte 0
)";
  auto Original = assembleBuiltinObject(*AndroidRoute, Assembly);
  ASSERT_TRUE(static_cast<bool>(Original)) << errorText(Original.takeError());
  auto Graph = makeBuiltinObject(*AndroidRoute, "release_graph_entry");
  ASSERT_NE(Graph, nullptr);
  auto Target = makeBuiltinTargetKey(*AndroidRoute);
  ASSERT_TRUE(static_cast<bool>(Target)) << errorText(Target.takeError());
  std::array<PluginObjectGraph *, 1> Objects{Graph.get()};

  const auto Verify = [&](ArrayRef<uint8_t> Image) {
    std::array<ArrayRef<uint8_t>, 1> Images{Image};
    return verifyAndroidKernelReleaseObjectMergeInputs(
        Objects, Images, Target->view(), "test native Android input audit");
  };
  auto Valid = Verify(*Original);
  ASSERT_TRUE(static_cast<bool>(Valid)) << errorText(Valid.takeError());
  EXPECT_EQ(Valid->abi().Machine, ELF::EM_AARCH64);
  EXPECT_FALSE(Valid->hasRetainedAnonymousSymbols());

  const auto ExpectSectionTamperRejected = [&](StringRef Name,
                                               unsigned Occurrence,
                                               auto Mutator,
                                               StringRef ExpectedReason) {
    std::vector<uint8_t> Bytes = *Original;
    Error Patch = patchELF64SectionHeader(Bytes, Name, Occurrence, Mutator);
    ASSERT_FALSE(Patch) << errorText(std::move(Patch));
    auto Result = Verify(Bytes);
    ASSERT_FALSE(Result);
    const std::string Message = errorText(Result.takeError());
    EXPECT_NE(Message.find(ExpectedReason.str()), std::string::npos) << Message;
  };

  for (unsigned Occurrence : {0U, 1U}) {
    ExpectSectionTamperRejected(
        "__versions", Occurrence,
        [](object::ELF64LE::Shdr &Section) { Section.sh_type = ELF::SHT_NOTE; },
        "allocated, uncompressed SHT_PROGBITS");
    ExpectSectionTamperRejected(
        "__versions", Occurrence,
        [](object::ELF64LE::Shdr &Section) {
          Section.sh_flags &= ~ELF::SHF_ALLOC;
        },
        "allocated, uncompressed SHT_PROGBITS");
    ExpectSectionTamperRejected(
        "__versions", Occurrence,
        [](object::ELF64LE::Shdr &Section) {
          Section.sh_flags |= ELF::SHF_COMPRESSED;
        },
        "allocated, uncompressed SHT_PROGBITS");
    ExpectSectionTamperRejected(
        "__versions", Occurrence,
        [](object::ELF64LE::Shdr &Section) { Section.sh_addralign = 4; },
        "power of two >= 8");
    ExpectSectionTamperRejected(
        "__versions", Occurrence,
        [](object::ELF64LE::Shdr &Section) { Section.sh_size = 65; },
        "multiple of 64 bytes");
  }

  ExpectSectionTamperRejected(
      ".native_extra", 0,
      [](object::ELF64LE::Shdr &Section) { Section.sh_type = ELF::SHT_STRTAB; },
      "additional SHT_STRTAB");
  ExpectSectionTamperRejected(
      ".native_extra", 0,
      [](object::ELF64LE::Shdr &Section) { Section.sh_type = ELF::SHT_DYNSYM; },
      "SHT_DYNSYM");
  ExpectSectionTamperRejected(
      ".native_extra", 0,
      [](object::ELF64LE::Shdr &Section) { Section.sh_type = ELF::SHT_REL; },
      "SHT_REL");
  ExpectSectionTamperRejected(
      ".text", 0, [](object::ELF64LE::Shdr &Section) { Section.sh_addr = 1; },
      "nonzero sh_addr");
  ExpectSectionTamperRejected(
      ".native_extra", 0,
      [](object::ELF64LE::Shdr &Section) { Section.sh_addralign = 3; },
      "non-power-of-two alignment");
}

TEST(PluginObjectMergeProviderTest,
     DirectBuiltinAndroidFinalizerAuditsInputAcrossStripModes) {
  initializeBuiltinTargets();
  LinkTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());
  const BuiltinTargetRoute *AndroidRoute = findAndroidAArch64ObjectRoute();
  ASSERT_NE(AndroidRoute, nullptr);

  auto Input = assembleValidAndroidReleaseInput(*AndroidRoute);
  ASSERT_TRUE(static_cast<bool>(Input)) << errorText(Input.takeError());
  Error Patch = patchELF64SectionHeader(
      *Input, "__versions", 0,
      [](object::ELF64LE::Shdr &Section) { Section.sh_addralign = 4; });
  ASSERT_FALSE(Patch) << errorText(std::move(Patch));

  auto Snapshot = PluginTargetRegistry::freeze(
      ArrayRef<PluginTargetRegistrationView>(), PluginTargetRequest{});
  ASSERT_TRUE(static_cast<bool>(Snapshot)) << errorText(Snapshot.takeError());
  auto Reader = ObjectReaderProvider::create(*Snapshot);
  ASSERT_TRUE(static_cast<bool>(Reader)) << errorText(Reader.takeError());
  auto ReadTarget = makeBuiltinTargetKey(*AndroidRoute);
  ASSERT_TRUE(static_cast<bool>(ReadTarget))
      << errorText(ReadTarget.takeError());
  auto Graph = (*Reader)->read(Scope.task(), *Input,
                               "memory://invalid-final-native-input.o",
                               *ReadTarget, AndroidRoute->ObjectFormatID);
  ASSERT_TRUE(static_cast<bool>(Graph)) << errorText(Graph.takeError());

  std::array<PluginObjectGraph *, 1> Objects{Graph->get()};
  std::array<ArrayRef<uint8_t>, 1> Images{*Input};
  struct StripCase {
    const char *Name;
    bool DropDebugInfo;
    bool StripUnneededSymbols;
  };
  constexpr std::array<StripCase, 3> Cases{{
      {"none", false, false},
      {"debug-info", true, false},
      {"all", true, true},
  }};
  for (const StripCase &Case : Cases) {
    SCOPED_TRACE(Case.Name);
    auto MergeTarget = makeBuiltinTargetKey(*AndroidRoute);
    ASSERT_TRUE(static_cast<bool>(MergeTarget))
        << errorText(MergeTarget.takeError());
    BuiltinObjectMergeConfig Config;
    Config.AndroidKernelModule = true;
    Config.FinalizeAndroidKernelModule = true;
    Config.DropDebugInfo = Case.DropDebugInfo;
    Config.StripUnneededSymbols = Case.StripUnneededSymbols;
    auto Merged = executeBuiltinObjectMergeAdapter(
        Scope.task(), *Snapshot, std::move(*MergeTarget), Objects, Images,
        NEVERC_LINK_OPTION_NONE, Config);
    ASSERT_FALSE(Merged);
    const std::string Message = errorText(Merged.takeError());
    EXPECT_NE(Message.find("power of two >= 8"), std::string::npos) << Message;
  }
}

TEST(
    PluginObjectMergeProviderTest,
    NativeOnlyAndroidFinalizerRejectsUntrustedHooksBeforeSinkAcrossStripModes) {
  initializeBuiltinTargets();
  const BuiltinTargetRoute *AndroidRoute = findAndroidAArch64ObjectRoute();
  ASSERT_NE(AndroidRoute, nullptr);

  struct HookCase {
    const char *Name;
    const char *PluginPath;
    const char *ExpectedReason;
  };
  const std::array<HookCase, 2> Hooks{{
      {"third-party-provider", NEVERC_TEST_OBJECT_MERGE_PLUGIN,
       "third-party ObjectMergeProvider cannot preserve"},
      {"replaceable-output-phase", NEVERC_TEST_OBJECT_POST_WRITE_PLUGIN,
       "incompatible with registered ObjectGraph/output phase bindings"},
  }};
  struct StripCase {
    const char *Name;
    linker::StripMode Mode;
  };
  constexpr std::array<StripCase, 3> Modes{{
      {"none", linker::StripMode::None},
      {"debug-info", linker::StripMode::DebugInfo},
      {"all", linker::StripMode::All},
  }};

  for (const HookCase &Hook : Hooks) {
    for (const StripCase &Strip : Modes) {
      SCOPED_TRACE(Hook.Name);
      SCOPED_TRACE(Strip.Name);
      LinkTaskScope Scope;
      ASSERT_TRUE(Scope.initialize(Hook.PluginPath));

      auto Input = assembleValidAndroidReleaseInput(*AndroidRoute);
      ASSERT_TRUE(static_cast<bool>(Input)) << errorText(Input.takeError());
      Error Patch = patchELF64Header(*Input, [](object::ELF64LE::Ehdr &Header) {
        Header.e_ident[ELF::EI_OSABI] = ELF::ELFOSABI_GNU;
      });
      ASSERT_FALSE(Patch) << errorText(std::move(Patch));

      SmallString<128> Directory;
      ASSERT_FALSE(sys::fs::createUniqueDirectory(
          "neverc-untrusted-finalize-native-input", Directory));
      auto RemoveDirectory = make_scope_exit(
          [&] { (void)sys::fs::remove_directories(Directory); });
      SmallString<160> OutputPath(Directory);
      sys::path::append(OutputPath, "must-not-open.ko");

      linker::LinkExecutionRequest Request;
      Request.TargetTriple = AndroidRoute->CanonicalTriple.str();
      Request.OutputKind = linker::LinkExecutionOutputKind::Relocatable;
      Request.OutputURI = OutputPath.str().str();
      linker::LinkExecutionInput LinkInput;
      LinkInput.Kind = linker::LinkExecutionInputKind::Object;
      LinkInput.LogicalURI = "memory://native-only-final-input.o";
      LinkInput.AuthorizedBlob = std::move(*Input);
      Request.Inputs.push_back(std::move(LinkInput));

      linker::LinkerDriverConfig Config;
      Config.pluginTask = &Scope.task();
      Config.relocatable = true;
      Config.androidKernelModule = true;
      Config.finalizeAndroidKernelModule = true;
      Config.stripMode = Strip.Mode;

      neverc::OutputCoordinator Outputs;
      auto SessionAlias = std::shared_ptr<PluginSession>(
          &Scope.session(), [](PluginSession *) {});
      LinkExecutionHooksBridge Bridge(std::move(SessionAlias), Outputs);
      raw_null_ostream NullOutput;
      auto Result = Bridge.execute(Request, Config, NullOutput, NullOutput);
      ASSERT_FALSE(Result);
      const std::string Message = errorText(Result.takeError());
      EXPECT_NE(Message.find(Hook.ExpectedReason), std::string::npos)
          << Message;
      EXPECT_FALSE(sys::fs::exists(OutputPath));
    }
  }
}

TEST(PluginObjectMergeProviderTest,
     OrdinaryAndroidReleaseAllowsAuthorizedPostWriteMutation) {
  initializeBuiltinTargets();
  const BuiltinTargetRoute *AndroidRoute = findAndroidAArch64ObjectRoute();
  ASSERT_NE(AndroidRoute, nullptr);

  auto Input = assembleValidAndroidReleaseInput(*AndroidRoute);
  ASSERT_TRUE(static_cast<bool>(Input)) << errorText(Input.takeError());
  const std::vector<uint8_t> InputBytes = std::move(*Input);

  const auto Run =
      [&](StringRef PluginPath,
          StringRef OutputStem) -> std::optional<std::vector<uint8_t>> {
    LinkTaskScope Scope;
    if (!Scope.initialize(PluginPath))
      return std::nullopt;

    SmallString<128> Directory;
    if (std::error_code EC =
            sys::fs::createUniqueDirectory(OutputStem, Directory)) {
      ADD_FAILURE() << EC.message();
      return std::nullopt;
    }
    auto RemoveDirectory =
        make_scope_exit([&] { (void)sys::fs::remove_directories(Directory); });
    SmallString<160> OutputPath(Directory);
    sys::path::append(OutputPath, "post-write-release.ko");

    linker::LinkExecutionRequest Request;
    Request.TargetTriple = AndroidRoute->CanonicalTriple.str();
    Request.OutputKind = linker::LinkExecutionOutputKind::Relocatable;
    Request.OutputURI = OutputPath.str().str();
    linker::LinkExecutionInput LinkInput;
    LinkInput.Kind = linker::LinkExecutionInputKind::Object;
    LinkInput.LogicalURI = "memory://ordinary-post-write-release-input.o";
    LinkInput.AuthorizedBlob = InputBytes;
    Request.Inputs.push_back(std::move(LinkInput));

    linker::LinkerDriverConfig Config;
    Config.pluginTask = &Scope.task();
    Config.relocatable = true;
    Config.androidKernelModule = true;
    Config.finalizeAndroidKernelModule = true;
    Config.stripMode = linker::StripMode::All;

    neverc::OutputCoordinator Outputs;
    auto SessionAlias = std::shared_ptr<PluginSession>(&Scope.session(),
                                                       [](PluginSession *) {});
    LinkExecutionHooksBridge Bridge(std::move(SessionAlias), Outputs);
    raw_null_ostream NullOutput;
    auto Result = Bridge.execute(Request, Config, NullOutput, NullOutput);
    if (!Result) {
      ADD_FAILURE() << errorText(Result.takeError());
      return std::nullopt;
    }
    if (Result->Disposition != linker::LinkHookDisposition::Completed) {
      ADD_FAILURE() << "ordinary Android release link did not complete";
      Bridge.complete(false);
      return std::nullopt;
    }
    Bridge.complete(true);

    auto Output = MemoryBuffer::getFile(OutputPath);
    if (!Output) {
      ADD_FAILURE() << Output.getError().message();
      return std::nullopt;
    }
    StringRef Bytes = (*Output)->getBuffer();
    return std::vector<uint8_t>(Bytes.bytes_begin(), Bytes.bytes_end());
  };

  auto Baseline = Run({}, "neverc-ordinary-release-baseline");
  ASSERT_TRUE(Baseline.has_value());
  auto Mutated = Run(NEVERC_TEST_OBJECT_TEXT_PAYLOAD_POST_WRITE_PLUGIN,
                     "neverc-ordinary-release-post-write");
  ASSERT_TRUE(Mutated.has_value());
  ASSERT_EQ(Mutated->size(), Baseline->size());

  auto TextRange = findELF64SectionFileRange(*Baseline, ".text");
  ASSERT_TRUE(static_cast<bool>(TextRange)) << errorText(TextRange.takeError());
  const uint64_t TextOffset = TextRange->first;
  const uint64_t TextSize = TextRange->second;

  size_t Differences = 0;
  size_t FirstDifference = 0;
  for (size_t Index = 0; Index != Baseline->size(); ++Index) {
    if ((*Baseline)[Index] == (*Mutated)[Index])
      continue;
    if (Differences == 0)
      FirstDifference = Index;
    ++Differences;
  }
  EXPECT_EQ(Differences, 1U);
  EXPECT_GE(FirstDifference, TextOffset);
  EXPECT_LT(FirstDifference, TextOffset + TextSize);
  EXPECT_EQ((*Mutated)[FirstDifference],
            static_cast<uint8_t>((*Baseline)[FirstDifference] ^ UINT8_C(1)));
}

TEST(PluginObjectMergeProviderTest,
     OrdinaryAndroidReleaseRejectsAuthorizedExactNameReplacement) {
  initializeBuiltinTargets();
  const BuiltinTargetRoute *AndroidRoute = findAndroidAArch64ObjectRoute();
  ASSERT_NE(AndroidRoute, nullptr);
  LinkTaskScope Scope;
  ASSERT_TRUE(Scope.initialize(NEVERC_TEST_OBJECT_EXACT_NAME_CORRUPT_PLUGIN));
  auto Input = assembleValidAndroidReleaseInput(*AndroidRoute);
  ASSERT_TRUE(static_cast<bool>(Input)) << errorText(Input.takeError());
  AndroidObjectLinkOutcome Outcome =
      runAndroidObjectLink(Scope, *AndroidRoute, std::move(*Input),
                           "neverc-ordinary-release-exact-name-corrupt", true);
  EXPECT_FALSE(Outcome.Completed);
  EXPECT_FALSE(Outcome.Published);
  EXPECT_NE(Outcome.Error.find("immutable release identity seal"),
            std::string::npos)
      << Outcome.Error;
}

TEST(PluginObjectMergeProviderTest,
     OrdinaryAndroidReleaseRejectsProtectedSectionNameReplacement) {
  initializeBuiltinTargets();
  const BuiltinTargetRoute *AndroidRoute = findAndroidAArch64ObjectRoute();
  ASSERT_NE(AndroidRoute, nullptr);

  LinkTaskScope Scope;
  ASSERT_TRUE(Scope.initialize(
      NEVERC_TEST_OBJECT_PROTECTED_SECTION_NAME_CORRUPT_PLUGIN));
  auto Input =
      assembleAndroidReleaseInputWithProtectedSectionSymbol(*AndroidRoute);
  ASSERT_TRUE(static_cast<bool>(Input)) << errorText(Input.takeError());
  AndroidObjectLinkOutcome Outcome = runAndroidObjectLink(
      Scope, *AndroidRoute, std::move(*Input),
      "neverc-ordinary-release-protected-symbol-corrupt", true);
  EXPECT_FALSE(Outcome.Completed);
  EXPECT_FALSE(Outcome.Published);
  EXPECT_NE(Outcome.Error.find("immutable release identity seal"),
            std::string::npos)
      << Outcome.Error;
}

TEST(PluginObjectMergeProviderTest,
     OrdinaryAndroidReleaseRejectsRawSectionNameReplacement) {
  initializeBuiltinTargets();
  const BuiltinTargetRoute *AndroidRoute = findAndroidAArch64ObjectRoute();
  ASSERT_NE(AndroidRoute, nullptr);
  LinkTaskScope Scope;
  ASSERT_TRUE(Scope.initialize(NEVERC_TEST_OBJECT_SECTION_NAME_CORRUPT_PLUGIN));
  auto Input = assembleAndroidReleaseInputWithInitPLT(*AndroidRoute);
  ASSERT_TRUE(static_cast<bool>(Input)) << errorText(Input.takeError());
  AndroidObjectLinkOutcome Outcome = runAndroidObjectLink(
      Scope, *AndroidRoute, std::move(*Input),
      "neverc-ordinary-release-section-name-corrupt", true);
  EXPECT_FALSE(Outcome.Completed);
  EXPECT_FALSE(Outcome.Published);
  EXPECT_NE(Outcome.Error.find("release layout identity seal"),
            std::string::npos)
      << Outcome.Error;
  EXPECT_NE(Outcome.Error.find(".init.plt"), std::string::npos)
      << Outcome.Error;
  EXPECT_NE(Outcome.Error.find(".hide.plt"), std::string::npos)
      << Outcome.Error;
}

TEST(PluginObjectMergeProviderTest,
     OrdinaryAndroidReleaseRejectsSectionTargetNameReplacement) {
  initializeBuiltinTargets();
  const BuiltinTargetRoute *AndroidRoute = findAndroidAArch64ObjectRoute();
  ASSERT_NE(AndroidRoute, nullptr);
  LinkTaskScope Scope;
  ASSERT_TRUE(
      Scope.initialize(NEVERC_TEST_OBJECT_SECTION_SYMBOL_CORRUPT_PLUGIN));
  auto Input = assembleAndroidReleaseInputWithNamedSection(*AndroidRoute);
  ASSERT_TRUE(static_cast<bool>(Input)) << errorText(Input.takeError());
  AndroidObjectLinkOutcome Outcome = runAndroidObjectLink(
      Scope, *AndroidRoute, std::move(*Input),
      "neverc-ordinary-release-section-symbol-corrupt", true);
  EXPECT_FALSE(Outcome.Completed);
  EXPECT_FALSE(Outcome.Published);
  EXPECT_NE(Outcome.Error.find("immutable release identity seal"),
            std::string::npos)
      << Outcome.Error;
}

TEST(PluginObjectMergeProviderTest,
     FinalizedAndroidReleaseRejectsReplaceableWritePhaseBeforeOpeningSink) {
  initializeBuiltinTargets();
  const BuiltinTargetRoute *AndroidRoute = findAndroidAArch64ObjectRoute();
  ASSERT_NE(AndroidRoute, nullptr);
  for (const auto &[PluginPath, Stem] :
       std::array<std::pair<StringRef, StringRef>, 2>{
           std::pair<StringRef, StringRef>{
               NEVERC_TEST_OBJECT_WRITE_INTERCEPTOR_PLUGIN,
               "neverc-release-write-interceptor"},
           std::pair<StringRef, StringRef>{
               NEVERC_TEST_OBJECT_WRITE_PROVIDER_PLUGIN,
               "neverc-release-write-provider"}}) {
    SCOPED_TRACE(Stem.str());
    LinkTaskScope Scope;
    ASSERT_TRUE(Scope.initialize(PluginPath));
    auto Input = assembleValidAndroidReleaseInput(*AndroidRoute);
    ASSERT_TRUE(static_cast<bool>(Input)) << errorText(Input.takeError());
    AndroidObjectLinkOutcome Outcome = runAndroidObjectLink(
        Scope, *AndroidRoute, std::move(*Input), Stem, true);
    EXPECT_FALSE(Outcome.Completed);
    EXPECT_FALSE(Outcome.Published);
    EXPECT_NE(Outcome.Error.find("replaceable object write phase"),
              std::string::npos)
        << Outcome.Error;
    EXPECT_NE(Outcome.Error.find("before the trusted image baseline"),
              std::string::npos)
        << Outcome.Error;
  }
}

TEST(PluginObjectMergeProviderTest,
     FinalizedAndroidReleaseIgnoresMismatchedWriteProviderRoute) {
  initializeBuiltinTargets();
  const BuiltinTargetRoute *AndroidRoute = findAndroidAArch64ObjectRoute();
  ASSERT_NE(AndroidRoute, nullptr);

  for (const bool NativeOnly : {false, true}) {
    SCOPED_TRACE(NativeOnly ? "native-only" : "ordinary");
    LinkTaskScope Scope;
    ASSERT_TRUE(Scope.initialize(
        NEVERC_TEST_OBJECT_WRITE_MISMATCHED_ROUTE_PROVIDER_PLUGIN));
    auto Input = assembleValidAndroidReleaseInput(*AndroidRoute);
    ASSERT_TRUE(static_cast<bool>(Input)) << errorText(Input.takeError());
    if (NativeOnly) {
      Error HeaderPatch =
          patchELF64Header(*Input, [](object::ELF64LE::Ehdr &Header) {
            Header.e_flags = UINT32_C(0x6a31);
            Header.e_ident[ELF::EI_OSABI] = ELF::ELFOSABI_GNU;
          });
      ASSERT_FALSE(HeaderPatch) << errorText(std::move(HeaderPatch));
      Error AnonymousPatch = patchELF64SectionHeader(
          *Input, ".native_extra", 0,
          [](object::ELF64LE::Shdr &Section) { Section.sh_name = 0; });
      ASSERT_FALSE(AnonymousPatch) << errorText(std::move(AnonymousPatch));
    }

    AndroidObjectLinkOutcome Outcome = runAndroidObjectLink(
        Scope, *AndroidRoute, std::move(*Input),
        NativeOnly ? "neverc-native-only-mismatched-write-provider"
                   : "neverc-release-mismatched-write-provider",
        true);
    EXPECT_TRUE(Outcome.Completed) << Outcome.Error;
    EXPECT_TRUE(Outcome.Published);
    EXPECT_FALSE(Outcome.Output.empty());
  }
}

TEST(PluginObjectMergeProviderTest,
     FinalizedAndroidReleaseRejectsFrozenOutputFormatConfusionBeforeProvider) {
  initializeBuiltinTargets();
  const BuiltinTargetRoute *AndroidRoute = findAndroidAArch64ObjectRoute();
  ASSERT_NE(AndroidRoute, nullptr);
  LinkTaskScope Scope;
  ASSERT_TRUE(
      Scope.initialize(NEVERC_TEST_OBJECT_WRITE_ELF_ROUTE_PROVIDER_PLUGIN));
  auto Input = assembleValidAndroidReleaseInput(*AndroidRoute);
  ASSERT_TRUE(static_cast<bool>(Input)) << errorText(Input.takeError());
  constexpr NevercObjectFormatID FakeOutputFormat{UINT64_C(0x4e43524f55544542),
                                                  UINT64_C(0xdec0de)};

  AndroidObjectLinkOutcome Outcome = runAndroidObjectLink(
      Scope, *AndroidRoute, std::move(*Input),
      "neverc-release-output-format-confusion", true, FakeOutputFormat);
  EXPECT_FALSE(Outcome.Completed);
  EXPECT_FALSE(Outcome.Published);
  EXPECT_NE(Outcome.Error.find("input, target, and output object formats"),
            std::string::npos)
      << Outcome.Error;
  EXPECT_EQ(Outcome.Error.find("Provider callback"), std::string::npos)
      << Outcome.Error;
}

TEST(PluginObjectMergeProviderTest,
     FinalizedAndroidReleaseRejectsNonRelocatableRequestBeforeRouting) {
  initializeBuiltinTargets();
  const BuiltinTargetRoute *AndroidRoute = findAndroidAArch64ObjectRoute();
  ASSERT_NE(AndroidRoute, nullptr);
  LinkTaskScope Scope;
  ASSERT_TRUE(
      Scope.initialize(NEVERC_TEST_OBJECT_WRITE_ELF_ROUTE_PROVIDER_PLUGIN));
  struct InvalidRelocatableState {
    const char *Name;
    linker::LinkExecutionOutputKind RequestKind;
    bool ConfigRelocatable;
  };
  constexpr std::array<InvalidRelocatableState, 3> Cases{{
      {"request-only", linker::LinkExecutionOutputKind::Executable, true},
      {"config-only", linker::LinkExecutionOutputKind::Relocatable, false},
      {"request-and-config", linker::LinkExecutionOutputKind::Executable,
       false},
  }};

  for (const InvalidRelocatableState &Case : Cases) {
    SCOPED_TRACE(Case.Name);
    auto Input = assembleValidAndroidReleaseInput(*AndroidRoute);
    ASSERT_TRUE(static_cast<bool>(Input)) << errorText(Input.takeError());

    AndroidObjectLinkOutcome Outcome = runAndroidObjectLink(
        Scope, *AndroidRoute, std::move(*Input),
        (Twine("neverc-release-nonrelocatable-") + Case.Name).str(), true,
        std::nullopt, Case.RequestKind, Case.ConfigRelocatable);
    EXPECT_FALSE(Outcome.Completed);
    EXPECT_FALSE(Outcome.Published);
    EXPECT_NE(Outcome.Error.find("requires a relocatable output request"),
              std::string::npos)
        << Outcome.Error;
    EXPECT_EQ(Outcome.Error.find("Provider callback"), std::string::npos)
        << Outcome.Error;
  }
}

TEST(PluginObjectMergeProviderTest,
     NonReleaseWriteBindingsRetainTheirExistingExecutionSemantics) {
  initializeBuiltinTargets();
  const BuiltinTargetRoute *AndroidRoute = findAndroidAArch64ObjectRoute();
  ASSERT_NE(AndroidRoute, nullptr);

  LinkTaskScope InterceptorScope;
  ASSERT_TRUE(
      InterceptorScope.initialize(NEVERC_TEST_OBJECT_WRITE_INTERCEPTOR_PLUGIN));
  auto InterceptorInput = assembleValidAndroidReleaseInput(*AndroidRoute);
  ASSERT_TRUE(static_cast<bool>(InterceptorInput))
      << errorText(InterceptorInput.takeError());
  AndroidObjectLinkOutcome Interceptor = runAndroidObjectLink(
      InterceptorScope, *AndroidRoute, std::move(*InterceptorInput),
      "neverc-nonrelease-write-interceptor", false);
  EXPECT_TRUE(Interceptor.Completed) << Interceptor.Error;
  EXPECT_TRUE(Interceptor.Published);
  EXPECT_FALSE(Interceptor.Output.empty());

  LinkTaskScope ProviderScope;
  ASSERT_TRUE(
      ProviderScope.initialize(NEVERC_TEST_OBJECT_WRITE_PROVIDER_PLUGIN));
  auto ProviderInput = assembleValidAndroidReleaseInput(*AndroidRoute);
  ASSERT_TRUE(static_cast<bool>(ProviderInput))
      << errorText(ProviderInput.takeError());
  AndroidObjectLinkOutcome Provider = runAndroidObjectLink(
      ProviderScope, *AndroidRoute, std::move(*ProviderInput),
      "neverc-nonrelease-write-provider", false);
  EXPECT_FALSE(Provider.Completed);
  EXPECT_FALSE(Provider.Published);
  EXPECT_EQ(Provider.Error.find("finalized Android release"), std::string::npos)
      << Provider.Error;
  EXPECT_NE(Provider.Error.find("Provider"), std::string::npos)
      << Provider.Error;
}

TEST(PluginObjectMergeProviderTest,
     FinalizedThirdPartyMergeUsesItsGraphAndTheHostReleaseWriter) {
  initializeBuiltinTargets();
  const BuiltinTargetRoute *AndroidRoute = findAndroidAArch64ObjectRoute();
  ASSERT_NE(AndroidRoute, nullptr);
  LinkTaskScope Scope;
  ASSERT_TRUE(Scope.initialize(NEVERC_TEST_OBJECT_MERGE_PLUGIN));
  auto Input = assembleValidAndroidReleaseInput(*AndroidRoute);
  ASSERT_TRUE(static_cast<bool>(Input)) << errorText(Input.takeError());

  AndroidObjectLinkOutcome Outcome =
      runAndroidObjectLink(Scope, *AndroidRoute, std::move(*Input),
                           "neverc-third-party-release-host-writer", true);
  ASSERT_TRUE(Outcome.Completed) << Outcome.Error;
  ASSERT_TRUE(Outcome.Published);
  auto Semantics = readELFSemantics(Outcome.Output);
  ASSERT_TRUE(static_cast<bool>(Semantics)) << errorText(Semantics.takeError());
  EXPECT_NE(Semantics->SectionNames.find(".plugin-merged"),
            Semantics->SectionNames.end());
  EXPECT_TRUE(Semantics->HasSymbolStringTable);
  EXPECT_TRUE(Semantics->HasSectionStringTable);
  EXPECT_TRUE(Semantics->SymtabLinksSymbolStringTable);

  AndroidKernelModuleFinalizationPolicy Policy;
  Policy.DropDebugInfo = true;
  Policy.StripUnneededSymbols = true;
  EXPECT_FALSE(verifyFinalAndroidKernelModuleImage(
      Outcome.Output, Policy,
      "test third-party graph serialized by host release writer"));
}

TEST(PluginObjectMergeProviderTest,
     AndroidReleaseSymbolCountRejectsOnlyValuesAboveELF64IndexRange) {
  EXPECT_FALSE(verifyAndroidKernelReleaseSymbolCount(
      std::numeric_limits<uint32_t>::max(), "test maximum symbol count"));
  Error TooLarge = verifyAndroidKernelReleaseSymbolCount(
      uint64_t{std::numeric_limits<uint32_t>::max()} + 1,
      "test excessive symbol count");
  ASSERT_TRUE(static_cast<bool>(TooLarge));
  const std::string Message = errorText(std::move(TooLarge));
  EXPECT_NE(Message.find("exceeds the ELF64 relocation-index range"),
            std::string::npos)
      << Message;
}

TEST(PluginObjectMergeProviderTest,
     ReleasePipelineSelectsOneAuthorityAndSymbolMapProducer) {
  initializeBuiltinTargets();
  LinkTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());
  const BuiltinTargetRoute *AndroidRoute = findAndroidAArch64ObjectRoute();
  ASSERT_NE(AndroidRoute, nullptr);

  auto First = assembleValidAndroidReleaseInput(*AndroidRoute);
  ASSERT_TRUE(static_cast<bool>(First)) << errorText(First.takeError());
  constexpr StringLiteral SecondAssembly = R"(
    .text
    .globl secondary_release_entry
    .type secondary_release_entry, %function
secondary_release_entry:
    nop
    .size secondary_release_entry, .-secondary_release_entry

    .section .neverc.android.kernel.profile,"a",%progbits
    .balign 8
    .globl __neverc_android_kernel_profile_contract
    .type __neverc_android_kernel_profile_contract, %object
__neverc_android_kernel_profile_contract:
    .byte 2, 0, 0, 0, 100, 2, 0, 0
    .size __neverc_android_kernel_profile_contract, 8
)";
  auto Second = assembleBuiltinObject(*AndroidRoute, SecondAssembly);
  ASSERT_TRUE(static_cast<bool>(Second)) << errorText(Second.takeError());

  auto Snapshot = PluginTargetRegistry::freeze(
      ArrayRef<PluginTargetRegistrationView>(), PluginTargetRequest{});
  ASSERT_TRUE(static_cast<bool>(Snapshot)) << errorText(Snapshot.takeError());
  auto Reader = ObjectReaderProvider::create(*Snapshot);
  ASSERT_TRUE(static_cast<bool>(Reader)) << errorText(Reader.takeError());
  auto ReadTarget = makeBuiltinTargetKey(*AndroidRoute);
  ASSERT_TRUE(static_cast<bool>(ReadTarget))
      << errorText(ReadTarget.takeError());
  auto FirstGraph =
      (*Reader)->read(Scope.task(), *First, "memory://bound-first.o",
                      *ReadTarget, AndroidRoute->ObjectFormatID);
  auto SecondGraph =
      (*Reader)->read(Scope.task(), *Second, "memory://bound-second.o",
                      *ReadTarget, AndroidRoute->ObjectFormatID);
  ASSERT_TRUE(static_cast<bool>(FirstGraph))
      << errorText(FirstGraph.takeError());
  ASSERT_TRUE(static_cast<bool>(SecondGraph))
      << errorText(SecondGraph.takeError());

  std::array<PluginObjectGraph *, 2> Objects{FirstGraph->get(),
                                             SecondGraph->get()};
  std::array<ArrayRef<uint8_t>, 2> Images{*First, *Second};
  auto InputContract = verifyAndroidKernelReleaseObjectMergeInputs(
      Objects, Images, ReadTarget->view(),
      "test direct multi-input release contract");
  ASSERT_TRUE(static_cast<bool>(InputContract))
      << errorText(InputContract.takeError());
  PluginObjectGraph GraphRouteObject(**FirstGraph);

  auto MergeTarget = makeBuiltinTargetKey(*AndroidRoute);
  ASSERT_TRUE(static_cast<bool>(MergeTarget))
      << errorText(MergeTarget.takeError());
  BuiltinObjectMergeConfig Config;
  Config.AndroidKernelModule = true;
  Config.FinalizeAndroidKernelModule = true;
  Config.DropDebugInfo = true;
  Config.StripUnneededSymbols = true;
  auto Merged = executeBuiltinObjectMergeAdapter(
      Scope.task(), *Snapshot, std::move(*MergeTarget), Objects, Images,
      NEVERC_LINK_OPTION_NONE, Config);
  ASSERT_TRUE(static_cast<bool>(Merged)) << errorText(Merged.takeError());
  ASSERT_FALSE(Merged->MergedImage.empty());

  const auto &ProducedToken = Merged->boundAndroidKernelReleaseOutput();
  ASSERT_NE(ProducedToken, nullptr);
  const ArrayRef<uint8_t> CompleteMergedImage(
      reinterpret_cast<const uint8_t *>(Merged->MergedImage.data()),
      Merged->MergedImage.size());
  EXPECT_EQ(ProducedToken->nativeOutputDigest(),
            SHA256::hash(CompleteMergedImage));
  EXPECT_EQ(ProducedToken->nativeOutputDigest(), Merged->ProducerRouteDigest);
  auto ConsumedToken = consumeAndroidKernelReleaseBoundOutput(
      *Merged, *InputContract, "test Bridge bound-output consumption");
  ASSERT_TRUE(static_cast<bool>(ConsumedToken))
      << errorText(ConsumedToken.takeError());
  EXPECT_EQ(ConsumedToken->get(), ProducedToken.get());
  EXPECT_FALSE(ConsumedToken->owner_before(ProducedToken));
  EXPECT_FALSE(ProducedToken.owner_before(*ConsumedToken));

  AndroidKernelModuleFinalizationPolicy NativePolicy;
  NativePolicy.DropDebugInfo = true;
  NativePolicy.StripUnneededSymbols = true;
  NativePolicy.SymbolNameState = AndroidKernelSymbolNameState::CanonicalRelease;
  auto NativeRelease = finalizeAndroidKernelReleasePipeline(
      *Merged, /*ProviderBuiltin=*/true, *InputContract, NativePolicy);
  ASSERT_TRUE(static_cast<bool>(NativeRelease))
      << errorText(NativeRelease.takeError());
  EXPECT_EQ(NativeRelease->Authority,
            AndroidKernelReleaseAuthority::NativeAttested);
  EXPECT_EQ(NativeRelease->SymbolMapSource,
            AndroidKernelReleaseSymbolMapSource::NativeMerger);
  EXPECT_EQ(NativeRelease->BoundNativeOutput.get(), ProducedToken.get());
  EXPECT_FALSE(Merged->MergedImage.empty());

  ObjectMergeResult GraphMerged;
  GraphMerged.Object =
      std::make_unique<PluginObjectGraph>(std::move(GraphRouteObject));
  GraphMerged.MergedImage.assign(
      reinterpret_cast<const char *>(First->data()),
      reinterpret_cast<const char *>(First->data() + First->size()));
  AndroidKernelModuleFinalizationPolicy GraphPolicy;
  GraphPolicy.DropDebugInfo = true;
  GraphPolicy.StripUnneededSymbols = true;
  auto GraphRelease = finalizeAndroidKernelReleasePipeline(
      GraphMerged, /*ProviderBuiltin=*/false, *InputContract, GraphPolicy);
  ASSERT_TRUE(static_cast<bool>(GraphRelease))
      << errorText(GraphRelease.takeError());
  EXPECT_EQ(GraphRelease->Authority,
            AndroidKernelReleaseAuthority::GraphAuthoritative);
  EXPECT_EQ(GraphRelease->SymbolMapSource,
            AndroidKernelReleaseSymbolMapSource::GraphFinalizer);
  EXPECT_TRUE(GraphRelease->SymbolMap.has_value());
  EXPECT_EQ(GraphRelease->BoundNativeOutput, nullptr);
  EXPECT_TRUE(GraphMerged.MergedImage.empty());
}

TEST(PluginObjectMergeProviderTest,
     AnonymousRegeneratedMetadataDoesNotRequireNativeSectionPassthrough) {
  initializeBuiltinTargets();
  LinkTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());
  const BuiltinTargetRoute *AndroidRoute = findAndroidAArch64ObjectRoute();
  ASSERT_NE(AndroidRoute, nullptr);

  constexpr StringLiteral Assembly = R"(
    .text
    .globl init_module
    .type init_module, %function
init_module:
    nop
    .size init_module, .-init_module

    .section .data.refs,"aw",%progbits
    .xword init_module

    .section .native_anon,"a",%progbits
    .balign 8
    .xword 0x8877665544332211

    .section .neverc.android.kernel.profile,"a",%progbits
    .balign 8
    .globl __neverc_android_kernel_profile_contract
    .type __neverc_android_kernel_profile_contract, %object
__neverc_android_kernel_profile_contract:
    .byte 2, 0, 0, 0, 100, 2, 0, 0
    .size __neverc_android_kernel_profile_contract, 8

    .addrsig
    .addrsig_sym init_module
)";
  auto Assembled = assembleBuiltinObject(*AndroidRoute, Assembly);
  ASSERT_TRUE(static_cast<bool>(Assembled)) << errorText(Assembled.takeError());

  const StringRef AssembledRef(
      reinterpret_cast<const char *>(Assembled->data()), Assembled->size());
  auto Canonical = canonicalizeBuiltinELFTables(AssembledRef);
  ASSERT_TRUE(static_cast<bool>(Canonical)) << errorText(Canonical.takeError());
  std::vector<uint8_t> CanonicalImage(Canonical->begin(), Canonical->end());

  StringRef OriginalRef(reinterpret_cast<const char *>(CanonicalImage.data()),
                        CanonicalImage.size());
  auto Original = object::ELFFile<object::ELF64LE>::create(OriginalRef);
  ASSERT_TRUE(static_cast<bool>(Original)) << errorText(Original.takeError());
  auto OriginalSections = Original->sections();
  ASSERT_TRUE(static_cast<bool>(OriginalSections))
      << errorText(OriginalSections.takeError());
  const unsigned SectionStringTableIndex = Original->getHeader().e_shstrndx;
  ASSERT_LT(SectionStringTableIndex, OriginalSections->size());

  std::optional<unsigned> SymbolTableIndex;
  std::optional<unsigned> SymbolStringTableIndex;
  std::set<unsigned> RegeneratedMetadataIndices{SectionStringTableIndex};
  bool SawRela = false;
  bool SawLLVMAddrSig = false;
  bool SawLLVMCallGraphProfile = false;
  for (unsigned Index = 1; Index != OriginalSections->size(); ++Index) {
    const object::ELF64LE::Shdr &Section = (*OriginalSections)[Index];
    switch (Section.sh_type) {
    case ELF::SHT_SYMTAB:
      ASSERT_FALSE(SymbolTableIndex.has_value());
      SymbolTableIndex = Index;
      ASSERT_LT(Section.sh_link, OriginalSections->size());
      SymbolStringTableIndex = Section.sh_link;
      RegeneratedMetadataIndices.insert(Index);
      RegeneratedMetadataIndices.insert(Section.sh_link);
      break;
    case ELF::SHT_RELA:
      SawRela = true;
      RegeneratedMetadataIndices.insert(Index);
      break;
    case ELF::SHT_LLVM_ADDRSIG:
      SawLLVMAddrSig = true;
      RegeneratedMetadataIndices.insert(Index);
      break;
    case ELF::SHT_LLVM_CALL_GRAPH_PROFILE:
      SawLLVMCallGraphProfile = true;
      RegeneratedMetadataIndices.insert(Index);
      break;
    default:
      break;
    }
  }
  ASSERT_TRUE(SymbolTableIndex.has_value());
  ASSERT_TRUE(SymbolStringTableIndex.has_value());
  ASSERT_NE(*SymbolStringTableIndex, SectionStringTableIndex);
  ASSERT_TRUE(SawRela);
  ASSERT_TRUE(SawLLVMAddrSig || SawLLVMCallGraphProfile);

  std::vector<uint8_t> MetadataOnly = CanonicalImage;
  for (unsigned Index : RegeneratedMetadataIndices) {
    Error Patch = patchELF64SectionHeaderAtIndex(
        MetadataOnly, Index,
        [](object::ELF64LE::Shdr &Section) { Section.sh_name = 0; });
    ASSERT_FALSE(Patch) << errorText(std::move(Patch));
  }

  auto Snapshot = PluginTargetRegistry::freeze(
      ArrayRef<PluginTargetRegistrationView>(), PluginTargetRequest{});
  ASSERT_TRUE(static_cast<bool>(Snapshot)) << errorText(Snapshot.takeError());
  auto Reader = ObjectReaderProvider::create(*Snapshot);
  ASSERT_TRUE(static_cast<bool>(Reader)) << errorText(Reader.takeError());
  auto Target = makeBuiltinTargetKey(*AndroidRoute);
  ASSERT_TRUE(static_cast<bool>(Target)) << errorText(Target.takeError());
  const auto Inspect = [&](ArrayRef<uint8_t> Image, StringRef LogicalPath)
      -> Expected<AndroidKernelReleaseInputContract> {
    auto Graph = (*Reader)->read(Scope.task(), Image, LogicalPath, *Target,
                                 AndroidRoute->ObjectFormatID);
    if (!Graph)
      return Graph.takeError();
    std::array<PluginObjectGraph *, 1> Objects{Graph->get()};
    std::array<ArrayRef<uint8_t>, 1> Images{Image};
    return verifyAndroidKernelReleaseObjectMergeInputs(
        Objects, Images, Target->view(), LogicalPath);
  };

  auto MetadataContract =
      Inspect(MetadataOnly, "test anonymous regenerated metadata only");
  ASSERT_TRUE(static_cast<bool>(MetadataContract))
      << errorText(MetadataContract.takeError());
  EXPECT_FALSE(MetadataContract->hasRetainedAnonymousSections());
  EXPECT_TRUE(MetadataContract->retainedAnonymousSections().empty());

  std::vector<uint8_t> WithOrdinaryAnonymous = MetadataOnly;
  Error OrdinaryPatch = patchELF64SectionHeader(
      WithOrdinaryAnonymous, ".native_anon", 0,
      [](object::ELF64LE::Shdr &Section) { Section.sh_name = 0; });
  ASSERT_FALSE(OrdinaryPatch) << errorText(std::move(OrdinaryPatch));
  auto OrdinaryContract = Inspect(WithOrdinaryAnonymous,
                                  "test ordinary anonymous PROGBITS contrast");
  ASSERT_TRUE(static_cast<bool>(OrdinaryContract))
      << errorText(OrdinaryContract.takeError());
  EXPECT_TRUE(OrdinaryContract->hasRetainedAnonymousSections());
  ASSERT_EQ(OrdinaryContract->retainedAnonymousSections().size(), 1U);
  EXPECT_EQ(OrdinaryContract->retainedAnonymousSections().front().Type,
            ELF::SHT_PROGBITS);
  EXPECT_EQ(OrdinaryContract->retainedAnonymousSections().front().Flags,
            ELF::SHF_ALLOC);
  EXPECT_EQ(OrdinaryContract->retainedAnonymousSections().front().Size, 8U);
}

TEST(PluginObjectMergeProviderTest,
     NativeAndroidReleaseAnonymousSectionRequiresExactPassthrough) {
  initializeBuiltinTargets();
  LinkTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());
  const BuiltinTargetRoute *AndroidRoute = findAndroidAArch64ObjectRoute();
  ASSERT_NE(AndroidRoute, nullptr);

  constexpr StringLiteral Assembly = R"(
    .text
    .globl init_module
    .type init_module, %function
init_module:
    nop
    .size init_module, .-init_module

    .section .native_anon,"a",%progbits
    .balign 8
    .xword 0x0123456789abcdef

    .section .neverc.android.kernel.profile,"a",%progbits
    .balign 8
    .globl __neverc_android_kernel_profile_contract
    .type __neverc_android_kernel_profile_contract, %object
__neverc_android_kernel_profile_contract:
    .byte 2, 0, 0, 0, 100, 2, 0, 0
    .size __neverc_android_kernel_profile_contract, 8
)";
  auto NamedImage = assembleBuiltinObject(*AndroidRoute, Assembly);
  ASSERT_TRUE(static_cast<bool>(NamedImage))
      << errorText(NamedImage.takeError());
  std::vector<uint8_t> AnonymousImage = *NamedImage;
  Error AnonymousPatch = patchELF64SectionHeader(
      AnonymousImage, ".native_anon", 0,
      [](object::ELF64LE::Shdr &Section) { Section.sh_name = 0; });
  ASSERT_FALSE(AnonymousPatch) << errorText(std::move(AnonymousPatch));

  auto Snapshot = PluginTargetRegistry::freeze(
      ArrayRef<PluginTargetRegistrationView>(), PluginTargetRequest{});
  ASSERT_TRUE(static_cast<bool>(Snapshot)) << errorText(Snapshot.takeError());
  auto Reader = ObjectReaderProvider::create(*Snapshot);
  ASSERT_TRUE(static_cast<bool>(Reader)) << errorText(Reader.takeError());
  auto Target = makeBuiltinTargetKey(*AndroidRoute);
  ASSERT_TRUE(static_cast<bool>(Target)) << errorText(Target.takeError());
  auto Graph = (*Reader)->read(Scope.task(), AnonymousImage,
                               "memory://anonymous-section.o", *Target,
                               AndroidRoute->ObjectFormatID);
  ASSERT_TRUE(static_cast<bool>(Graph)) << errorText(Graph.takeError());
  ASSERT_TRUE(
      std::any_of((*Graph)->sections().begin(), (*Graph)->sections().end(),
                  [](const PluginObjectSection &Section) {
                    return StringRef(Section.Name).starts_with("$section.");
                  }))
      << "the built-in reader must expose its lossy anonymous-section "
         "placeholder";

  std::array<PluginObjectGraph *, 1> Objects{Graph->get()};
  std::array<ArrayRef<uint8_t>, 1> Images{AnonymousImage};
  auto Contract = verifyAndroidKernelReleaseObjectMergeInputs(
      Objects, Images, Target->view(),
      "test retained anonymous section input contract");
  ASSERT_TRUE(static_cast<bool>(Contract)) << errorText(Contract.takeError());
  EXPECT_TRUE(Contract->hasRetainedAnonymousSections());
  EXPECT_EQ(Contract->retainedAnonymousSections().size(), 1U);
  EXPECT_TRUE(Contract->requiresNativeImagePassthrough());
  Error UnboundOutput = verifyAndroidKernelReleaseOutputContract(
      AnonymousImage, *Contract,
      "test unbound retained anonymous section output");
  ASSERT_TRUE(static_cast<bool>(UnboundOutput));
  EXPECT_NE(errorText(std::move(UnboundOutput)).find("has not been bound"),
            std::string::npos);

  BuiltinObjectMergeConfig Config;
  Config.AndroidKernelModule = true;
  Config.FinalizeAndroidKernelModule = true;
  Config.DropDebugInfo = true;
  Config.StripUnneededSymbols = true;
  auto Merged = executeBuiltinObjectMergeAdapter(
      Scope.task(), *Snapshot, std::move(*Target), Objects, Images,
      NEVERC_LINK_OPTION_NONE, Config);
  ASSERT_TRUE(static_cast<bool>(Merged)) << errorText(Merged.takeError());
  const auto &Bound = Merged->boundAndroidKernelReleaseOutput();
  ASSERT_NE(Bound, nullptr);
  const ArrayRef<uint8_t> MergedImage(
      reinterpret_cast<const uint8_t *>(Merged->MergedImage.data()),
      Merged->MergedImage.size());
  EXPECT_EQ(Bound->nativeOutputDigest(), SHA256::hash(MergedImage));
  EXPECT_FALSE(verifyAndroidKernelReleaseOutputContract(
      MergedImage, *Bound, "test unchanged retained anonymous section output"));

  Error RenamedOutput = verifyAndroidKernelReleaseOutputContract(
      *NamedImage, *Bound, "test renamed anonymous section output");
  ASSERT_TRUE(static_cast<bool>(RenamedOutput));
  EXPECT_NE(errorText(std::move(RenamedOutput)).find("anonymous section"),
            std::string::npos);
  std::vector<uint8_t> ByteTampered(MergedImage.begin(), MergedImage.end());
  StringRef ByteTamperedRef(reinterpret_cast<const char *>(ByteTampered.data()),
                            ByteTampered.size());
  auto ByteTamperedELF =
      object::ELFFile<object::ELF64LE>::create(ByteTamperedRef);
  ASSERT_TRUE(static_cast<bool>(ByteTamperedELF))
      << errorText(ByteTamperedELF.takeError());
  auto ByteTamperedSections = ByteTamperedELF->sections();
  ASSERT_TRUE(static_cast<bool>(ByteTamperedSections))
      << errorText(ByteTamperedSections.takeError());
  bool PatchedText = false;
  for (const object::ELF64LE::Shdr &Section : *ByteTamperedSections) {
    auto Name = ByteTamperedELF->getSectionName(Section);
    ASSERT_TRUE(static_cast<bool>(Name)) << errorText(Name.takeError());
    if (*Name != ".text")
      continue;
    ASSERT_LT(Section.sh_offset, ByteTampered.size());
    ByteTampered[Section.sh_offset] ^= UINT8_C(1);
    PatchedText = true;
    break;
  }
  ASSERT_TRUE(PatchedText);
  Error ByteContract = verifyAndroidKernelReleaseOutputContract(
      ByteTampered, *Bound, "test byte-tampered bound native output");
  ASSERT_TRUE(static_cast<bool>(ByteContract));
  EXPECT_NE(errorText(std::move(ByteContract)).find("output bytes"),
            std::string::npos);
}

TEST(PluginObjectMergeProviderTest,
     BoundNativeOutputContractIsImmutableAndImageSpecific) {
  initializeBuiltinTargets();
  LinkTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());
  const BuiltinTargetRoute *AndroidRoute = findAndroidAArch64ObjectRoute();
  ASSERT_NE(AndroidRoute, nullptr);

  constexpr StringLiteral Assembly = R"(
    .text
    .globl init_module
    .type init_module, %function
init_module:
    nop
    .size init_module, .-init_module

    .section .native_anon,"a",%progbits
    .balign 8
    .xword 0x0123456789abcdef

    .section .neverc.android.kernel.profile,"a",%progbits
    .balign 8
    .globl __neverc_android_kernel_profile_contract
    .type __neverc_android_kernel_profile_contract, %object
__neverc_android_kernel_profile_contract:
    .byte 2, 0, 0, 0, 100, 2, 0, 0
    .size __neverc_android_kernel_profile_contract, 8
)";
  auto Image = assembleBuiltinObject(*AndroidRoute, Assembly);
  ASSERT_TRUE(static_cast<bool>(Image)) << errorText(Image.takeError());
  Error AnonymousPatch = patchELF64SectionHeader(
      *Image, ".native_anon", 0,
      [](object::ELF64LE::Shdr &Section) { Section.sh_name = 0; });
  ASSERT_FALSE(AnonymousPatch) << errorText(std::move(AnonymousPatch));

  auto Snapshot = PluginTargetRegistry::freeze(
      ArrayRef<PluginTargetRegistrationView>(), PluginTargetRequest{});
  ASSERT_TRUE(static_cast<bool>(Snapshot)) << errorText(Snapshot.takeError());
  auto Reader = ObjectReaderProvider::create(*Snapshot);
  ASSERT_TRUE(static_cast<bool>(Reader)) << errorText(Reader.takeError());
  auto Target = makeBuiltinTargetKey(*AndroidRoute);
  ASSERT_TRUE(static_cast<bool>(Target)) << errorText(Target.takeError());
  auto Graph =
      (*Reader)->read(Scope.task(), *Image, "memory://atomic-bind-input.o",
                      *Target, AndroidRoute->ObjectFormatID);
  ASSERT_TRUE(static_cast<bool>(Graph)) << errorText(Graph.takeError());

  std::array<PluginObjectGraph *, 1> Objects{Graph->get()};
  std::array<ArrayRef<uint8_t>, 1> Images{*Image};
  auto Contract = verifyAndroidKernelReleaseObjectMergeInputs(
      Objects, Images, Target->view(), "test atomic native-output bind input");
  ASSERT_TRUE(static_cast<bool>(Contract)) << errorText(Contract.takeError());
  BuiltinObjectMergeConfig Config;
  Config.AndroidKernelModule = true;
  Config.FinalizeAndroidKernelModule = true;
  Config.DropDebugInfo = true;
  Config.StripUnneededSymbols = true;
  auto Merged = executeBuiltinObjectMergeAdapter(
      Scope.task(), *Snapshot, std::move(*Target), Objects, Images,
      NEVERC_LINK_OPTION_NONE, Config);
  ASSERT_TRUE(static_cast<bool>(Merged)) << errorText(Merged.takeError());
  const auto &Bound = Merged->boundAndroidKernelReleaseOutput();
  ASSERT_NE(Bound, nullptr);
  const ArrayRef<uint8_t> MergedImage(
      reinterpret_cast<const uint8_t *>(Merged->MergedImage.data()),
      Merged->MergedImage.size());
  EXPECT_EQ(Bound->nativeOutputDigest(), SHA256::hash(MergedImage));

  std::vector<uint8_t> DifferentImage(MergedImage.begin(), MergedImage.end());
  StringRef DifferentRef(reinterpret_cast<const char *>(DifferentImage.data()),
                         DifferentImage.size());
  auto DifferentELF = object::ELFFile<object::ELF64LE>::create(DifferentRef);
  ASSERT_TRUE(static_cast<bool>(DifferentELF))
      << errorText(DifferentELF.takeError());
  auto DifferentSections = DifferentELF->sections();
  ASSERT_TRUE(static_cast<bool>(DifferentSections))
      << errorText(DifferentSections.takeError());
  bool ChangedText = false;
  for (const object::ELF64LE::Shdr &Section : *DifferentSections) {
    auto Name = DifferentELF->getSectionName(Section);
    ASSERT_TRUE(static_cast<bool>(Name)) << errorText(Name.takeError());
    if (*Name != ".text")
      continue;
    ASSERT_LT(Section.sh_offset, DifferentImage.size());
    DifferentImage[Section.sh_offset] ^= UINT8_C(1);
    ChangedText = true;
    break;
  }
  ASSERT_TRUE(ChangedText);

  EXPECT_FALSE(verifyAndroidKernelReleaseOutputContract(
      MergedImage, *Bound, "test original contract after failed verification"));

  Error DifferentOutput = verifyAndroidKernelReleaseOutputContract(
      DifferentImage, *Bound, "test different image after failed verification");
  ASSERT_TRUE(static_cast<bool>(DifferentOutput));
  EXPECT_NE(errorText(std::move(DifferentOutput)).find("output bytes"),
            std::string::npos);
}

TEST(PluginObjectMergeProviderTest,
     BuiltinAndroidReleaseBindsMergedAnonymousSectionMultiset) {
  initializeBuiltinTargets();
  LinkTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());
  const BuiltinTargetRoute *AndroidRoute = findAndroidAArch64ObjectRoute();
  ASSERT_NE(AndroidRoute, nullptr);

  constexpr StringLiteral FirstAssembly = R"(
    .text
    .globl init_module
    .type init_module, %function
init_module:
    nop
    .size init_module, .-init_module

    .section .native_anon,"a",%progbits
    .balign 8
    .xword 0x1111111111111111

    .section .neverc.android.kernel.profile,"a",%progbits
    .balign 8
    .globl __neverc_android_kernel_profile_contract
    .type __neverc_android_kernel_profile_contract, %object
__neverc_android_kernel_profile_contract:
    .byte 2, 0, 0, 0, 100, 2, 0, 0
    .size __neverc_android_kernel_profile_contract, 8
)";
  constexpr StringLiteral SecondAssembly = R"(
    .text
    .globl second_partition_entry
    .type second_partition_entry, %function
second_partition_entry:
    nop
    .size second_partition_entry, .-second_partition_entry

    .section .native_anon,"a",%progbits
    .balign 8
    .xword 0x2222222222222222

    .section .neverc.android.kernel.profile,"a",%progbits
    .balign 8
    .globl __neverc_android_kernel_profile_contract
    .type __neverc_android_kernel_profile_contract, %object
__neverc_android_kernel_profile_contract:
    .byte 2, 0, 0, 0, 100, 2, 0, 0
    .size __neverc_android_kernel_profile_contract, 8
)";
  auto First = assembleBuiltinObject(*AndroidRoute, FirstAssembly);
  auto Second = assembleBuiltinObject(*AndroidRoute, SecondAssembly);
  ASSERT_TRUE(static_cast<bool>(First)) << errorText(First.takeError());
  ASSERT_TRUE(static_cast<bool>(Second)) << errorText(Second.takeError());
  for (std::vector<uint8_t> *Image : {&*First, &*Second}) {
    Error Patch = patchELF64SectionHeader(
        *Image, ".native_anon", 0,
        [](object::ELF64LE::Shdr &Section) { Section.sh_name = 0; });
    ASSERT_FALSE(Patch) << errorText(std::move(Patch));
  }

  SmallString<128> Directory;
  ASSERT_FALSE(sys::fs::createUniqueDirectory(
      "neverc-native-anonymous-section-merge", Directory));
  auto RemoveDirectory =
      make_scope_exit([&] { (void)sys::fs::remove_directories(Directory); });
  SmallString<160> OutputPath(Directory);
  sys::path::append(OutputPath, "anonymous-sections-release.ko");

  linker::LinkExecutionRequest Request;
  Request.TargetTriple = AndroidRoute->CanonicalTriple.str();
  Request.OutputKind = linker::LinkExecutionOutputKind::Relocatable;
  Request.OutputURI = OutputPath.str().str();
  unsigned InputIndex = 0;
  for (auto *Image : {&*First, &*Second}) {
    linker::LinkExecutionInput Input;
    Input.Kind = linker::LinkExecutionInputKind::Object;
    Input.Ordinal = InputIndex;
    Input.LogicalURI =
        (Twine("memory://anonymous-release-input-") + Twine(InputIndex) + ".o")
            .str();
    ++InputIndex;
    Input.AuthorizedBlob = std::move(*Image);
    Request.Inputs.push_back(std::move(Input));
  }

  linker::LinkerDriverConfig Config;
  Config.pluginTask = &Scope.task();
  Config.relocatable = true;
  Config.androidKernelModule = true;
  Config.finalizeAndroidKernelModule = true;
  Config.stripMode = linker::StripMode::All;

  neverc::OutputCoordinator Outputs;
  auto SessionAlias =
      std::shared_ptr<PluginSession>(&Scope.session(), [](PluginSession *) {});
  LinkExecutionHooksBridge Bridge(std::move(SessionAlias), Outputs);
  raw_null_ostream NullOutput;
  auto Result = Bridge.execute(Request, Config, NullOutput, NullOutput);
  ASSERT_TRUE(static_cast<bool>(Result)) << errorText(Result.takeError());
  ASSERT_EQ(Result->Disposition, linker::LinkHookDisposition::Completed);
  Bridge.complete(true);

  auto Output = MemoryBuffer::getFile(OutputPath);
  ASSERT_TRUE(static_cast<bool>(Output));
  auto Parsed =
      object::ELFFile<object::ELF64LE>::create((*Output)->getBuffer());
  ASSERT_TRUE(static_cast<bool>(Parsed)) << errorText(Parsed.takeError());
  auto Sections = Parsed->sections();
  ASSERT_TRUE(static_cast<bool>(Sections)) << errorText(Sections.takeError());
  unsigned RetainedAnonymousCount = 0;
  for (unsigned Index = 1; Index != Sections->size(); ++Index) {
    const object::ELF64LE::Shdr &Section = (*Sections)[Index];
    auto Name = Parsed->getSectionName(Section);
    ASSERT_TRUE(static_cast<bool>(Name)) << errorText(Name.takeError());
    if (!Name->empty() ||
        neverc::AndroidKernelModuleSectionPolicy::regeneratesReleaseInputType(
            Section.sh_type) ||
        Index == Parsed->getHeader().e_shstrndx)
      continue;
    ++RetainedAnonymousCount;
    EXPECT_EQ(Section.sh_type, ELF::SHT_PROGBITS);
    EXPECT_EQ(Section.sh_flags, ELF::SHF_ALLOC);
    EXPECT_EQ(Section.sh_size, 16U);
    auto Contents = Parsed->getSectionContents(Section);
    ASSERT_TRUE(static_cast<bool>(Contents)) << errorText(Contents.takeError());
    const StringRef Expected("\x11\x11\x11\x11\x11\x11\x11\x11"
                             "\x22\x22\x22\x22\x22\x22\x22\x22",
                             16);
    EXPECT_EQ(*Contents, ArrayRef<uint8_t>(
                             reinterpret_cast<const uint8_t *>(Expected.data()),
                             Expected.size()));
  }
  EXPECT_EQ(RetainedAnonymousCount, 1U);
}

TEST(
    PluginObjectMergeProviderTest,
    DirectBuiltinAndroidReleaseBypassesInternalProvidersAndFoldsAnonymousInputs) {
  initializeBuiltinTargets();
  LinkTaskScope Scope;
  ASSERT_TRUE(
      Scope.initialize(NEVERC_TEST_OBJECT_PHASE_OBSERVER_PROVIDER_PLUGIN));
  const BuiltinTargetRoute *AndroidRoute = findAndroidAArch64ObjectRoute();
  ASSERT_NE(AndroidRoute, nullptr);

  constexpr StringLiteral FirstAssembly = R"(
    .text
    .globl init_module
    .type init_module, %function
init_module:
    nop
    .size init_module, .-init_module

    .section .native_anon,"a",%progbits
    .balign 8
    .xword 0x1111111111111111

    .section .neverc.android.kernel.profile,"a",%progbits
    .balign 8
    .globl __neverc_android_kernel_profile_contract
    .type __neverc_android_kernel_profile_contract, %object
__neverc_android_kernel_profile_contract:
    .byte 2, 0, 0, 0, 100, 2, 0, 0
    .size __neverc_android_kernel_profile_contract, 8
)";
  constexpr StringLiteral SecondAssembly = R"(
    .text
    .globl second_partition_entry
    .type second_partition_entry, %function
second_partition_entry:
    nop
    .size second_partition_entry, .-second_partition_entry

    .section .native_anon,"a",%progbits
    .balign 8
    .xword 0x2222222222222222

    .section .neverc.android.kernel.profile,"a",%progbits
    .balign 8
    .globl __neverc_android_kernel_profile_contract
    .type __neverc_android_kernel_profile_contract, %object
__neverc_android_kernel_profile_contract:
    .byte 2, 0, 0, 0, 100, 2, 0, 0
    .size __neverc_android_kernel_profile_contract, 8
)";
  auto First = assembleBuiltinObject(*AndroidRoute, FirstAssembly);
  auto Second = assembleBuiltinObject(*AndroidRoute, SecondAssembly);
  ASSERT_TRUE(static_cast<bool>(First)) << errorText(First.takeError());
  ASSERT_TRUE(static_cast<bool>(Second)) << errorText(Second.takeError());
  for (std::vector<uint8_t> *Image : {&*First, &*Second}) {
    Error Patch = patchELF64SectionHeader(
        *Image, ".native_anon", 0,
        [](object::ELF64LE::Shdr &Section) { Section.sh_name = 0; });
    ASSERT_FALSE(Patch) << errorText(std::move(Patch));
  }

  auto Snapshot = PluginTargetRegistry::freeze(
      ArrayRef<PluginTargetRegistrationView>(), PluginTargetRequest{});
  ASSERT_TRUE(static_cast<bool>(Snapshot)) << errorText(Snapshot.takeError());
  auto Reader = ObjectReaderProvider::create(*Snapshot);
  ASSERT_TRUE(static_cast<bool>(Reader)) << errorText(Reader.takeError());
  auto Target = makeBuiltinTargetKey(*AndroidRoute);
  ASSERT_TRUE(static_cast<bool>(Target)) << errorText(Target.takeError());
  auto FirstGraph =
      (*Reader)->read(Scope.task(), *First, "memory://direct-observer-first.o",
                      *Target, AndroidRoute->ObjectFormatID);
  auto SecondGraph = (*Reader)->read(Scope.task(), *Second,
                                     "memory://direct-observer-second.o",
                                     *Target, AndroidRoute->ObjectFormatID);
  ASSERT_TRUE(static_cast<bool>(FirstGraph))
      << errorText(FirstGraph.takeError());
  ASSERT_TRUE(static_cast<bool>(SecondGraph))
      << errorText(SecondGraph.takeError());

  std::array<PluginObjectGraph *, 2> Objects{FirstGraph->get(),
                                             SecondGraph->get()};
  std::array<ArrayRef<uint8_t>, 2> Images{*First, *Second};
  BuiltinObjectMergeConfig Config;
  Config.AndroidKernelModule = true;
  Config.FinalizeAndroidKernelModule = true;
  Config.DropDebugInfo = true;
  Config.StripUnneededSymbols = true;
  auto Merged = executeBuiltinObjectMergeAdapter(
      Scope.task(), *Snapshot, std::move(*Target), Objects, Images,
      NEVERC_LINK_OPTION_NONE, Config);
  ASSERT_TRUE(static_cast<bool>(Merged)) << errorText(Merged.takeError());
  ASSERT_NE(Merged->Object, nullptr);
  ASSERT_FALSE(Merged->MergedImage.empty());

  const StringRef MergedBytes(Merged->MergedImage.data(),
                              Merged->MergedImage.size());
  EXPECT_FALSE(MergedBytes.contains("$section."));
  auto Parsed = object::ELFFile<object::ELF64LE>::create(MergedBytes);
  ASSERT_TRUE(static_cast<bool>(Parsed)) << errorText(Parsed.takeError());
  auto Sections = Parsed->sections();
  ASSERT_TRUE(static_cast<bool>(Sections)) << errorText(Sections.takeError());
  unsigned RetainedAnonymousCount = 0;
  for (unsigned Index = 1; Index != Sections->size(); ++Index) {
    const object::ELF64LE::Shdr &Section = (*Sections)[Index];
    auto Name = Parsed->getSectionName(Section);
    ASSERT_TRUE(static_cast<bool>(Name)) << errorText(Name.takeError());
    if (!Name->empty() ||
        neverc::AndroidKernelModuleSectionPolicy::regeneratesReleaseInputType(
            Section.sh_type) ||
        Index == Parsed->getHeader().e_shstrndx)
      continue;
    ++RetainedAnonymousCount;
    EXPECT_EQ(Section.sh_type, ELF::SHT_PROGBITS);
    EXPECT_EQ(Section.sh_flags, ELF::SHF_ALLOC);
    EXPECT_EQ(Section.sh_size, 16U);
    auto Contents = Parsed->getSectionContents(Section);
    ASSERT_TRUE(static_cast<bool>(Contents)) << errorText(Contents.takeError());
    const StringRef Expected("\x11\x11\x11\x11\x11\x11\x11\x11"
                             "\x22\x22\x22\x22\x22\x22\x22\x22",
                             16);
    EXPECT_EQ(*Contents, ArrayRef<uint8_t>(
                             reinterpret_cast<const uint8_t *>(Expected.data()),
                             Expected.size()));
  }
  EXPECT_EQ(RetainedAnonymousCount, 1U);
}

TEST(PluginObjectMergeProviderTest,
     DirectBuiltinAndroidReleaseBypassesInternalInterceptors) {
  initializeBuiltinTargets();
  LinkTaskScope Scope;
  ASSERT_TRUE(
      Scope.initialize(NEVERC_TEST_OBJECT_PHASE_OBSERVER_INTERCEPTOR_PLUGIN));
  const BuiltinTargetRoute *AndroidRoute = findAndroidAArch64ObjectRoute();
  ASSERT_NE(AndroidRoute, nullptr);

  auto Input = assembleValidAndroidReleaseInput(*AndroidRoute);
  ASSERT_TRUE(static_cast<bool>(Input)) << errorText(Input.takeError());
  Error AnonymousPatch = patchELF64SectionHeader(
      *Input, ".native_extra", 0,
      [](object::ELF64LE::Shdr &Section) { Section.sh_name = 0; });
  ASSERT_FALSE(AnonymousPatch) << errorText(std::move(AnonymousPatch));

  auto Snapshot = PluginTargetRegistry::freeze(
      ArrayRef<PluginTargetRegistrationView>(), PluginTargetRequest{});
  ASSERT_TRUE(static_cast<bool>(Snapshot)) << errorText(Snapshot.takeError());
  auto Reader = ObjectReaderProvider::create(*Snapshot);
  ASSERT_TRUE(static_cast<bool>(Reader)) << errorText(Reader.takeError());
  auto Target = makeBuiltinTargetKey(*AndroidRoute);
  ASSERT_TRUE(static_cast<bool>(Target)) << errorText(Target.takeError());
  auto Graph =
      (*Reader)->read(Scope.task(), *Input, "memory://direct-anonymous-input.o",
                      *Target, AndroidRoute->ObjectFormatID);
  ASSERT_TRUE(static_cast<bool>(Graph)) << errorText(Graph.takeError());

  PluginObjectGraph *GraphPointer = Graph->get();
  std::array<PluginObjectGraph *, 1> Objects{GraphPointer};
  std::array<ArrayRef<uint8_t>, 1> Images{*Input};
  BuiltinObjectMergeConfig Config;
  Config.AndroidKernelModule = true;
  Config.FinalizeAndroidKernelModule = true;
  Config.DropDebugInfo = true;
  Config.StripUnneededSymbols = true;
  auto Merged = executeBuiltinObjectMergeAdapter(
      Scope.task(), *Snapshot, std::move(*Target), Objects, Images,
      NEVERC_LINK_OPTION_NONE, Config);
  ASSERT_TRUE(static_cast<bool>(Merged)) << errorText(Merged.takeError());
  ASSERT_NE(Merged->Object, nullptr);
  ASSERT_FALSE(Merged->MergedImage.empty());
  ASSERT_NE(Merged->boundAndroidKernelReleaseOutput(), nullptr);

  AndroidKernelModuleFinalizationPolicy Policy;
  Policy.DropDebugInfo = true;
  Policy.StripUnneededSymbols = true;
  EXPECT_FALSE(verifyFinalAndroidKernelModuleImage(
      ArrayRef<uint8_t>(
          reinterpret_cast<const uint8_t *>(Merged->MergedImage.data()),
          Merged->MergedImage.size()),
      Policy, "test direct release bypass of internal interceptors"));
}

TEST(PluginObjectMergeProviderTest,
     NativeAndroidReleaseABIContractRejectsMismatchAndOutputTampering) {
  initializeBuiltinTargets();
  LinkTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());
  const BuiltinTargetRoute *AndroidRoute = findAndroidAArch64ObjectRoute();
  ASSERT_NE(AndroidRoute, nullptr);

  auto Snapshot = PluginTargetRegistry::freeze(
      ArrayRef<PluginTargetRegistrationView>(), PluginTargetRequest{});
  ASSERT_TRUE(static_cast<bool>(Snapshot)) << errorText(Snapshot.takeError());

  auto FirstImage = assembleValidAndroidReleaseInput(*AndroidRoute);
  ASSERT_TRUE(static_cast<bool>(FirstImage))
      << errorText(FirstImage.takeError());
  Error FirstPatch =
      patchELF64Header(*FirstImage, [](object::ELF64LE::Ehdr &Header) {
        Header.e_flags = UINT32_C(0x13579);
        Header.e_ident[ELF::EI_OSABI] = ELF::ELFOSABI_GNU;
        Header.e_ident[ELF::EI_ABIVERSION] = UINT8_C(9);
      });
  ASSERT_FALSE(FirstPatch) << errorText(std::move(FirstPatch));
  std::vector<uint8_t> SecondImage = *FirstImage;

  auto FirstGraph = makeBuiltinObject(*AndroidRoute, "first_abi_input");
  auto SecondGraph = makeBuiltinObject(*AndroidRoute, "second_abi_input");
  ASSERT_NE(FirstGraph, nullptr);
  ASSERT_NE(SecondGraph, nullptr);
  auto Target = makeBuiltinTargetKey(*AndroidRoute);
  ASSERT_TRUE(static_cast<bool>(Target)) << errorText(Target.takeError());
  std::array<PluginObjectGraph *, 2> Objects{FirstGraph.get(),
                                             SecondGraph.get()};
  std::array<ArrayRef<uint8_t>, 2> MatchingImages{*FirstImage, SecondImage};
  auto Contract = verifyAndroidKernelReleaseObjectMergeInputs(
      Objects, MatchingImages, Target->view(),
      "test matching native-only Android ABI inputs");
  ASSERT_TRUE(static_cast<bool>(Contract)) << errorText(Contract.takeError());
  EXPECT_EQ(Contract->abi().Machine, ELF::EM_AARCH64);
  EXPECT_EQ(Contract->abi().Flags, UINT32_C(0x13579));
  EXPECT_EQ(Contract->abi().OSABI, ELF::ELFOSABI_GNU);
  EXPECT_EQ(Contract->abi().ABIVersion, 9U);
  EXPECT_TRUE(Contract->requiresNativeImagePassthrough());

  Error MismatchPatch =
      patchELF64Header(SecondImage, [](object::ELF64LE::Ehdr &Header) {
        Header.e_flags = static_cast<uint32_t>(Header.e_flags) + UINT32_C(1);
      });
  ASSERT_FALSE(MismatchPatch) << errorText(std::move(MismatchPatch));
  std::array<ArrayRef<uint8_t>, 2> MismatchedImages{*FirstImage, SecondImage};
  auto Mismatched = verifyAndroidKernelReleaseObjectMergeInputs(
      Objects, MismatchedImages, Target->view(),
      "test mismatched native-only Android ABI inputs");
  ASSERT_FALSE(Mismatched);
  EXPECT_NE(errorText(Mismatched.takeError()).find("inconsistent ELF ABI"),
            std::string::npos);

  std::array<PluginObjectGraph *, 1> MergeObjects{FirstGraph.get()};
  std::array<ArrayRef<uint8_t>, 1> MergeImages{*FirstImage};
  auto MergeTarget = makeBuiltinTargetKey(*AndroidRoute);
  ASSERT_TRUE(static_cast<bool>(MergeTarget))
      << errorText(MergeTarget.takeError());
  BuiltinObjectMergeConfig Config;
  Config.AndroidKernelModule = true;
  Config.FinalizeAndroidKernelModule = true;
  Config.DropDebugInfo = true;
  Config.StripUnneededSymbols = true;
  auto Merged = executeBuiltinObjectMergeAdapter(
      Scope.task(), *Snapshot, std::move(*MergeTarget), MergeObjects,
      MergeImages, NEVERC_LINK_OPTION_NONE, Config);
  ASSERT_TRUE(static_cast<bool>(Merged)) << errorText(Merged.takeError());
  const auto &Bound = Merged->boundAndroidKernelReleaseOutput();
  ASSERT_NE(Bound, nullptr);
  const ArrayRef<uint8_t> MergedImage(
      reinterpret_cast<const uint8_t *>(Merged->MergedImage.data()),
      Merged->MergedImage.size());
  EXPECT_FALSE(verifyAndroidKernelReleaseOutputContract(
      MergedImage, *Bound, "test unchanged native-only Android output"));

  std::vector<uint8_t> TamperedOutput(MergedImage.begin(), MergedImage.end());
  Error OutputPatch =
      patchELF64Header(TamperedOutput, [](object::ELF64LE::Ehdr &Header) {
        Header.e_ident[ELF::EI_ABIVERSION] ^= UINT8_C(1);
      });
  ASSERT_FALSE(OutputPatch) << errorText(std::move(OutputPatch));
  Error OutputContract = verifyAndroidKernelReleaseOutputContract(
      TamperedOutput, *Bound, "test tampered native-only Android output");
  ASSERT_TRUE(static_cast<bool>(OutputContract));
  EXPECT_NE(errorText(std::move(OutputContract)).find("does not match"),
            std::string::npos);
}

TEST(PluginObjectMergeProviderTest,
     BuiltinAndroidReleasePreservesNativeOnlyABIWithoutGraphPhases) {
  initializeBuiltinTargets();
  LinkTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());
  const BuiltinTargetRoute *AndroidRoute = findAndroidAArch64ObjectRoute();
  ASSERT_NE(AndroidRoute, nullptr);

  auto Input = assembleValidAndroidReleaseInput(*AndroidRoute);
  ASSERT_TRUE(static_cast<bool>(Input)) << errorText(Input.takeError());
  Error Patched = patchELF64Header(*Input, [](object::ELF64LE::Ehdr &Header) {
    Header.e_flags = UINT32_C(0x2468);
    Header.e_ident[ELF::EI_OSABI] = ELF::ELFOSABI_GNU;
    Header.e_ident[ELF::EI_ABIVERSION] = UINT8_C(7);
  });
  ASSERT_FALSE(Patched) << errorText(std::move(Patched));

  SmallString<128> Directory;
  ASSERT_FALSE(sys::fs::createUniqueDirectory("neverc-native-abi-passthrough",
                                              Directory));
  auto RemoveDirectory =
      make_scope_exit([&] { (void)sys::fs::remove_directories(Directory); });
  SmallString<160> OutputPath(Directory);
  sys::path::append(OutputPath, "native-abi-release.ko");

  linker::LinkExecutionRequest Request;
  Request.TargetTriple = AndroidRoute->CanonicalTriple.str();
  Request.OutputKind = linker::LinkExecutionOutputKind::Relocatable;
  Request.OutputURI = OutputPath.str().str();
  linker::LinkExecutionInput LinkInput;
  LinkInput.Kind = linker::LinkExecutionInputKind::Object;
  LinkInput.LogicalURI = "memory://native-abi-release-input.o";
  LinkInput.AuthorizedBlob = std::move(*Input);
  Request.Inputs.push_back(std::move(LinkInput));

  linker::LinkerDriverConfig Config;
  Config.pluginTask = &Scope.task();
  Config.relocatable = true;
  Config.androidKernelModule = true;
  Config.finalizeAndroidKernelModule = true;
  Config.stripMode = linker::StripMode::All;

  neverc::OutputCoordinator Outputs;
  auto SessionAlias =
      std::shared_ptr<PluginSession>(&Scope.session(), [](PluginSession *) {});
  LinkExecutionHooksBridge Bridge(std::move(SessionAlias), Outputs);
  raw_null_ostream NullOutput;
  auto Result = Bridge.execute(Request, Config, NullOutput, NullOutput);
  ASSERT_TRUE(static_cast<bool>(Result)) << errorText(Result.takeError());
  EXPECT_EQ(Result->Disposition, linker::LinkHookDisposition::Completed);
  Bridge.complete(true);

  auto Output = MemoryBuffer::getFile(OutputPath);
  ASSERT_TRUE(static_cast<bool>(Output));
  StringRef OutputBytes = (*Output)->getBuffer();
  auto Parsed = object::ELFFile<object::ELF64LE>::create(OutputBytes);
  ASSERT_TRUE(static_cast<bool>(Parsed)) << errorText(Parsed.takeError());
  const object::ELF64LE::Ehdr &Header = Parsed->getHeader();
  EXPECT_EQ(Header.e_flags, UINT32_C(0x2468));
  EXPECT_EQ(Header.e_ident[ELF::EI_OSABI], ELF::ELFOSABI_GNU);
  EXPECT_EQ(Header.e_ident[ELF::EI_ABIVERSION], 7U);
}

TEST(PluginObjectMergeProviderTest,
     NativeOnlyAndroidABIRejectsCustomProviderAndObjectPhasesBeforeSink) {
  initializeBuiltinTargets();
  const BuiltinTargetRoute *AndroidRoute = findAndroidAArch64ObjectRoute();
  ASSERT_NE(AndroidRoute, nullptr);

  const auto RunRejected = [&](StringRef PluginPath, StringRef ExpectedReason,
                               StringRef OutputStem, bool UseAnonymousSection) {
    LinkTaskScope Scope;
    ASSERT_TRUE(Scope.initialize(PluginPath));
    auto Input = assembleValidAndroidReleaseInput(*AndroidRoute);
    ASSERT_TRUE(static_cast<bool>(Input)) << errorText(Input.takeError());
    if (UseAnonymousSection) {
      Error SectionPatch = patchELF64SectionHeader(
          *Input, ".native_extra", 0,
          [](object::ELF64LE::Shdr &Section) { Section.sh_name = 0; });
      ASSERT_FALSE(SectionPatch) << errorText(std::move(SectionPatch));
    } else {
      Error HeaderPatch =
          patchELF64Header(*Input, [](object::ELF64LE::Ehdr &Header) {
            Header.e_ident[ELF::EI_OSABI] = ELF::ELFOSABI_GNU;
          });
      ASSERT_FALSE(HeaderPatch) << errorText(std::move(HeaderPatch));
    }

    SmallString<128> Directory;
    ASSERT_FALSE(sys::fs::createUniqueDirectory(OutputStem, Directory));
    auto RemoveDirectory =
        make_scope_exit([&] { (void)sys::fs::remove_directories(Directory); });
    SmallString<160> OutputPath(Directory);
    sys::path::append(OutputPath, "must-not-open.ko");

    linker::LinkExecutionRequest Request;
    Request.TargetTriple = AndroidRoute->CanonicalTriple.str();
    Request.OutputKind = linker::LinkExecutionOutputKind::Relocatable;
    Request.OutputURI = OutputPath.str().str();
    linker::LinkExecutionInput LinkInput;
    LinkInput.Kind = linker::LinkExecutionInputKind::Object;
    LinkInput.LogicalURI = "memory://native-only-rejected-input.o";
    LinkInput.AuthorizedBlob = std::move(*Input);
    Request.Inputs.push_back(std::move(LinkInput));

    linker::LinkerDriverConfig Config;
    Config.pluginTask = &Scope.task();
    Config.relocatable = true;
    Config.androidKernelModule = true;
    Config.finalizeAndroidKernelModule = true;
    Config.stripMode = linker::StripMode::All;

    neverc::OutputCoordinator Outputs;
    auto SessionAlias = std::shared_ptr<PluginSession>(&Scope.session(),
                                                       [](PluginSession *) {});
    LinkExecutionHooksBridge Bridge(std::move(SessionAlias), Outputs);
    raw_null_ostream NullOutput;
    auto Result = Bridge.execute(Request, Config, NullOutput, NullOutput);
    ASSERT_FALSE(Result);
    const std::string Message = errorText(Result.takeError());
    EXPECT_NE(Message.find(ExpectedReason.str()), std::string::npos) << Message;
    EXPECT_FALSE(sys::fs::exists(OutputPath));
  };

  RunRejected(NEVERC_TEST_OBJECT_MERGE_PLUGIN,
              "third-party ObjectMergeProvider cannot preserve native-only",
              "neverc-native-abi-custom-provider", false);
  RunRejected(NEVERC_TEST_OBJECT_POST_WRITE_PLUGIN,
              "incompatible with registered ObjectGraph/output phase bindings",
              "neverc-native-abi-object-phase", false);
  RunRejected(NEVERC_TEST_OBJECT_CONTRACT_CORRUPT_PLUGIN,
              "incompatible with registered ObjectGraph/output phase bindings",
              "neverc-native-abi-graph-phase", false);
  RunRejected(NEVERC_TEST_OBJECT_MERGE_PLUGIN,
              "third-party ObjectMergeProvider cannot preserve native-only",
              "neverc-native-anonymous-custom-provider", true);
  RunRejected(NEVERC_TEST_OBJECT_POST_WRITE_PLUGIN,
              "incompatible with registered ObjectGraph/output phase bindings",
              "neverc-native-anonymous-object-phase", true);
  RunRejected(NEVERC_TEST_OBJECT_CONTRACT_CORRUPT_PLUGIN,
              "incompatible with registered ObjectGraph/output phase bindings",
              "neverc-native-anonymous-graph-phase", true);
}

TEST(PluginObjectMergeProviderTest,
     CustomProviderCannotBypassNativeAndroidReleaseInputAudit) {
  initializeBuiltinTargets();
  LinkTaskScope Scope;
  ASSERT_TRUE(Scope.initialize(NEVERC_TEST_OBJECT_MERGE_PLUGIN));

  const BuiltinTargetRoute *AndroidRoute = nullptr;
  for (const BuiltinTargetRoute &Route : builtinTargetRoutes()) {
    const Triple Parsed(Triple::normalize(Route.CanonicalTriple));
    if (Route.SupportsObject &&
        Route.ObjectFormat == BuiltinObjectFormat::ELF &&
        Parsed.getArch() == Triple::aarch64 && Parsed.isAndroid()) {
      AndroidRoute = &Route;
      break;
    }
  }
  ASSERT_NE(AndroidRoute, nullptr);

  constexpr StringLiteral Assembly = R"(
    .text
    .globl release_input_entry
    .type release_input_entry, %function
release_input_entry:
    nop
    .size release_input_entry, .-release_input_entry
    .globl livepatch_target
    .type livepatch_target, %object
livepatch_target:
    .word 0
    .size livepatch_target, .-livepatch_target

    .section .neverc.android.kernel.profile,"a",%progbits
    .balign 8
    .globl __neverc_android_kernel_profile_contract
    .type __neverc_android_kernel_profile_contract, %object
__neverc_android_kernel_profile_contract:
    .byte 2, 0, 0, 0, 100, 2, 0, 0
    .size __neverc_android_kernel_profile_contract, 8
)";
  auto Input = assembleBuiltinObject(*AndroidRoute, Assembly);
  ASSERT_TRUE(static_cast<bool>(Input)) << errorText(Input.takeError());
  Error Patched = patchELF64SymbolSectionIndex(
      *Input, "livepatch_target",
      neverc::AndroidKernelModuleSymbolPolicy::LivePatchSectionIndex);
  ASSERT_FALSE(Patched) << errorText(std::move(Patched));

  linker::LinkExecutionRequest Request;
  Request.TargetTriple = AndroidRoute->CanonicalTriple.str();
  Request.OutputKind = linker::LinkExecutionOutputKind::Relocatable;
  Request.OutputURI = "custom-provider-invalid-release.ko";
  linker::LinkExecutionInput LinkInput;
  LinkInput.Kind = linker::LinkExecutionInputKind::Object;
  LinkInput.LogicalURI = "memory://livepatch-release-input.o";
  LinkInput.AuthorizedBlob = std::move(*Input);
  Request.Inputs.push_back(std::move(LinkInput));

  linker::LinkerDriverConfig Config;
  Config.pluginTask = &Scope.task();
  Config.relocatable = true;
  Config.androidKernelModule = true;
  Config.finalizeAndroidKernelModule = true;
  Config.stripMode = linker::StripMode::All;

  neverc::OutputCoordinator Outputs;
  auto SessionAlias =
      std::shared_ptr<PluginSession>(&Scope.session(), [](PluginSession *) {});
  LinkExecutionHooksBridge Bridge(std::move(SessionAlias), Outputs);
  raw_null_ostream NullOutput;
  auto Result = Bridge.execute(Request, Config, NullOutput, NullOutput);
  ASSERT_FALSE(static_cast<bool>(Result));
  const std::string Message = errorText(Result.takeError());
  EXPECT_NE(Message.find("livepatch symbol"), std::string::npos) << Message;
}

TEST(PluginObjectMergeProviderTest,
     BuiltinAdapterRoundTripsTypedGraphsThroughRelocatableMerge) {
  initializeBuiltinTargets();
  LinkTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());

  const BuiltinTargetRoute *ELFRoute = nullptr;
  for (const BuiltinTargetRoute &Route : builtinTargetRoutes()) {
    const Triple Parsed(Triple::normalize(Route.CanonicalTriple));
    if (Route.SupportsObject &&
        Route.ObjectFormat == BuiltinObjectFormat::ELF &&
        Parsed.getArch() == Triple::x86_64) {
      ELFRoute = &Route;
      break;
    }
  }
  ASSERT_NE(ELFRoute, nullptr);

  auto First = makeBuiltinObject(*ELFRoute, "merge_first");
  auto Second = makeBuiltinObject(*ELFRoute, "merge_second");
  auto Target = makeBuiltinTargetKey(*ELFRoute);
  ASSERT_NE(First, nullptr);
  ASSERT_NE(Second, nullptr);
  ASSERT_TRUE(static_cast<bool>(Target)) << errorText(Target.takeError());

  auto Snapshot = PluginTargetRegistry::freeze(
      ArrayRef<PluginTargetRegistrationView>(), PluginTargetRequest{});
  ASSERT_TRUE(static_cast<bool>(Snapshot)) << errorText(Snapshot.takeError());
  std::array<PluginObjectGraph *, 2> Inputs{First.get(), Second.get()};
  auto Merged = executeBuiltinObjectMergeAdapter(Scope.task(), *Snapshot,
                                                 std::move(*Target), Inputs);
  ASSERT_TRUE(static_cast<bool>(Merged)) << errorText(Merged.takeError());
  ASSERT_NE(Merged->Object, nullptr);
  ASSERT_FALSE(verifyPluginObjectGraph(*Merged->Object));
  EXPECT_EQ(Merged->PluginID, "neverc.builtin");
  EXPECT_EQ(Merged->ProviderID, "neverc.builtin.object-merge");

  const auto HasSymbol = [&](StringRef Name) {
    const auto &Symbols = Merged->Object->symbols();
    return std::any_of(
        Symbols.begin(), Symbols.end(),
        [&](const PluginObjectSymbol &Symbol) { return Symbol.Name == Name; });
  };
  EXPECT_TRUE(HasSymbol("merge_first"));
  EXPECT_TRUE(HasSymbol("merge_second"));
}

TEST(PluginObjectMergeProviderTest,
     NativeOnlyAndroidReleaseAllowsReadOnlyObjectObservers) {
  initializeBuiltinTargets();
  const BuiltinTargetRoute *AndroidRoute = findAndroidAArch64ObjectRoute();
  ASSERT_NE(AndroidRoute, nullptr);

  const auto Run =
      [&](StringRef PluginPath,
          StringRef OutputStem) -> std::optional<std::vector<uint8_t>> {
    LinkTaskScope Scope;
    if (!Scope.initialize(PluginPath))
      return std::nullopt;
    auto Input = assembleValidAndroidReleaseInput(*AndroidRoute);
    if (!Input) {
      ADD_FAILURE() << errorText(Input.takeError());
      return std::nullopt;
    }
    Error Patched = patchELF64Header(*Input, [](object::ELF64LE::Ehdr &Header) {
      Header.e_flags = UINT32_C(0x5a17);
      Header.e_ident[ELF::EI_OSABI] = ELF::ELFOSABI_GNU;
      Header.e_ident[ELF::EI_ABIVERSION] = UINT8_C(11);
    });
    if (Patched) {
      ADD_FAILURE() << errorText(std::move(Patched));
      return std::nullopt;
    }
    Error AnonymousPatch = patchELF64SectionHeader(
        *Input, ".native_extra", 0,
        [](object::ELF64LE::Shdr &Section) { Section.sh_name = 0; });
    if (AnonymousPatch) {
      ADD_FAILURE() << errorText(std::move(AnonymousPatch));
      return std::nullopt;
    }

    SmallString<128> Directory;
    if (std::error_code EC =
            sys::fs::createUniqueDirectory(OutputStem, Directory)) {
      ADD_FAILURE() << EC.message();
      return std::nullopt;
    }
    auto RemoveDirectory =
        make_scope_exit([&] { (void)sys::fs::remove_directories(Directory); });
    SmallString<160> OutputPath(Directory);
    sys::path::append(OutputPath, "observer-release.ko");

    linker::LinkExecutionRequest Request;
    Request.TargetTriple = AndroidRoute->CanonicalTriple.str();
    Request.OutputKind = linker::LinkExecutionOutputKind::Relocatable;
    Request.OutputURI = OutputPath.str().str();
    linker::LinkExecutionInput LinkInput;
    LinkInput.Kind = linker::LinkExecutionInputKind::Object;
    LinkInput.LogicalURI = "memory://native-only-observer-input.o";
    LinkInput.AuthorizedBlob = std::move(*Input);
    Request.Inputs.push_back(std::move(LinkInput));

    linker::LinkerDriverConfig Config;
    Config.pluginTask = &Scope.task();
    Config.relocatable = true;
    Config.androidKernelModule = true;
    Config.finalizeAndroidKernelModule = true;
    Config.stripMode = linker::StripMode::All;

    neverc::OutputCoordinator Outputs;
    auto SessionAlias = std::shared_ptr<PluginSession>(&Scope.session(),
                                                       [](PluginSession *) {});
    LinkExecutionHooksBridge Bridge(std::move(SessionAlias), Outputs);
    raw_null_ostream NullOutput;
    auto Result = Bridge.execute(Request, Config, NullOutput, NullOutput);
    if (!Result) {
      ADD_FAILURE() << errorText(Result.takeError());
      return std::nullopt;
    }
    if (Result->Disposition != linker::LinkHookDisposition::Completed) {
      ADD_FAILURE() << "native-only observer link did not complete";
      Bridge.complete(false);
      return std::nullopt;
    }
    Bridge.complete(true);

    auto Output = MemoryBuffer::getFile(OutputPath);
    if (!Output) {
      ADD_FAILURE() << Output.getError().message();
      return std::nullopt;
    }
    StringRef Bytes = (*Output)->getBuffer();
    return std::vector<uint8_t>(Bytes.bytes_begin(), Bytes.bytes_end());
  };

  auto Baseline = Run({}, "neverc-native-only-observer-baseline");
  ASSERT_TRUE(Baseline.has_value());
  auto Observed = Run(NEVERC_TEST_OBJECT_PHASE_OBSERVER_PLUGIN,
                      "neverc-native-only-observer-plugin");
  ASSERT_TRUE(Observed.has_value());
  EXPECT_EQ(*Observed, *Baseline);

  StringRef ObservedImage(reinterpret_cast<const char *>(Observed->data()),
                          Observed->size());
  auto Parsed = object::ELFFile<object::ELF64LE>::create(ObservedImage);
  ASSERT_TRUE(static_cast<bool>(Parsed)) << errorText(Parsed.takeError());
  EXPECT_EQ(Parsed->getHeader().e_flags, UINT32_C(0x5a17));
  EXPECT_EQ(Parsed->getHeader().e_ident[ELF::EI_OSABI], ELF::ELFOSABI_GNU);
  EXPECT_EQ(Parsed->getHeader().e_ident[ELF::EI_ABIVERSION], 11U);
  auto ObserverSections = Parsed->sections();
  ASSERT_TRUE(static_cast<bool>(ObserverSections))
      << errorText(ObserverSections.takeError());
  unsigned AnonymousLogicalSections = 0;
  for (unsigned Index = 1; Index != ObserverSections->size(); ++Index) {
    const object::ELF64LE::Shdr &Section = (*ObserverSections)[Index];
    auto Name = Parsed->getSectionName(Section);
    ASSERT_TRUE(static_cast<bool>(Name)) << errorText(Name.takeError());
    if (Name->empty() &&
        !neverc::AndroidKernelModuleSectionPolicy::regeneratesReleaseInputType(
            Section.sh_type) &&
        Index != Parsed->getHeader().e_shstrndx)
      ++AnonymousLogicalSections;
  }
  EXPECT_EQ(AnonymousLogicalSections, 1U);
}

static std::optional<std::vector<uint8_t>>
runObjectCapabilityCachePipeline(StringRef PluginPath, StringRef OutputName) {
  LinkTaskScope Scope;
  if (!Scope.initialize(PluginPath))
    return std::nullopt;
  initializeBuiltinTargets();
  const BuiltinTargetRoute *AndroidRoute = findAndroidAArch64ObjectRoute();
  if (!AndroidRoute) {
    ADD_FAILURE() << "AArch64 Android object route is unavailable";
    return std::nullopt;
  }
  auto Graph = makeBuiltinObject(*AndroidRoute, "capability_cache_entry");
  if (!Graph) {
    ADD_FAILURE() << "capability-cache graph could not be created";
    return std::nullopt;
  }
  auto Snapshot = PluginTargetRegistry::freeze(
      ArrayRef<PluginTargetRegistrationView>(), PluginTargetRequest{});
  if (!Snapshot) {
    ADD_FAILURE() << errorText(Snapshot.takeError());
    return std::nullopt;
  }
  auto Pipeline = ObjectPhasePipeline::create(Scope.task(), *Snapshot);
  if (!Pipeline) {
    ADD_FAILURE() << errorText(Pipeline.takeError());
    return std::nullopt;
  }
  ObjectOutputDestination Destination =
      ObjectOutputDestination::memory(OutputName, UINT64_C(1) << 20);
  auto Image = (*Pipeline)->execute(*Graph, Destination);
  if (!Image) {
    ADD_FAILURE() << errorText(Image.takeError());
    return std::nullopt;
  }
  auto Output = findPluginMemoryOutput(Scope.task(), OutputName);
  if (!Output) {
    ADD_FAILURE() << "capability-cache pipeline published no memory output";
    return std::nullopt;
  }
  return std::move(Output->Bytes);
}

TEST(PluginObjectMergeProviderTest,
     CachedObjectCapabilitiesCannotMutateFromAfterObservers) {
  auto Baseline = runObjectCapabilityCachePipeline(
      {}, "capability-cache-after-observer-baseline.o");
  ASSERT_TRUE(Baseline.has_value());
  auto Protected = runObjectCapabilityCachePipeline(
      NEVERC_TEST_OBJECT_CAPABILITY_AFTER_OBSERVER_PLUGIN,
      "capability-cache-after-observer-protected.o");
  ASSERT_TRUE(Protected.has_value());
  EXPECT_EQ(*Protected, *Baseline);
}

TEST(PluginObjectMergeProviderTest,
     CachedObjectCapabilitiesCannotMutateAcrossThreads) {
  auto Baseline = runObjectCapabilityCachePipeline(
      {}, "capability-cache-cross-thread-baseline.o");
  ASSERT_TRUE(Baseline.has_value());
  auto Protected = runObjectCapabilityCachePipeline(
      NEVERC_TEST_OBJECT_CAPABILITY_CROSS_THREAD_PLUGIN,
      "capability-cache-cross-thread-protected.o");
  ASSERT_TRUE(Protected.has_value());
  EXPECT_EQ(*Protected, *Baseline);
}

TEST(PluginObjectMergeProviderTest,
     PreWriteGraphFacadeRemainsSafeThroughPostLayoutObserver) {
  auto Baseline = runObjectCapabilityCachePipeline(
      {}, "capability-cache-graph-cross-phase-baseline.o");
  ASSERT_TRUE(Baseline.has_value());
  auto Protected = runObjectCapabilityCachePipeline(
      NEVERC_TEST_OBJECT_CAPABILITY_GRAPH_CROSS_PHASE_PLUGIN,
      "capability-cache-graph-cross-phase-protected.o");
  ASSERT_TRUE(Protected.has_value());
  EXPECT_EQ(*Protected, *Baseline);
}

TEST(PluginObjectMergeProviderTest,
     PostWriteBinaryFacadeRemainsSafeThroughFinalVerifyObserver) {
  auto Baseline = runObjectCapabilityCachePipeline(
      {}, "capability-cache-binary-cross-phase-baseline.o");
  ASSERT_TRUE(Baseline.has_value());
  auto Protected = runObjectCapabilityCachePipeline(
      NEVERC_TEST_OBJECT_CAPABILITY_BINARY_CROSS_PHASE_PLUGIN,
      "capability-cache-binary-cross-phase-protected.o");
  ASSERT_TRUE(Protected.has_value());
  EXPECT_EQ(*Protected, *Baseline);
}

TEST(PluginObjectMergeProviderTest,
     NativeOnlyAndroidReleaseRejectsArtifactReplacementHooksBeforeSink) {
  initializeBuiltinTargets();
  const BuiltinTargetRoute *AndroidRoute = findAndroidAArch64ObjectRoute();
  ASSERT_NE(AndroidRoute, nullptr);

  const auto RunRejected = [&](StringRef PluginPath, StringRef OutputStem) {
    LinkTaskScope Scope;
    ASSERT_TRUE(Scope.initialize(PluginPath));
    auto Input = assembleValidAndroidReleaseInput(*AndroidRoute);
    ASSERT_TRUE(static_cast<bool>(Input)) << errorText(Input.takeError());
    Error Patched = patchELF64Header(*Input, [](object::ELF64LE::Ehdr &Header) {
      Header.e_flags = UINT32_C(0x7b19);
      Header.e_ident[ELF::EI_OSABI] = ELF::ELFOSABI_GNU;
    });
    ASSERT_FALSE(Patched) << errorText(std::move(Patched));
    Error AnonymousPatch = patchELF64SectionHeader(
        *Input, ".native_extra", 0,
        [](object::ELF64LE::Shdr &Section) { Section.sh_name = 0; });
    ASSERT_FALSE(AnonymousPatch) << errorText(std::move(AnonymousPatch));

    SmallString<128> Directory;
    ASSERT_FALSE(sys::fs::createUniqueDirectory(OutputStem, Directory));
    auto RemoveDirectory =
        make_scope_exit([&] { (void)sys::fs::remove_directories(Directory); });
    SmallString<160> OutputPath(Directory);
    sys::path::append(OutputPath, "must-not-open.ko");

    linker::LinkExecutionRequest Request;
    Request.TargetTriple = AndroidRoute->CanonicalTriple.str();
    Request.OutputKind = linker::LinkExecutionOutputKind::Relocatable;
    Request.OutputURI = OutputPath.str().str();
    linker::LinkExecutionInput LinkInput;
    LinkInput.Kind = linker::LinkExecutionInputKind::Object;
    LinkInput.LogicalURI = "memory://native-only-replacement-hook.o";
    LinkInput.AuthorizedBlob = std::move(*Input);
    Request.Inputs.push_back(std::move(LinkInput));

    linker::LinkerDriverConfig Config;
    Config.pluginTask = &Scope.task();
    Config.relocatable = true;
    Config.androidKernelModule = true;
    Config.finalizeAndroidKernelModule = true;
    Config.stripMode = linker::StripMode::All;

    neverc::OutputCoordinator Outputs;
    auto SessionAlias = std::shared_ptr<PluginSession>(&Scope.session(),
                                                       [](PluginSession *) {});
    LinkExecutionHooksBridge Bridge(std::move(SessionAlias), Outputs);
    raw_null_ostream NullOutput;
    auto Result = Bridge.execute(Request, Config, NullOutput, NullOutput);
    ASSERT_FALSE(Result);
    const std::string Message = errorText(Result.takeError());
    EXPECT_NE(Message.find("native-image passthrough"), std::string::npos)
        << Message;
    EXPECT_NE(Message.find("incompatible"), std::string::npos) << Message;
    EXPECT_FALSE(sys::fs::exists(OutputPath));
  };

  RunRejected(NEVERC_TEST_OBJECT_PHASE_OBSERVER_INTERCEPTOR_PLUGIN,
              "neverc-native-only-observer-interceptor");
  RunRejected(NEVERC_TEST_OBJECT_PHASE_OBSERVER_PROVIDER_PLUGIN,
              "neverc-native-only-observer-provider");
}

TEST(PluginObjectMergeProviderTest,
     NestedReadOnlyCallbackCannotReuseExternalMergeMutationFacade) {
  LinkTaskScope Scope;
  ASSERT_TRUE(Scope.initialize(NEVERC_TEST_OBJECT_MERGE_PLUGIN,
                               NEVERC_TEST_OBJECT_POST_WRITE_PLUGIN));
  auto Input = makeObject(1);
  auto Target = makeTargetKey();
  ASSERT_NE(Input, nullptr);
  ASSERT_TRUE(static_cast<bool>(Target)) << errorText(Target.takeError());

  NestedMergeMutationState State;
  State.Task = &Scope.task();
  State.ObserverPluginID = Scope.plugin(1)->descriptor().PluginID;

  PluginLinkSnapshot::ObjectMergeProviderRecord Provider;
  Provider.PluginID = Scope.plugin(0)->descriptor().PluginID;
  Provider.Owner = Scope.plugin(0);
  Provider.ProviderID = "nested-merge-capability";
  Provider.TargetID = TestTargetID;
  Provider.FormatID = TestFormatID;
  Provider.ProductID = TestProductID;
  Provider.Merge = mergeAndAttemptMutationFromNestedObserver;
  Provider.UserData = &State;
  Provider.Builtin = false;
  PluginObjectGraph *InputPointer = Input.get();

  auto Merged = executeObjectMergeProvider(
      Scope.task(), Provider, std::move(*Target),
      ArrayRef<PluginObjectGraph *>(&InputPointer, 1));
  ASSERT_TRUE(static_cast<bool>(Merged)) << errorText(Merged.takeError());
  EXPECT_EQ(State.ObserverDispatch.Code, NEVERC_STATUS_OK);
  EXPECT_EQ(State.MutationAttempt.Code, NEVERC_STATUS_POLICY_VIOLATION);
}

} // namespace
