#include "LinkInputReaderInternal.h"
#include "LinkerScriptProvider.h"
#include "neverc/Plugin/Host/PluginTaskContext.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/SHA256.h"
#include <cctype>
#include <cstring>

using namespace llvm;

namespace neverc::plugin {
namespace {

std::string unquote(StringRef Value) {
  Value = Value.trim();
  if (Value.size() >= 2 &&
      ((Value.front() == '"' && Value.back() == '"') ||
       (Value.front() == '\'' && Value.back() == '\'')))
    Value = Value.drop_front().drop_back();
  return Value.str();
}

std::vector<StringRef> splitArguments(StringRef Text) {
  std::vector<StringRef> Result;
  size_t Start = 0;
  unsigned Depth = 0;
  char Quote = 0;
  for (size_t I = 0; I != Text.size(); ++I) {
    const char C = Text[I];
    if (Quote) {
      if (C == '\\')
        ++I;
      else if (C == Quote)
        Quote = 0;
      continue;
    }
    if (C == '"' || C == '\'') {
      Quote = C;
      continue;
    }
    if (C == '(')
      ++Depth;
    else if (C == ')' && Depth)
      --Depth;
    else if (Depth == 0 &&
             (C == ',' || std::isspace(static_cast<unsigned char>(C)))) {
      StringRef Value = Text.slice(Start, I).trim();
      if (!Value.empty())
        Result.push_back(Value);
      Start = I + 1;
    }
  }
  StringRef Value = Text.drop_front(Start).trim();
  if (!Value.empty())
    Result.push_back(Value);
  return Result;
}

class ScriptParser {
public:
  ScriptParser(StringRef URIValue, StringRef TextValue)
      : URI(URIValue), Text(TextValue) {}

  Expected<LinkerScriptResult> parse() {
    LinkerScriptResult Result;
    Result.ProviderRoute = "builtin.gnu-linker-script";
    while (true) {
      if (Error E = skipTrivia())
        return std::move(E);
      if (Position == Text.size())
        return Result;
      if (!isIdentifierStart(Text[Position])) {
        ++Position;
        continue;
      }

      const std::string Name = readIdentifier();
      if (Error E = skipTrivia())
        return std::move(E);
      if (Position == Text.size())
        continue;

      if (Text[Position] == '(') {
        auto Body = balanced('(', ')');
        if (!Body)
          return Body.takeError();
        handleCall(Name, *Body, Result);
        continue;
      }
      if (Text[Position] == '{') {
        auto Body = balanced('{', '}');
        if (!Body)
          return Body.takeError();
        LinkerScriptLayoutConstraint Constraint;
        Constraint.Kind = StringRef(Name).lower().str().str();
        Constraint.Expression = Body->trim().str();
        Result.LayoutConstraints.push_back(std::move(Constraint));
        continue;
      }

      if (StringRef(Name).equals_insensitive("INCLUDE")) {
        size_t Start = Position;
        while (Position != Text.size() && Text[Position] != '\n' &&
               Text[Position] != ';')
          ++Position;
        StringRef Included = Text.slice(Start, Position).trim();
        if (!Included.empty())
          Result.Inputs.push_back(
              {unquote(Included), false, false});
      }
    }
  }

private:
  static bool isIdentifierStart(char C) {
    return std::isalpha(static_cast<unsigned char>(C)) || C == '_';
  }

  static bool isIdentifierBody(char C) {
    return std::isalnum(static_cast<unsigned char>(C)) || C == '_' ||
           C == '.' || C == '-';
  }

  Error skipTrivia() {
    while (Position != Text.size()) {
      if (std::isspace(static_cast<unsigned char>(Text[Position])) ||
          Text[Position] == ';') {
        ++Position;
        continue;
      }
      if (Text.substr(Position).starts_with("/*")) {
        size_t End = Text.find("*/", Position + 2);
        if (End == StringRef::npos)
          return error("unterminated block comment");
        Position = End + 2;
        continue;
      }
      if (Text.substr(Position).starts_with("//")) {
        size_t End = Text.find('\n', Position + 2);
        Position = End == StringRef::npos ? Text.size() : End + 1;
        continue;
      }
      break;
    }
    return Error::success();
  }

  std::string readIdentifier() {
    const size_t Start = Position++;
    while (Position != Text.size() && isIdentifierBody(Text[Position]))
      ++Position;
    return Text.slice(Start, Position).str();
  }

