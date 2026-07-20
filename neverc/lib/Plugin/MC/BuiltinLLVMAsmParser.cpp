#include "neverc/Plugin/Host/BuiltinLLVMAsmParser.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCObjectFileInfo.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCParser/MCAsmParser.h"
#include "llvm/MC/MCParser/MCTargetAsmParser.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/MCTargetOptions.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/SourceMgr.h"
#include <memory>

using namespace llvm;

namespace neverc::plugin {
namespace {

Error assemblerError(const Twine &Message) {
  return createStringError(errc::invalid_argument, Message);
}

} // namespace

Error runBuiltinLLVMAsmParser(
    const BuiltinLLVMAsmParserRequest &Request) {
  if (!Request.Target || !Request.Output)
    return assemblerError("invalid builtin assembly parser request");

  MCTargetOptions MCOptions;
  std::unique_ptr<MCRegisterInfo> MRI(
      Request.Target->createMCRegInfo(Request.TargetTriple.str()));
  std::unique_ptr<MCAsmInfo> MAI(
      MRI ? Request.Target->createMCAsmInfo(
                *MRI, Request.TargetTriple.str(), MCOptions)
          : nullptr);
  std::unique_ptr<MCSubtargetInfo> STI(
      Request.Target->createMCSubtargetInfo(
          Request.TargetTriple.str(), Request.CPU, Request.Features));
  std::unique_ptr<MCInstrInfo> MCII(
      Request.Target->createMCInstrInfo());
  if (!MRI || !MAI || !STI || !MCII)
    return assemblerError(
        "target does not provide the required assembler components");

  SourceMgr SourceManager;
  SourceManager.AddNewSourceBuffer(
      MemoryBuffer::getMemBufferCopy(
          Request.Input.getBuffer(),
          Request.Input.getBufferIdentifier()),
      SMLoc());
  MCContext Context(Request.TargetTriple, MAI.get(), MRI.get(), STI.get(),
                    &SourceManager, &MCOptions);
  std::unique_ptr<MCObjectFileInfo> ObjectFileInfo(
      Request.Target->createMCObjectFileInfo(Context));
  if (!ObjectFileInfo)
    return assemblerError(
        "target does not provide object-file information");
  ObjectFileInfo->setSDKVersion(Request.SDKVersion);
  Context.setObjectFileInfo(ObjectFileInfo.get());

  std::unique_ptr<MCCodeEmitter> Emitter(
      Request.Target->createMCCodeEmitter(*MCII, Context));
  std::unique_ptr<MCAsmBackend> Backend(
      Request.Target->createMCAsmBackend(*STI, *MRI, MCOptions));
  if (!Emitter || !Backend)
    return assemblerError(
        "target does not provide object emission components");

  std::unique_ptr<MCObjectWriter> ObjectWriter =
      Backend->createObjectWriter(*Request.Output);
  std::unique_ptr<MCStreamer> Streamer(
      Request.Target->createMCObjectStreamer(
          Request.TargetTriple, Context, std::move(Backend),
          std::move(ObjectWriter), std::move(Emitter), *STI,
          MCOptions.MCRelaxAll,
          MCOptions.MCIncrementalLinkerCompatible,
          /*DWARFMustBeAtTheEnd=*/true));
  if (!Streamer)
    return assemblerError(
        "target cannot create an object streamer");

  std::unique_ptr<MCAsmParser> Parser(
      createMCAsmParser(SourceManager, Context, *Streamer, *MAI));
  std::unique_ptr<MCTargetAsmParser> TargetParser(
      Parser ? Request.Target->createMCAsmParser(
                   *STI, *Parser, *MCII, MCOptions)
             : nullptr);
  if (!Parser || !TargetParser)
    return assemblerError(
        "target does not provide an assembly parser");

  Parser->setTargetParser(*TargetParser);
  if (Parser->Run(/*NoInitialTextSection=*/false))
    return assemblerError("assembly parsing failed");
  return Error::success();
}

} // namespace neverc::plugin
