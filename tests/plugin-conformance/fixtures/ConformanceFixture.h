/*===-- ConformanceFixture.h - shared plugin conformance fixture helpers -===*\
|*                                                                            *|
|* Helpers shared by the NeverC plugin conformance fixtures. Every fixture is  *|
|* pure C and includes only the public SDK single header, so these fixtures    *|
|* prove the shipped ABI is usable by an ordinary C compiler with no access to  *|
|* the host's private headers.                                                  *|
|*                                                                            *|
|* Lifecycle events are appended to the file named by the NEVERC_CONFORMANCE_  *|
|* LOG environment variable. That gives the test a deterministic, ordered      *|
|* record of process/register/session/task/destroy callbacks and of per-scope  *|
|* user data, independent of how host diagnostics are formatted or filtered.   *|
\*===----------------------------------------------------------------------===*/

#ifndef NEVERC_CONFORMANCE_FIXTURE_H
#define NEVERC_CONFORMANCE_FIXTURE_H

#include "neverc/Plugin/NevercPluginAPI.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NCF_SV(Text)                                                           \
  (NevercStringView) { (Text), (uint64_t)(sizeof(Text) - 1) }

static NevercStatus ncf_status(NevercStatusCode Code) {
  NevercStatus Status = neverc_status_ok();
  Status.Code = Code;
  return Status;
}

/* Append one "event\n" line to the conformance log, if configured. */
static void ncf_log(const char *Event) {
  const char *Path = getenv("NEVERC_CONFORMANCE_LOG");
  FILE *File;
  if (Path == NULL || Path[0] == '\0')
    return;
  File = fopen(Path, "ab");
  if (File == NULL)
    return;
  fputs(Event, File);
  fputc('\n', File);
  fclose(File);
}

/* Emit a host diagnostic so a test can observe a plugin ran via stderr too. */
static NevercStatus ncf_emit(const NevercCoreAPI *Core, const char *PluginID,
                             uint32_t Code, const char *Message) {
  NevercDiagnosticDescriptor Diagnostic;
  NevercDiagnosticHandle Handle = {0};
  if (Core == NULL || Core->EmitDiagnostic == NULL)
    return ncf_status(NEVERC_STATUS_INVALID_ARGUMENT);
  memset(&Diagnostic, 0, sizeof(Diagnostic));
  Diagnostic.Header = (NevercABITableHeader){
      (uint32_t)sizeof(Diagnostic), NEVERC_CORE_API_MAJOR, NEVERC_CORE_API_MINOR,
      0};
  Diagnostic.Severity = NEVERC_DIAGNOSTIC_WARNING;
  Diagnostic.Code = Code;
  Diagnostic.PluginID =
      (NevercStringView){PluginID, (uint64_t)strlen(PluginID)};
  Diagnostic.Message =
      (NevercStringView){Message, (uint64_t)strlen(Message)};
  return Core->EmitDiagnostic(Core->Context, &Diagnostic, &Handle);
}

/* Copy a fully-initialized descriptor into the host's out-parameter, honoring
 * the capacity it advertised and reporting the real struct size back. */
static void ncf_write_descriptor(NevercPluginDescriptor *Out,
                                 const NevercPluginDescriptor *Source) {
  uint32_t Capacity = Out->Header.StructSize;
  size_t Writable = Capacity < sizeof(*Source) ? Capacity : sizeof(*Source);
  memcpy(Out, Source, Writable);
  Out->Header.StructSize = (uint32_t)sizeof(*Source);
}

#endif /* NEVERC_CONFORMANCE_FIXTURE_H */
