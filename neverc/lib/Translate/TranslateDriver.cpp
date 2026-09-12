#include "neverc/Translate/TranslateDriver.h"
#include "ArtifactWriter.h"
#include "Cpp/CompilationContext.h"
#include "Cpp/CppSdk.h"
#include "Cpp/LibraryMappings.h"
#include "Diagnostics.h"
#include "FrontendProcess.h"
#include "JSON.h"
#include "ProjectIR.h"
#include "TranslateIR.h"
#include "neverc/Foundation/Core/Version.h"
#include "neverc/Foundation/Diagnostic/Diagnostic.h"
#include "neverc/Foundation/LangOpts/LangOptions.h"
#include "neverc/Foundation/Target/TargetInfo.h"
#include "neverc/Foundation/Target/TargetOptions.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Process.h"
#include "llvm/Support/Program.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Host.h"
#include "llvm/TargetParser/Triple.h"
#include <map>
#include <set>

using namespace llvm;
namespace neverc::translate {
namespace {
constexpr size_t MaxProjectResponseBytes = 64u * 1024u * 1024u;
struct Invocation {
  std::string Source, Profile = "cpp-core-v1";
  std::vector<std::string> Sources, EntrySelectors;
  std::string Database, ProjectRoot, Target, Quoting;
  ArtifactOptions Artifacts;
  std::vector<std::string> Arguments;
  bool Help = false, Check = false, BuiltinStdEnabled = true;
};

bool fail(Diagnostics &D, StringRef Code, StringRef Source, StringRef Construct,
          StringRef Reason, StringRef Guidance) {
  D.push_back(driverDiagnostic(Code, Source, Construct, Reason, Guidance));
  return false;
}
// These are the complete target/layout-affecting validation options. Source
// frontend flags are never forwarded to the NC compiler. Keep the independent
// target model and the replayed validation command on this same configuration.
constexpr const char *ValidationStandard = "c23";
std::vector<std::string> validationOptions(llvm::StringRef Triple) {
  return {"--no-default-config", "--target=" + Triple.str(),
          std::string("-std=") + ValidationStandard};
}
bool expectedCarrierLayout(VerificationContext &Context, Diagnostics &D,
                           llvm::StringRef Source) {
  neverc::DiagnosticsEngine TargetDiags(new neverc::DiagnosticIDs(),
                                        new neverc::DiagnosticOptions(),
                                        new neverc::IgnoringDiagConsumer());
  auto Options = std::make_shared<neverc::TargetOptions>();
  Options->Triple = Context.TargetTriple;
  std::unique_ptr<neverc::TargetInfo> Target(
      neverc::TargetInfo::CreateTargetInfo(TargetDiags, Options));
  if (!Target || TargetDiags.hasErrorOccurred())
    return fail(D, "TR0204", Source, "NeverC target layout",
                "Cannot construct the requested NeverC target.",
                "Select an admitted native target.");
  neverc::LangOptions Lang;
  std::vector<std::string> Includes;
  neverc::LangOptions::setLangDefaults(
      Lang, neverc::Language::C, llvm::Triple(Context.TargetTriple), Includes,
      neverc::LangStandard::getLangKind(ValidationStandard));
  Target->adjust(TargetDiags, Lang);
  if (TargetDiags.hasErrorOccurred())
    return fail(D, "TR0204", Source, "NeverC target layout",
                "Cannot apply the NC validation language configuration.",
                "Select an admitted native target.");
  CarrierLayout Layout;
  Layout.CharBits = Target->getCharWidth();
  Layout.Carriers[0] = {Target->getBoolWidth(), Target->getBoolAlign()};
  Layout.Carriers[1] = Layout.Carriers[2] =
      {Target->getCharWidth(), Target->getCharAlign()};
  Layout.Carriers[3] = Layout.Carriers[4] =
      {Target->getShortWidth(), Target->getShortAlign()};
  Layout.Carriers[5] = Layout.Carriers[6] =
      {Target->getIntWidth(), Target->getIntAlign()};
  Layout.Carriers[7] = Layout.Carriers[8] =
      {Target->getLongLongWidth(), Target->getLongLongAlign()};
  Layout.Carriers[9] = {
      uint32_t(Target->getPointerWidth(neverc::LangAS::Default)),
      uint32_t(Target->getPointerAlign(neverc::LangAS::Default))};
  Context.IntBits = Target->getIntWidth();
  Context.PointerBits = Layout.Carriers[9].SizeBits;
  auto PtrDiff = Target->getPtrDiffType(neverc::LangAS::Default);
  Context.ExpectedPtrDiffBits = neverc::TargetInfo::isTypeSigned(PtrDiff)
                                    ? Target->getTypeWidth(PtrDiff)
                                    : 0;
  Context.LittleEndian = Target->isLittleEndian();
  Context.ExpectedCarrierLayout = Layout;
  return true;
}
json::Object layoutJSON(const StorageLayout &L) {
  return json::Object{{"size_bits", L.SizeBits},
                      {"abi_align_bits", L.ABIAlignBits}};
}
json::Object layoutJSON(const CarrierLayout &L) {
  json::Object O{{"char_bits", L.CharBits}};
  for (size_t I = 0; I < CarrierNames.size(); ++I)
    O[CarrierNames[I]] = layoutJSON(L.Carriers[I]);
  return O;
}
json::Object layoutJSON(const RecordLayout &L) {
  auto O = layoutJSON(L.Storage);
  json::Array Offsets;
  for (auto Bits : L.FieldOffsetsBits)
    Offsets.push_back(Bits);
  O["field_offsets_bits"] = std::move(Offsets);
  return O;
}
bool parseInvocation(int Argc, const char **Argv, Invocation &I,
                     Diagnostics &D) {
  std::string Language;
  std::set<std::string> Seen;
  auto error = [&](StringRef Why) {
    return fail(D, "TR0001", I.Source, "invocation", Why,
                "Run 'neverc translate --help' for the command contract.");
  };
  for (int N = 1; N < Argc; ++N) {
    StringRef A(Argv[N]);
    if (A == "--") {
      for (++N; N < Argc; ++N)
        I.Arguments.emplace_back(Argv[N]);
      break;
    }
    if (A == "--help" || A == "-h") {
      I.Help = true;
      continue;
    }
    if (A == "-fno-builtin-std") {
      if (!I.BuiltinStdEnabled)
        return error("duplicate -fno-builtin-std");
      I.BuiltinStdEnabled = false;
      continue;
    }
    if (A == "--check") {
      if (I.Check)
        return error("duplicate --check");
      I.Check = true;
      continue;
    }
    std::string *Value = nullptr;
    if (A == "--from")
      Value = &Language;
    else if (A == "--profile")
      Value = &I.Profile;
    else if (A == "--compdb")
      Value = &I.Database;
    else if (A == "--project-root")
      Value = &I.ProjectRoot;
    else if (A == "--target")
      Value = &I.Target;
    else if (A == "--compdb-quoting")
      Value = &I.Quoting;
    else if (A == "-o")
      Value = &I.Artifacts.Output;
    else if (A == "--out-dir")
      Value = &I.Artifacts.OutputDirectory;
    else if (A == "--report")
      Value = &I.Artifacts.Report;
    if (A == "--compdb-entry") {
      if (++N >= Argc || StringRef(Argv[N]).empty() ||
          StringRef(Argv[N]) == "--")
        return error("missing value for --compdb-entry");
      I.EntrySelectors.emplace_back(Argv[N]);
      continue;
    }
    if (Value) {
      if (!Seen.insert(A.str()).second)
        return error("duplicate option: " + A.str());
      if (++N >= Argc || StringRef(Argv[N]).empty() ||
          StringRef(Argv[N]) == "--")
        return error("missing value for " + A.str());
      *Value = Argv[N];
      continue;
    }
    if (A.starts_with("-"))
      return error("unknown option: " + A.str());
    I.Sources.push_back(A.str());
    if (I.Source.empty())
      I.Source = A.str();
  }
  if (I.Help)
    return true;
  if (Language.empty())
    return error("--from is required");
  if (Language != "cpp")
    return fail(D, "TR0002", I.Source, "language",
                "unsupported language: " + Language,
                "Only --from cpp is available.");
  if (I.Profile != "cpp-core-v1" && I.Profile != "cpp-core-v2" &&
      I.Profile != "cpp-project-v1" &&
      I.Profile != "cpp-math-v1")
    return fail(D, "TR0003", I.Source, "profile",
                "unsupported profile: " + I.Profile,
                "Use cpp-core-v1, cpp-core-v2, cpp-project-v1, or cpp-math-v1.");
  if (I.Profile == "cpp-math-v1") {
    if (I.Target.empty())
      return error("cpp-math-v1 requires an explicit --target");
  } else if (!I.BuiltinStdEnabled) {
    return error("-fno-builtin-std requires cpp-math-v1");
  }
  if (I.Source.empty())
    return error("a source file is required");
  unsigned Modes = I.Check + !I.Artifacts.Output.empty() +
                   !I.Artifacts.OutputDirectory.empty();
  if (Modes != 1)
    return error("choose exactly one of -o, --out-dir, or --check");
  if (!I.Artifacts.Output.empty() &&
      sys::path::extension(I.Artifacts.Output) != ".nc")
    return error("-o must name a .nc file");
  I.Artifacts.Project = I.Profile == "cpp-project-v1" ||
                        I.Profile == "cpp-math-v1";
  if (I.Artifacts.Project) {
    if (I.Database.empty() || I.ProjectRoot.empty())
      return error("project profiles require --compdb and --project-root");
    if (!I.Artifacts.Output.empty())
      return error("project profiles require --out-dir or --check");
    if (!I.Quoting.empty() && I.Quoting != "gnu" && I.Quoting != "windows")
      return error("--compdb-quoting must be gnu or windows");
    return true;
  }
  if (I.Sources.size() != 1)
    return error("core profiles accept exactly one source file");
  if (!I.Database.empty() || !I.ProjectRoot.empty() || !I.Target.empty() ||
      !I.Quoting.empty() || !I.EntrySelectors.empty())
    return error("project context options require an explicit project profile");
  std::vector<std::string> Normalized{"-std=c++17"};
  for (size_t N = 0; N < I.Arguments.size(); ++N) {
    StringRef A(I.Arguments[N]);
    if (A == "-std=c++17")
      continue;
    if (A == "-D" || A == "-U") {
      if (++N == I.Arguments.size() || I.Arguments[N].empty())
        return error("missing macro option value");
      Normalized.push_back(A.str() + I.Arguments[N]);
      continue;
    }
    if ((A.starts_with("-D") || A.starts_with("-U")) && A.size() > 2) {
      Normalized.push_back(A.str());
      continue;
    }
    return fail(D, "TR0004", I.Source, "source compiler option",
                "option is not in the selected core profile: " + A.str(),
                "Use -std=c++17 and supported -D/-U macros; "
                "target/layout/include options require another profile.");
  }
  I.Arguments = std::move(Normalized);
  return true;
}

void help() {
  outs()
      << "Usage: neverc translate --from cpp input.cpp "
         "(-o output.nc | --out-dir new-directory | --check) [options] -- "
         "[source options]\n\n"
         "Experimental C++17 scalar/aggregate translation.\n"
         "  --profile PROFILE     cpp-core-v1 (default), cpp-core-v2, "
         "cpp-project-v1, "
         "cpp-math-v1\n"
         "  --compdb PATH         Compilation database for selected project "
         "sources\n"
         "  --project-root PATH   Explicit ownership root (required for "
         "projects)\n"
         "  --target TRIPLE       Project target; defaults to the native "
         "target\n"
         "  --compdb-entry F=N    Select zero-based database entry N for "
         "source F\n"
         "  --compdb-quoting MODE gnu or windows; defaults to the host "
         "convention\n"
         "  -fno-builtin-std      Disable embedded std; math mappings then "
         "fail capability checks\n"
         "  --report PATH         Write a JSON validation report without "
         "overwriting\n"
         "  --check               Analyze, emit temporarily, syntax/object "
         "validate, discard\n"
         "  --help                Show this help\n\n"
         "Core source options: -std=c++17, supported -D/-U macros. No "
         "includes.\n"
         "Projects accept explicitly selected sources and owned headers; use "
         "--out-dir or --check.\n"
         "Output parents must exist; generated files and sidecars are never "
         "overwritten.\n"
         "C++ source analysis is built into this NeverC executable.\n"
         "Math requires an explicit macOS 15.0 target and approved runtime "
         "payloads. Its pinned C++ headers are built in.\n"
         "Translation does not execute source programs.\n";
}

std::string jsonText(json::Value V) {
  std::string Text;
  raw_string_ostream OS(Text);
  OS << formatv("{0:2}", V) << '\n';
  return Text;
}
json::Array strings(const std::vector<std::string> &Values) {
  json::Array Result;
  for (const auto &V : Values)
    Result.push_back(jsonString(V));
  return Result;
}
json::Array mappingJSON(const std::vector<MappingEvidence> &Mappings) {
  json::Array Result;
  for (const auto &Mapping : Mappings) {
    json::Array Parameters;
    for (const auto &Type : Mapping.Parameters)
      Parameters.push_back(jsonString(typeName(Type)));
    Result.push_back(json::Object{
        {"id", jsonString(Mapping.ID)},
        {"declaration_id", jsonString(Mapping.DeclarationID)},
        {"result", jsonString(typeName(Mapping.Result))},
        {"parameters", std::move(Parameters)},
        {"origin", json::Object{{"root", jsonString(Mapping.Origin.Root)},
                                {"path", jsonString(Mapping.Origin.Path)},
                                {"sha256", jsonString(Mapping.Origin.SHA256)},
                                {"line", Mapping.Origin.Line},
                                {"column", Mapping.Origin.Column}}}});
  }
  return Result;
}
json::Array
sdkDependenciesJSON(const std::vector<SDKDependency> &Dependencies) {
  json::Array Result;
  for (const auto &Dep : Dependencies)
    Result.push_back(json::Object{{"root", jsonString(Dep.Root)},
                                  {"path", jsonString(Dep.Path)},
                                  {"sha256", jsonString(Dep.SHA256)}});
  return Result;
}
std::string report(StringRef Profile, StringRef Stage, bool Success,
                   const Diagnostics &D,
                   const std::vector<MappingEvidence> &Mappings = {}) {
  json::Array Items;
  for (const auto &Item : D)
    Items.push_back(diagnosticJSON(Item));
  return jsonText(json::Object{{"schema", "neverc.translate.report"},
                               {"version", 1},
                               {"profile", jsonString(Profile)},
                               {"stage", jsonString(Stage)},
                               {"status", Success ? "success" : "failed"},
                               {"diagnostics", std::move(Items)},
                               {"mappings", mappingJSON(Mappings)}});
}
bool recordError(Error E, Diagnostics &D, StringRef Source,
                 StringRef Construct) {
  std::string Text = toString(std::move(E)).str().str();
  StringRef Message(Text), Code = "TR0502";
  if (Message.starts_with("TR") && Message.size() > 7 && Message[6] == ':') {
    Code = Message.take_front(6);
    Message = Message.drop_front(7).ltrim();
  }
  return fail(D, Code, Source, Construct, Message,
              "Choose unused output paths with existing parents and check the "
              "reported file error.");
}
Expected<std::string> executable(StringRef Path) {
  if (Path.empty())
    return createStringError(
        inconvertibleErrorCode(),
        "cannot determine the running NeverC executable path");
  auto Found = sys::findProgramByName(Path);
  if (!Found)
    return errorCodeToError(Found.getError());
  SmallString<256> Real;
  if (auto EC = sys::fs::real_path(*Found, Real))
    return errorCodeToError(EC);
  if (!sys::fs::can_execute(Real))
    return createStringError(inconvertibleErrorCode(),
                             "file is not executable");
  return Real.str().str();
}

bool frontendDiagnostics(StringRef Text, Diagnostics &D) {
  if (!jsonWithinLimits(Text, MaxFrontendResponseBytes, MaxProtocolDepth))
    return false;
  auto V = json::parse(Text);
  if (!V) {
    consumeError(V.takeError());
    return false;
  }
  const auto *O = V->getAsObject();
  int64_t Protocol = 0;
  if (!O || !O->getInteger("protocol", Protocol) ||
      Protocol != FrontendProtocolMajor)
    return false;
  const auto *Items = O->getArray("diagnostics");
  if (!Items || Items->empty())
    return false;
  Diagnostics Parsed;
  for (const auto &Item : *Items) {
    const auto *Diag = Item.getAsObject();
    if (!Diag)
      return false;
    auto Code = Diag->getString("code"), File = Diag->getString("file"),
         Construct = Diag->getString("construct"),
         Reason = Diag->getString("reason"),
         Guidance = Diag->getString("guidance");
    int64_t Line = 0, Column = 0;
    if (!Code.data() || !File.data() || !Construct.data() || !Reason.data() ||
        !Guidance.data() || !Diag->getInteger("line", Line) ||
        !Diag->getInteger("column", Column) || Line < 1 || Column < 1 ||
        Line > UINT32_MAX || Column > UINT32_MAX)
      return false;
    Parsed.push_back({Code.str(),
                      {File.str(), static_cast<uint32_t>(Line),
                       static_cast<uint32_t>(Column)},
                      Construct.str(),
                      Reason.str(),
                      Guidance.str()});
  }
  D.insert(D.end(), Parsed.begin(), Parsed.end());
  return true;
}

bool projectOptions(const Invocation &I, StringRef Target,
                    ProjectContextOptions &Options, Diagnostics &D) {
  Options.Database = I.Database;
  Options.ProjectRoot = I.ProjectRoot;
  Options.TargetTriple = Target.str();
#ifdef _WIN32
  Options.Quoting = CommandQuoting::Windows;
#else
  Options.Quoting = CommandQuoting::GNU;
#endif
  if (!I.Quoting.empty())
    Options.Quoting =
        I.Quoting == "windows" ? CommandQuoting::Windows : CommandQuoting::GNU;
  Options.ExtraSourceArguments = I.Arguments;
  std::map<std::string, size_t> Selected;
  for (const auto &Path : I.Sources) {
    SmallString<256> Real;
    if (auto EC = sys::fs::real_path(Path, Real))
      return fail(D, "TR0203", Path, "selected source", EC.message(),
                  "Provide each selected source as an existing file.");
    if (!Selected.emplace(Real.str().str(), Options.Selections.size()).second)
      return fail(D, "TR0001", Path, "source selection",
                  "the same source was selected more than once",
                  "Select one configuration per source in each translation.");
    Options.Selections.push_back({Real.str().str(), std::nullopt});
  }
  for (const auto &Selector : I.EntrySelectors) {
    auto Pair = StringRef(Selector).rsplit('=');
    uint32_t Index = 0;
    if (Pair.first.empty() || Pair.second.empty() ||
        Pair.second.getAsInteger(10, Index))
      return fail(D, "TR0001", I.Source, "configuration selector",
                  "expected FILE=INDEX: " + Selector,
                  "Use a selected source path and a zero-based compilation "
                  "database index.");
    SmallString<256> Real;
    if (auto EC = sys::fs::real_path(Pair.first, Real))
      return fail(D, "TR0001", Pair.first, "configuration selector",
                  EC.message(), "Use the path of a selected existing source.");
    auto Found = Selected.find(Real.str().str());
    if (Found == Selected.end() || Options.Selections[Found->second].EntryIndex)
      return fail(
          D, "TR0001", Pair.first, "configuration selector",
          "selector is duplicated or refers to an unselected source",
          "Give at most one selector for each explicitly selected source.");
    Options.Selections[Found->second].EntryIndex = Index;
  }
  return true;
}

json::Object contextInputJSON(const ContextInput &Input) {
  return json::Object{{"path", jsonString(Input.RelativePath)},
                      {"sha256", jsonString(Input.SHA256)}};
}

std::optional<SourceLocation>
mappedDiagnostic(StringRef Message, StringRef GeneratedPath,
                 const std::vector<SourceMapEntry> &Map) {
  std::string Prefix = GeneratedPath.str() + ":";
  auto Position = Message.find(Prefix);
  if (Position == StringRef::npos)
    return std::nullopt;
  StringRef Rest = Message.drop_front(Position + Prefix.size());
  uint32_t Line = 0;
  if (Rest.split(':').first.getAsInteger(10, Line))
    return std::nullopt;
  std::optional<SourceLocation> Result;
  uint32_t SmallestSpan = UINT32_MAX;
  for (const auto &Entry : Map)
    if (Entry.BeginLine <= Line && Line <= Entry.EndLine &&
        Entry.EndLine - Entry.BeginLine <= SmallestSpan) {
      SmallestSpan = Entry.EndLine - Entry.BeginLine;
      Result = Entry.Original;
    }
  return Result;
}

std::string manifest(const Invocation &I, const Module &M,
                     const ArtifactWriter &Writer, const EmittedSource &E,
                     StringRef MapText, const ProjectContext *Project = nullptr,
                     StringRef Header = {}, const CppSdkContext *SDK = nullptr,
                     const MathRuntimeCapabilities *Capabilities = nullptr) {
  json::Array Dependencies, Exports, Files;
  for (const auto &Dep : M.Dependencies)
    Dependencies.push_back(json::Object{{"path", jsonString(Dep.Path)},
                                        {"sha256", jsonString(Dep.SHA256)}});
  for (const auto &Export : M.Exports)
    Exports.push_back(json::Object{{"name", jsonString(Export.Name)},
                                   {"result", jsonString(Export.Result)},
                                   {"parameters", strings(Export.Parameters)},
                                   {"c_export", Export.CExport}});
  Files.push_back(json::Object{{"path", jsonString(Writer.sourceName())},
                               {"sha256", jsonString(sha256(E.Text))}});
  Files.push_back(json::Object{{"path", jsonString(Writer.mapName())},
                               {"sha256", jsonString(sha256(MapText))}});
  if (!Header.empty())
    Files.push_back(json::Object{{"path", jsonString(Writer.headerName())},
                                 {"sha256", jsonString(sha256(Header))}});
  std::vector<std::string> Recipe{"neverc"};
  auto ValidationArgs = validationOptions(M.Target.Triple);
  Recipe.insert(Recipe.end(), ValidationArgs.begin(), ValidationArgs.end());
  Recipe.insert(Recipe.end(), {Writer.sourceName().str(), "-c", "-o", "module.o"});
  json::Object Manifest{
      {"schema", "neverc.translate.manifest"},
      {"version", 1},
      {"protocol", FrontendProtocolMajor},
      {"profile", jsonString(I.Profile)},
      {"profile_version", I.Profile == "cpp-core-v2" ? 2 : 1},
      {"source_options", Project ? json::Array{} : strings(I.Arguments)},
      {"compiler_environment_policy", "neverc.translate.execution-env.v1"},
      {"compiler_options", strings(ValidationArgs)},
      {"frontend", json::Object{{"name", jsonString(M.Frontend.Name)},
                                {"version", jsonString(M.Frontend.Version)},
                                {"build", jsonString(M.Frontend.Build)}}},
      {"neverc", json::Object{{"version", jsonString(getNeverCFullVersion())},
                              {"build", NEVERC_TRANSLATE_BUILD_ID}}},
      {"target", json::Object{{"triple", jsonString(M.Target.Triple)},
                              {"int_bits", M.Target.IntBits},
                              {"pointer_bits", M.Target.PointerBits},
                              {"little_endian", M.Target.LittleEndian}}},
      {"dependencies", std::move(Dependencies)},
      {"exports", std::move(Exports)},
      {"generated_files", std::move(Files)},
      {"mappings", mappingJSON(M.Mappings)},
      {"required_headers", json::Array{}},
      {"required_modules", json::Array{}},
      {"compilation_recipe", strings(Recipe)}};
  if (M.Target.Carriers) {
    (*Manifest.getObject("target"))["carrier_layout"] =
        layoutJSON(*M.Target.Carriers);
    json::Array Records;
    for (const auto &R : M.Records) {
      auto Layout = layoutJSON(*R.Layout);
      Layout["id"] = jsonString(R.ID);
      Records.push_back(std::move(Layout));
    }
    Manifest["record_layouts"] = std::move(Records);
  }
  if (Project) {
    json::Array Units;
    for (const auto &Unit : Project->Units) {
      json::Array Responses;
      for (const auto &Input : Unit.ResponseFiles)
        Responses.push_back(contextInputJSON(Input));
      Units.push_back(json::Object{
          {"source", jsonString(Unit.SourceRelative)},
          {"working_directory", jsonString(Unit.WorkingDirectoryRelative)},
          {"entry_index", Unit.EntryIndex},
          {"configuration_id", jsonString(Unit.ConfigurationID)},
          {"source_options", strings(Unit.NormalizedArguments)},
          {"response_files", std::move(Responses)}});
    }
    Manifest["compilation_database"] = contextInputJSON(Project->Database);
    Manifest["translation_units"] = std::move(Units);
    Manifest["required_headers"] = json::Array{jsonString(Writer.headerName())};
  }
  if (SDK && Capabilities) {
    Manifest["fp_contract"] = jsonString(M.FPContractID);
    Manifest["sdk"] =
        json::Object{{"distribution_id", jsonString(SDK->DistributionID)},
                     {"catalog_sha256", jsonString(SDK->CatalogSHA256)},
                     {"delivery", "builtin"},
                     {"dependencies", sdkDependenciesJSON(M.SDKDependencies)}};
    json::Array Modules, RequiredModules;
    for (const auto &Module : Capabilities->Modules) {
      RequiredModules.push_back(jsonString(Module.Name));
      Modules.push_back(
          json::Object{{"name", jsonString(Module.Name)},
                       {"sha256", jsonString(Module.SHA256)},
                       {"implementation_identity",
                        jsonString(Module.ImplementationIdentity)},
                       {"defined_symbols", strings(Module.DefinedSymbols)},
                       {"required_symbols", strings(Module.RequiredSymbols)}});
    }
    Manifest["runtime_capabilities"] =
        json::Object{{"id", jsonString(Capabilities->ID)},
                     {"fingerprint_policy", MathRuntimeFingerprintPolicy},
                     {"target", jsonString(Capabilities->Target)},
                     {"header", jsonString(Capabilities->HeaderRelativePath)},
                     {"header_sha256", jsonString(Capabilities->HeaderSHA256)},
                     {"mapping_ids", strings(Capabilities->MappingIDs)},
                     {"modules", std::move(Modules)}};
    Manifest["required_modules"] = std::move(RequiredModules);
    json::Array Headers{jsonString(Writer.headerName())};
    if (!Capabilities->HeaderRelativePath.empty())
      Headers.push_back(jsonString(Capabilities->HeaderRelativePath));
    Manifest["required_headers"] = std::move(Headers);
    Manifest["builtin_std_enabled"] = I.BuiltinStdEnabled;
    if (!I.BuiltinStdEnabled) {
      Recipe.push_back("-fno-builtin-std");
      Manifest["compilation_recipe"] = strings(Recipe);
      Manifest["compiler_options"] = json::Array{
          "--no-default-config", jsonString("--target=" + M.Target.Triple),
          "-std=c23", "-fno-builtin-std"};
    }
  }
  return jsonText(std::move(Manifest));
}
} // namespace

int runTranslate(int Argc, const char **Argv, const char *ExecutablePath) {
  Diagnostics D;
  Invocation I;
  if (!parseInvocation(Argc, Argv, I, D)) {
    printDiagnostics(D);
    return 1;
  }
  if (I.Help) {
    help();
    return 0;
  }
  TranslationCancellation Cancellation;
  SmallString<256> Source;
  if (auto EC = sys::fs::real_path(I.Source, Source)) {
    fail(D, I.Artifacts.Project ? "TR0203" : "TR0202", I.Source, "source file",
         EC.message(), "Provide an existing C++ source file.");
    printDiagnostics(D);
    return 1;
  }
  I.Source = Source.str().str();
  auto WriterResult = ArtifactWriter::create(I.Artifacts, I.Source);
  if (!WriterResult) {
    recordError(WriterResult.takeError(), D, I.Source, "output preflight");
    printDiagnostics(D);
    return 1;
  }
  auto Writer = std::move(*WriterResult);
  std::string Stage = "frontend";
  auto failed = [&]() {
    for (auto &Item : D)
      if (Item.Location.File == "<frontend>")
        Item.Location.File = sys::path::filename(I.Source).str();
    if (Error E =
            Writer->publishFailureReport(report(I.Profile, Stage, false, D)))
      recordError(std::move(E), D, I.Source, "failure report");
    printDiagnostics(D);
    return 1;
  };
  auto cancelled = [&]() {
    if (!translationCancelled())
      return false;
    fail(D, "TR0005", sys::path::filename(I.Source), "translation cancellation",
         "translation was cancelled",
         "Retry when ready; incomplete generated artifacts are removed.");
    return true;
  };
  if (cancelled())
    return failed();
  auto Compiler = executable(ExecutablePath);
  if (!Compiler) {
    fail(D, "TR0402", I.Source, "NeverC executable",
         toString(Compiler.takeError()),
         "Run the installed NeverC executable.");
    return failed();
  }
  VerificationContext Context;
  Context.Profile = I.Profile;
  Context.TargetTriple = Triple::normalize(
      I.Target.empty() ? sys::getDefaultTargetTriple() : I.Target);
  Triple T(Context.TargetTriple);
  Context.PointerBits = T.isArch64Bit() ? 64 : 32;
  Context.LittleEndian = T.isLittleEndian();
  if (I.Profile == "cpp-core-v2" &&
      !expectedCarrierLayout(Context, D, I.Source))
    return failed();
  std::string Root = sys::path::parent_path(I.Source).str();
  ProjectContext Project;
  std::vector<TranslationUnitContext> Jobs;
  if (I.Artifacts.Project) {
    Stage = "context";
    ProjectContextOptions Options;
    if (!projectOptions(I, Context.TargetTriple, Options, D) ||
        !parseProjectContext(Options, Project, D))
      return failed();
    Root = Project.ProjectRootAbsolute;
    Jobs = Project.Units;
  } else {
    TranslationUnitContext Unit;
    Unit.SourceAbsolute = I.Source;
    Unit.SourceRelative = sys::path::filename(I.Source).str();
    Unit.WorkingDirectoryAbsolute = Root;
    Unit.FrontendArguments = I.Arguments;
    Jobs.push_back(std::move(Unit));
  }
  const bool Math = I.Profile == "cpp-math-v1";
  CppSdkContext SDK;
  MathRuntimeCapabilities Capabilities;
  std::string ResourceDirectory;
  if (Math) {
    Stage = "sdk";
    if (!loadBuiltinCppSdk(Context.TargetTriple, SDK, D))
      return failed();
    Context.FPContractID = CppMathFPContractID;
    Context.ApprovedSDKIDs = {SDK.DistributionID};
    Stage = "runtime";
    auto R =
        runProcess({*Compiler, "--no-default-config", "-print-resource-dir"},
                   Writer->stagePath("resource.stdout"),
                   Writer->stagePath("resource.stderr"));
    if (cancelled())
      return failed();
    auto Resource = readFile(Writer->stagePath("resource.stdout"), 64 * 1024);
    if (R.ExitCode || !Resource || StringRef(*Resource).trim().empty()) {
      if (!Resource)
        consumeError(Resource.takeError());
      fail(D, "TR0403", I.Source, "NeverC resource directory",
           "the validation compiler could not identify its installed resources",
           "Use an installed NeverC compiler with its resource headers.");
      return failed();
    }
    ResourceDirectory = StringRef(*Resource).trim().str();
  }
  std::map<std::string, std::string> DependencyHashes;
  auto verifyDependencies = [&](const Module &Module) {
    for (const auto &Dep : Module.Dependencies) {
      auto Previous = DependencyHashes.emplace(Dep.Path, Dep.SHA256);
      if (!Previous.second && Previous.first->second != Dep.SHA256)
        return fail(D, "TR0103", Dep.Path, "dependency hash",
                    "input changed between translation units",
                    "Retry translation with stable declared inputs.");
      SmallString<256> Path(Root), Real;
      sys::path::append(Path, Dep.Path);
      auto EC = sys::fs::real_path(Path, Real);
      bool Inside = Real.str().starts_with(Root) &&
                    (sys::path::is_separator(Root.back()) ||
                     (Real.size() > Root.size() &&
                      sys::path::is_separator(Real[Root.size()])));
      if (EC || !Inside)
        return fail(D, "TR0203", Dep.Path, "owned dependency",
                    "dependency is missing or no longer belongs to the "
                    "declared project root",
                    "Keep owned inputs stable under the explicit root.");
      auto Contents = readFile(Real);
      if (!Contents || sha256(*Contents) != Dep.SHA256) {
        if (!Contents)
          consumeError(Contents.takeError());
        return fail(D, "TR0103", Dep.Path, "dependency hash",
                    "input changed during source analysis",
                    "Retry translation with stable declared inputs.");
      }
    }
    return true;
  };
  Module M;
  std::vector<ProjectUnit> Units;
  size_t ResponseBytes = 0;
  for (size_t Index = 0; Index < Jobs.size(); ++Index) {
    if (cancelled())
      return failed();
    const auto &Job = Jobs[Index];
    Stage = "frontend";
    auto Input = readFile(Job.SourceAbsolute);
    if (!Input) {
      recordError(Input.takeError(), D, Job.SourceRelative, "source file");
      return failed();
    }
    json::Object Request{{"protocol", FrontendProtocolMajor},
                         {"profile", jsonString(I.Profile)},
                         {"root", jsonString(Root)},
                         {"source", jsonString(Job.SourceAbsolute)},
                         {"target", jsonString(Context.TargetTriple)},
                         {"arguments", strings(Job.FrontendArguments)}};
    if (I.Artifacts.Project) {
      Request["translation_unit"] = jsonString(Job.SourceRelative);
      Request["configuration_id"] = jsonString(Job.ConfigurationID);
      Request["working_directory"] = jsonString(Job.WorkingDirectoryAbsolute);
    }
    if (Math)
      Request["sdk"] = cppSdkRequestJSON(SDK);
    const std::string Prefix =
        I.Artifacts.Project ? "unit-" + std::to_string(Index) + "-" : "";
    if (Error E = Writer->writeStage(Prefix + "request.json",
                                     jsonText(std::move(Request)))) {
      recordError(std::move(E), D, Job.SourceRelative, "frontend request");
      return failed();
    }
    auto R = runProcess({*Compiler, CppFrontendCommand, "--request",
                         Writer->stagePath(Prefix + "request.json"), "--output",
                         Writer->stagePath(Prefix + "response.json")},
                        Writer->stagePath(Prefix + "frontend.stdout"),
                        Writer->stagePath(Prefix + "frontend.stderr"));
    if (cancelled())
      return failed();
    auto Response = readFile(Writer->stagePath(Prefix + "response.json"),
                             MaxFrontendResponseBytes);
    if (R.ExitCode != 0) {
      bool HadDiagnostics = false;
      if (Response)
        HadDiagnostics = frontendDiagnostics(*Response, D);
      else
        consumeError(Response.takeError());
      if (!HadDiagnostics)
        fail(D, "TR0102", Job.SourceRelative, "C++ frontend process",
             R.Error.empty() ? "built-in frontend failed with exit code " +
                                   std::to_string(R.ExitCode)
                             : R.Error,
             "Check the NeverC installation and supported source input.");
      return failed();
    }
    if (!Response) {
      fail(D, "TR0103", Job.SourceRelative, "frontend response",
           toString(Response.takeError()),
           "The built-in frontend must return a bounded protocol-v1 response.");
      return failed();
    }
    ResponseBytes += Response->size();
    if (ResponseBytes > MaxProjectResponseBytes) {
      fail(D, "TR0103", Job.SourceRelative, "project response size",
           "combined frontend responses exceed the 64 MiB project limit",
           "Translate a smaller explicitly selected project.");
      return failed();
    }
    Stage = "protocol";
    Module *Definitions = &M;
    if (I.Artifacts.Project) {
      Units.emplace_back();
      auto &Unit = Units.back();
      if (!parseProjectUnit(*Response, Unit, D))
        return failed();
      if (Unit.TranslationUnit != Job.SourceRelative ||
          Unit.ConfigurationID != Job.ConfigurationID) {
        fail(D, "TR0103", Job.SourceRelative, "frontend context",
             "response does not match the selected translation unit and "
             "configuration",
             "Use a consistent NeverC installation with its built-in C++ "
             "frontend.");
        return failed();
      }
      Definitions = &Unit.Definitions;
      if (Math) {
        Stage = "sdk";
        if (Definitions->SDKDistributionID != SDK.DistributionID ||
            Definitions->SDKCatalogSHA256 != SDK.CatalogSHA256) {
          fail(D, "TR0103", Job.SourceRelative, "SDK identity",
               "built-in frontend SDK identity does not match the approved "
               "request",
               "Use the matching built-in frontend and SDK.");
          return failed();
        }
        if (!verifyCppSdkDependencies(SDK, Definitions->SDKDependencies, D) ||
            !verifyCppSdkMappings(SDK, Definitions->Mappings, D))
          return failed();
        std::set<std::string> IDs(Context.ApprovedMappingIDs.begin(),
                                  Context.ApprovedMappingIDs.end());
        for (const auto &Mapping : Definitions->Mappings)
          IDs.insert(Mapping.ID);
        std::vector<std::string> Required(IDs.begin(), IDs.end());
        Stage = "runtime";
        if (!inspectMathRuntime(Context.TargetTriple, ResourceDirectory,
                                Required, I.BuiltinStdEnabled, Capabilities, D))
          return failed();
        Context.ApprovedMappingIDs = Capabilities.MappingIDs;
      }
      Stage = "ir";
      if (!verifyProjectUnit(Unit, Context, D))
        return failed();
    } else {
      if (!parseModule(*Response, M, D))
        return failed();
      Stage = "ir";
      if (!verifyModule(M, Context, D))
        return failed();
    }
    if (cancelled() || !verifyDependencies(*Definitions))
      return failed();
    bool FoundSource = false;
    for (const auto &Dep : Definitions->Dependencies)
      if (Dep.Path == Job.SourceRelative && Dep.SHA256 == sha256(*Input))
        FoundSource = true;
    if (!FoundSource) {
      fail(D, "TR0103", Job.SourceRelative, "source dependency",
           "built-in frontend did not record the selected source hash",
           "Use a consistent NeverC installation with its built-in C++ "
           "frontend.");
      return failed();
    }
  }
  EmittedSource Emitted;
  std::string Header;
  std::vector<SourceMapEntry> HeaderMap;
  if (I.Artifacts.Project) {
    MergedProject Merged;
    EmittedProject Output;
    if (!mergeProjectUnits(Units, Context, Merged, D) ||
        !emitProject(Merged, Context, Output, D))
      return failed();
    M = std::move(Merged.Definitions);
    for (auto &File : Output.Files) {
      if (File.RelativePath == Writer->sourceName()) {
        Emitted.Text = std::move(File.Text);
        Emitted.Map = std::move(File.Map);
      } else if (File.RelativePath == Writer->headerName()) {
        Header = std::move(File.Text);
        HeaderMap = std::move(File.Map);
      }
    }
    if (Emitted.Text.empty() || Header.empty()) {
      fail(D, "TR0301", I.Source, "project emission",
           "project source or header is missing",
           "Use a compatible project emitter.");
      return failed();
    }
    if (Error E = Writer->writeStage(Writer->headerName(), Header)) {
      recordError(std::move(E), D, I.Source, "temporary header");
      return failed();
    }
  } else if (!emitNC(M, Context, Emitted, D))
    return failed();
  if (cancelled())
    return failed();
  if (Error E = Writer->writeStage("validation.nc", Emitted.Text)) {
    recordError(std::move(E), D, I.Source, "temporary source");
    return failed();
  }
  for (bool Syntax : {true, false}) {
    Stage = Syntax ? "syntax" : "object";
    std::vector<std::string> Args{*Compiler};
    auto ValidationArgs = validationOptions(Context.TargetTriple);
    Args.insert(Args.end(), ValidationArgs.begin(), ValidationArgs.end());
    Args.push_back(Writer->stagePath("validation.nc"));
    if (!I.BuiltinStdEnabled)
      Args.push_back("-fno-builtin-std");
    if (Syntax)
      Args.push_back("-fsyntax-only");
    else {
      Args.push_back("-c");
      Args.push_back("-o");
      Args.push_back(Writer->stagePath("validation.o"));
    }
    auto Validation = runProcess(Args, Writer->stagePath(Stage + ".stdout"),
                                 Writer->stagePath(Stage + ".stderr"));
    if (cancelled())
      return failed();
    if (Validation.ExitCode != 0) {
      auto Detail = readFile(Writer->stagePath(Stage + ".stderr"), 1024 * 1024);
      std::string Message = Validation.Error;
      if (Detail)
        Message += *Detail;
      else
        consumeError(Detail.takeError());
      fail(D, Syntax ? "TR0401" : "TR0402", sys::path::filename(I.Source),
           Syntax ? "generated NC syntax" : "generated NC object code", Message,
           "Generated source failed NeverC validation; no translation "
           "artifacts were published.");
      auto Original = mappedDiagnostic(
          Message, Writer->stagePath("validation.nc"), Emitted.Map);
      if (!Original && !Header.empty())
        Original = mappedDiagnostic(
            Message, Writer->stagePath(Writer->headerName()), HeaderMap);
      if (Original)
        D.back().Location = *Original;
      return failed();
    }
  }
  if (Math && !Capabilities.MappingIDs.empty()) {
    Stage = "link";
    // Link only a generated capability probe. Never link or execute an owned
    // program here: modules need not define main and translation runs no code.
    std::string Probe =
        "#include <neverc/std/math.h>\nint main(int argc, char **argv) {\n"
        "  double value = (double)argc;\n";
    for (const auto &ID : Capabilities.MappingIDs) {
      const auto *Spec = findMappingSpec(ID);
      Probe += "  value = " + std::string(Spec->RuntimeSymbol) + "(value);\n";
    }
    Probe += "  return value == 0.0;\n}\n";
    if (Error E = Writer->writeStage("runtime-probe.nc", Probe)) {
      recordError(std::move(E), D, I.Source, "runtime link probe");
      return failed();
    }
    auto R = runProcess(
        {*Compiler, "--no-default-config", "--target=" + Context.TargetTriple,
         "-std=c23", "-O0", Writer->stagePath("runtime-probe.nc"), "-o",
         Writer->stagePath("runtime-probe")},
        Writer->stagePath("link.stdout"), Writer->stagePath("link.stderr"));
    if (cancelled())
      return failed();
    if (R.ExitCode) {
      auto Detail = readFile(Writer->stagePath("link.stderr"), 1024 * 1024);
      std::string Why = R.Error;
      if (Detail)
        Why += *Detail;
      else
        consumeError(Detail.takeError());
      fail(D, "TR0404", I.Source, "runtime link probe", Why,
           "Install usable runtime payloads and target libraries; no artifacts "
           "were published.");
      return failed();
    }
  }
  auto mapEntries = [&](const std::vector<SourceMapEntry> &Entries) {
    json::Array Result;
    for (const auto &Entry : Entries)
      Result.push_back(json::Object{{"generated_start_line", Entry.BeginLine},
                                    {"generated_end_line", Entry.EndLine},
                                    {"file", jsonString(Entry.Original.File)},
                                    {"line", Entry.Original.Line},
                                    {"column", Entry.Original.Column}});
    return Result;
  };
  json::Object MapObject{{"schema", "neverc.translate.source-map"},
                         {"version", 1},
                         {"generated_file", jsonString(Writer->sourceName())},
                         {"generated_sha256", jsonString(sha256(Emitted.Text))},
                         {"entries", mapEntries(Emitted.Map)}};
  if (!Header.empty())
    MapObject["additional_files"] = json::Array{
        json::Object{{"generated_file", jsonString(Writer->headerName())},
                     {"generated_sha256", jsonString(sha256(Header))},
                     {"entries", mapEntries(HeaderMap)}}};
  auto MapText = jsonText(std::move(MapObject));
  Stage = "context";
  if (!verifyDependencies(M))
    return failed();
  if (I.Artifacts.Project && !verifyProjectContextInputs(Project, D))
    return failed();
  if (Math) {
    Stage = "sdk";
    if (!verifyCppSdkDependencies(SDK, M.SDKDependencies, D) ||
        !verifyCppSdkMappings(SDK, M.Mappings, D))
      return failed();
    Stage = "runtime";
    if (!inspectMathRuntime(Context.TargetTriple, ResourceDirectory,
                            Context.ApprovedMappingIDs, I.BuiltinStdEnabled,
                            Capabilities, D))
      return failed();
  }
  auto Manifest = manifest(
      I, M, *Writer, Emitted, MapText, I.Artifacts.Project ? &Project : nullptr,
      Header, Math ? &SDK : nullptr, Math ? &Capabilities : nullptr);
  Stage = "publication";
  std::vector<Artifact> Artifacts{{Writer->sourceName().str(), Emitted.Text}};
  if (!Header.empty())
    Artifacts.push_back({Writer->headerName().str(), Header});
  Artifacts.push_back({Writer->mapName().str(), MapText});
  Artifacts.push_back({Writer->manifestName().str(), Manifest});
  if (Error E = Writer->publish(
          Artifacts, report(I.Profile, "complete", true, D, M.Mappings),
          translationCancelled)) {
    recordError(std::move(E), D, I.Source, "artifact publication");
    return failed();
  }
  outs() << "neverc translate: " << (I.Check ? "checked " : "translated ")
         << (I.Artifacts.Project
                 ? std::to_string(Jobs.size()) + " translation units"
                 : sys::path::filename(I.Source).str())
         << " (" << I.Profile << ")\n";
  return 0;
}
} // namespace neverc::translate
