//===- NetworkProtocolFuzzer.cpp - Server protocol fuzzing -------------===//

#include <cstddef>
#include <cstdint>

extern "C" {
int neverc_http_test_fuzz_request_parser(const void *input,
                                         size_t input_length);
int neverc_ws_test_fuzz_frame_parser(const void *input, size_t input_length,
                                     int is_client);
int neverc_ws_test_fuzz_url_parser(const void *input, size_t input_length);
void neverc_network_test_fuzz_binary_protocols(uint8_t selector,
                                               const uint8_t *input,
                                               size_t input_length);
}

namespace {
constexpr size_t MaxInputSize = 64 * 1024;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  if (!data || size == 0 || size > MaxInputSize)
    return 0;

  uint8_t selector = data[0];
  const uint8_t *payload = data + 1;
  size_t payloadSize = size - 1;
  switch (selector % 6) {
  case 0:
    (void)neverc_http_test_fuzz_request_parser(payload, payloadSize);
    break;
  case 1:
    if ((selector & 0x40) != 0)
      (void)neverc_ws_test_fuzz_url_parser(payload, payloadSize);
    else
      (void)neverc_ws_test_fuzz_frame_parser(payload, payloadSize,
                                             (selector >> 7) & 1);
    break;
  default:
    neverc_network_test_fuzz_binary_protocols(selector, payload, payloadSize);
    break;
  }
  return 0;
}
