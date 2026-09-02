#ifndef ADPK_CMAC_H
#define ADPK_CMAC_H

#include <stdint.h>

#include "aes.h"

/* Derive the 128-bit database key from the compact NEC frame representation.
 * frame_data contains address and command bytes for each frame; their
 * complementary bytes are reconstructed by the implementation. */
void ADPK_DeriveMasterKey(struct AES_ctx *context,
                          const uint8_t *frame_data,
                          uint8_t frame_count,
                          uint8_t *key);

#endif /* ADPK_CMAC_H */
