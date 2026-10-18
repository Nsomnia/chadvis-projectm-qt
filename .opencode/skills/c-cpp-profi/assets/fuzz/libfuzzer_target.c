#include <stddef.h>
#include <stdint.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  if (data == NULL && size != 0) {
    return 0;
  }
  return 0;
}
