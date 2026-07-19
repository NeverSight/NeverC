#include "PluginFrontendFuzzSupport.h"
#include "neverc/Plugin/Host/FrontendPluginBridge.h"
#include "neverc/Plugin/Host/PluginTaskContext.h"
#include "llvm/Support/Error.h"
#include <algorithm>
#include <cstdint>

using namespace llvm;
using namespace neverc;
using namespace neverc::fuzz;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
  ByteCursor Input(Data, Size);
  auto Created =
      PluginFrontendFuzzIteration::create(pluginFuzzRuntime(), false);
  if (!Created) {
    consumeError(Created.takeError());
    return 0;
  }

  std::unique_ptr<PluginFrontendFuzzIteration> Iteration =
      std::move(*Created);
  const NevercPrepAPI &API = Iteration->prepAPI();
  const NevercTaskHandle ValidTask = Iteration->task().handle();
  auto Anchor = Iteration->anchorLocation();
  if (!Anchor) {
    consumeError(Anchor.takeError());
    return 0;
  }

  // Exercise TokenStream creation and bounded query paths with a known-valid
  // native stream before mutating arbitrary builders.
  std::vector<Token> NativeTokens = Iteration->lexAllTokens();
  auto Stream = Iteration->prepBridge().createTokenStream(NativeTokens);
  if (Stream) {
    NevercTokenViewList View{};
    View.Header.StructSize =
        std::min<uint32_t>(sizeof(View), Input.takeU32());
    (void)API.GetTokenStreamView(API.Context, chooseTaskHandle(Input, ValidTask),
                                 *Stream, &View);

    NevercTokenHandle Token{};
    (void)API.GetTokenStreamToken(
        API.Context, chooseTaskHandle(Input, ValidTask), *Stream,
        Input.takeU64(), &Token);
    NevercTokenInfo Info{};
    Info.Header.StructSize =
        std::min<uint32_t>(sizeof(Info), Input.takeU32());
    (void)API.GetTokenInfo(API.Context, chooseTaskHandle(Input, ValidTask),
                           Token, &Info);
  } else {
    consumeError(Stream.takeError());
  }

  const unsigned OperationCount =
      std::min<unsigned>(Input.takeByte(), 64U);
  for (unsigned Operation = 0; Operation != OperationCount; ++Operation) {
    NevercTokenBuilderHandle Builder{};
    if (API.CreateTokenBuilder(API.Context, ValidTask, &Builder).Code !=
        NEVERC_STATUS_OK)
      continue;

    NevercTaskHandle OperationTask = chooseTaskHandle(Input, ValidTask);
    switch (Input.takeByte() & 3U) {
    case 0:
      (void)API.TokenBuilderSetKind(API.Context, OperationTask, Builder,
                                    Input.takeU32());
      break;
    case 1: {
      ArrayRef<uint8_t> Bytes = Input.takeBytes(64);
      NevercStringView Spelling{
          reinterpret_cast<const char *>(Bytes.data()),
          static_cast<uint64_t>(Bytes.size())};
      (void)API.TokenBuilderSetLiteral(API.Context, OperationTask, Builder,
                                       Input.takeU32(), Spelling);
      break;
    }
    case 2: {
      ArrayRef<uint8_t> Bytes = Input.takeBytes(64);
      NevercStringView Name{reinterpret_cast<const char *>(Bytes.data()),
                            static_cast<uint64_t>(Bytes.size())};
      NevercIdentifierHandle Identifier{};
      if (API.GetOrCreateIdentifier(API.Context, OperationTask, Name,
                                    &Identifier)
              .Code == NEVERC_STATUS_OK)
        (void)API.TokenBuilderSetIdentifier(API.Context, OperationTask, Builder,
                                            Identifier);
      break;
    }
    default:
      break;
    }

    NevercSourceLocation Location =
        (Input.takeByte() & 1U) ? *Anchor : arbitraryHandle(Input);
    (void)API.TokenBuilderSetLocation(API.Context, OperationTask, Builder,
                                      Location);
    (void)API.TokenBuilderSetFlags(API.Context, OperationTask, Builder,
                                   Input.takeU32());

    NevercTokenHandle Token{};
    NevercStatus Commit =
        API.TokenBuilderCommit(API.Context, OperationTask, Builder, &Token);
    if (Commit.Code == NEVERC_STATUS_OK) {
      NevercTokenInfo Info{};
      Info.Header.StructSize = sizeof(Info);
      (void)API.GetTokenInfo(API.Context, ValidTask, Token, &Info);
    }

    // Cleanup always uses the owning task, including after deliberately
    // malformed task handles above.
    (void)API.DestroyTokenBuilder(API.Context, ValidTask, Builder);
  }
  return 0;
}