  Expected<StringRef> balanced(char Open, char Close) {
    const size_t Start = ++Position;
    unsigned Depth = 1;
    char Quote = 0;
    for (; Position != Text.size(); ++Position) {
      const char C = Text[Position];
      if (Quote) {
        if (C == '\\' && Position + 1 != Text.size())
          ++Position;
        else if (C == Quote)
          Quote = 0;
        continue;
      }
      if (C == '"' || C == '\'') {
        Quote = C;
        continue;
      }
      if (C == Open)
        ++Depth;
      else if (C == Close && --Depth == 0) {
        StringRef Result = Text.slice(Start, Position);
        ++Position;
        return Result;
      }
    }
    return error("unterminated balanced expression");
  }

  void addInputs(StringRef Body, bool InGroup, bool AsNeeded,
                 LinkerScriptResult &Result) {
    for (StringRef Item : splitArguments(Body)) {
      if (Item.starts_with_insensitive("AS_NEEDED(") &&
          Item.ends_with(")")) {
        addInputs(Item.drop_front(strlen("AS_NEEDED(")).drop_back(),
                  InGroup, true, Result);
        continue;
      }
      Result.Inputs.push_back(
          {unquote(Item), InGroup, AsNeeded});
    }
  }

  void handleCall(StringRef Name, StringRef Body,
                  LinkerScriptResult &Result) {
    if (Name.equals_insensitive("INPUT")) {
      addInputs(Body, false, false, Result);
      return;
    }
    if (Name.equals_insensitive("GROUP")) {
      addInputs(Body, true, false, Result);
      return;
    }

    LinkerScriptOption Option;
    Option.Name = Name.lower().str().str();
    Option.Value = unquote(Body);
    Result.Options.push_back(std::move(Option));
  }

  Error error(const Twine &Message) const {
    return createStringError(errc::invalid_argument,
                             "linker script '" + URI + "': " + Message);
  }

  StringRef URI;
  StringRef Text;
  size_t Position = 0;
};

class BuiltinLinkerScriptProvider final : public LinkerScriptProvider {
public:
  Expected<LinkerScriptResult>
  parse(PluginTaskContext &Task, StringRef LogicalURI,
        ArrayRef<uint8_t> Bytes, const OwnedTargetKey &) const override {
    if (Task.checkCancelled().Code != NEVERC_STATUS_OK)
      return createStringError(
          std::make_error_code(std::errc::operation_canceled),
                               "linker-script parsing was cancelled");
    if (llvm::is_contained(Bytes, uint8_t{0}))
      return createStringError(errc::invalid_argument,
                               "linker script contains a NUL byte");
    StringRef Text(reinterpret_cast<const char *>(Bytes.data()), Bytes.size());
    return ScriptParser(LogicalURI, Text).parse();
  }
};

uint64_t stableValue(StringRef Text) {
  const std::array<uint8_t, 32> Digest =
      SHA256::hash(ArrayRef<uint8_t>(
          reinterpret_cast<const uint8_t *>(Text.data()), Text.size()));
  uint64_t Value = 0;
  std::memcpy(&Value, Digest.data(), sizeof(Value));
  return Value;
}

} // namespace

const LinkerScriptProvider &builtinLinkerScriptProvider() {
  static const BuiltinLinkerScriptProvider Provider;
  return Provider;
}

Error readLinkerScriptInput(LinkInputSetImpl &Set, LinkInputBlob &Blob,
                            PluginLinkInput &Input) {
  StringRef Bytes = Blob.Buffer->getBuffer();
  auto Parsed = Set.Scripts.parse(
      Set.Task, Blob.LogicalURI,
      ArrayRef<uint8_t>(
          reinterpret_cast<const uint8_t *>(Bytes.data()), Bytes.size()),
      Set.Target);
  if (!Parsed)
    return Parsed.takeError();
  Input.ReaderRoute = Parsed->ProviderRoute;

  for (const LinkerScriptLayoutConstraint &ScriptConstraint :
       Parsed->LayoutConstraints) {
    PluginLinkConstraint Constraint;
    Constraint.Kind = "script." + ScriptConstraint.Kind;
    Constraint.SubjectID = Input.ID;
    Constraint.Value = stableValue(ScriptConstraint.Expression);
    Constraint.Required = true;
    Constraint.Origin.InputID = Input.ID;
    Set.Graph->addConstraint(std::move(Constraint));
  }
  Set.ScriptResults.emplace(Input.ID, std::move(*Parsed));
  return Error::success();
}

} // namespace neverc::plugin
