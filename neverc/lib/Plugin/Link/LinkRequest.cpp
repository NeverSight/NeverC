#include "LinkRequest.h"
#include "llvm/ADT/Twine.h"
#include <algorithm>
#include <cstring>
#include <utility>

using namespace llvm;

namespace neverc::plugin {
namespace {

bool nonzero(NevercInterfaceID ID) {
  return ID.High != 0 || ID.Low != 0;
}

NevercStringView view(const std::string &Value) {
  return {Value.data(), static_cast<uint64_t>(Value.size())};
}

Error requestError(const Twine &Message) {
  return createStringError(inconvertibleErrorCode(), Message);
}

} // namespace

Expected<std::shared_ptr<const LinkRequest>>
LinkRequest::create(LinkRequestData Data) {
  const NevercTargetKey Target = Data.Target.view();
  if (!nonzero(Target.TargetID))
    return requestError("link request has no target ID");
  if (!nonzero(Data.OutputFormat))
    return requestError("link request has no output format");
  if (Data.OutputKind < NEVERC_LINK_OUTPUT_RELOCATABLE ||
      Data.OutputKind > NEVERC_LINK_OUTPUT_BUNDLE)
    return requestError("link request has an invalid output kind");
  if (Data.OutputURI.empty())
    return requestError("link request has no output URI");

  for (size_t Index = 0; Index != Data.Inputs.size(); ++Index) {
    const OwnedRawLinkInput &Input = Data.Inputs[Index];
    if (Input.Ordinal != Index)
      return requestError(
          "link request inputs are not in canonical frozen order");
    if (Input.Kind < NEVERC_LINK_INPUT_OBJECT ||
        Input.Kind > NEVERC_LINK_INPUT_BLOB)
      return requestError("link request has an invalid input kind");
    if (Input.LogicalURI.empty() && Input.AuthorizedBlob.empty() &&
        neverc_handle_is_null(Input.ObjectGraph) &&
        neverc_handle_is_null(Input.Artifact))
      return requestError("link request input has no authorized source");
  }

  return std::shared_ptr<const LinkRequest>(
      new LinkRequest(std::move(Data)));
}

LinkRequest::LinkRequest(LinkRequestData DataValue)
    : Data(std::move(DataValue)) {
  rebuildCView();
}

void LinkRequest::rebuildCView() {
  CInputs.clear();
  CInputs.reserve(Data.Inputs.size());
  for (const OwnedRawLinkInput &Input : Data.Inputs) {
    NevercRawLinkInput CInput{};
    CInput.Header = {sizeof(CInput), NEVERC_LINK_API_MAJOR,
                     NEVERC_LINK_API_MINOR, 0};
    CInput.Kind = Input.Kind;
    CInput.Flags = Input.Flags;
    CInput.Ordinal = Input.Ordinal;
    CInput.LogicalURI = view(Input.LogicalURI);
    CInput.AuthorizedBlob = {
        Input.AuthorizedBlob.data(),
        static_cast<uint64_t>(Input.AuthorizedBlob.size())};
    CInput.ObjectGraph = Input.ObjectGraph;
    CInput.Artifact = Input.Artifact;
    CInputs.push_back(CInput);
  }

  CSearchPaths.clear();
  CSearchPaths.reserve(Data.Options.SearchPaths.size());
  for (const std::string &Path : Data.Options.SearchPaths)
    CSearchPaths.push_back(view(Path));

  CLibraries.clear();
  CLibraries.reserve(Data.Options.Libraries.size());
  for (const std::string &Library : Data.Options.Libraries)
    CLibraries.push_back(view(Library));

  CView = {};
  CView.Header = {sizeof(CView), NEVERC_LINK_API_MAJOR,
                  NEVERC_LINK_API_MINOR, 0};
  CView.Request = Data.Request;
  CView.Task = Data.Task;
  CView.Target = Data.Target.view();
  CView.InputFormat = Data.InputFormat;
  CView.OutputFormat = Data.OutputFormat;
  CView.OutputKind = Data.OutputKind;
  CView.OutputURI = view(Data.OutputURI);
  CView.Options.Header = {sizeof(CView.Options), NEVERC_LINK_API_MAJOR,
                          NEVERC_LINK_API_MINOR, 0};
  CView.Options.Flags = Data.Options.Flags;
  CView.Options.EntrySymbol = view(Data.Options.EntrySymbol);
  CView.Options.InstallName = view(Data.Options.InstallName);
  CView.Options.Soname = view(Data.Options.Soname);
  CView.Options.ImageBase = Data.Options.ImageBase;
  CView.Options.PageSize = Data.Options.PageSize;
  CView.Options.ThreadBudget = Data.Options.ThreadBudget;
  CView.Options.SearchPaths = {
      CSearchPaths.data(), static_cast<uint64_t>(CSearchPaths.size()),
      sizeof(NevercStringView)};
  CView.Options.Libraries = {
      CLibraries.data(), static_cast<uint64_t>(CLibraries.size()),
      sizeof(NevercStringView)};
  CView.RawInputs.Header = {sizeof(CView.RawInputs),
                            NEVERC_LINK_API_MAJOR,
                            NEVERC_LINK_API_MINOR, 0};
  CView.RawInputs.Inputs = {
      CInputs.data(), static_cast<uint64_t>(CInputs.size()),
      sizeof(NevercRawLinkInput)};
  std::copy(Data.RequestDigest.begin(), Data.RequestDigest.end(),
            CView.RequestDigest);
}

} // namespace neverc::plugin
