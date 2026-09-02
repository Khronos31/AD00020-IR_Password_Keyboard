#include "cmac.h"

#define ADPK_MASTER_KEY_MAX_FRAMES 32u

static const uint8_t cmac_key[16] =
{
    'A', 'D', 'P', 'K', '-', 'K', 'D', 'F', '-', 'V', '2',
    0, 0, 0, 0, 0
};

static const uint8_t cmac_domain[] = "ADPK-MASTER-KEY-V2";

static void ClearBytes(uint8_t *buffer, uint8_t length)
{
    uint8_t index;
    for (index = 0; index < length; index++)
        buffer[index] = 0;
}

static void LeftShiftOne(uint8_t *buffer)
{
    uint8_t index;
    uint8_t carry = 0;
    uint8_t next_carry;

    for (index = 16u; index > 0u; index--)
    {
        next_carry = (uint8_t)(buffer[index - 1u] >> 7);
        buffer[index - 1u] = (uint8_t)((buffer[index - 1u] << 1) | carry);
        carry = next_carry;
    }
}

static void ProcessBlock(struct AES_ctx *context, uint8_t *mac,
                         const uint8_t *block)
{
    uint8_t index;

    for (index = 0; index < 16u; index++)
        mac[index] ^= block[index];
    AES_ECB_encrypt(context, mac);
}

static void UpdateByte(struct AES_ctx *context, uint8_t *mac,
                       uint8_t *block, uint8_t *block_length, uint8_t value)
{
    block[*block_length] = value;
    (*block_length)++;
    if (*block_length == 16u)
    {
        ProcessBlock(context, mac, block);
        *block_length = 0;
    }
}

void ADPK_DeriveMasterKey(struct AES_ctx *context,
                          const uint8_t *frame_data,
                          uint8_t frame_count,
                          uint8_t *key)
{
    uint8_t k1[16];
    uint8_t mac[16];
    uint8_t block[16];
    uint8_t index;
    uint8_t block_length = 0;
    uint8_t most_significant_bit;

    if (frame_count > ADPK_MASTER_KEY_MAX_FRAMES)
        frame_count = ADPK_MASTER_KEY_MAX_FRAMES;

    ClearBytes(k1, sizeof(k1));
    ClearBytes(mac, sizeof(mac));
    ClearBytes(block, sizeof(block));
    AES_init_ctx(context, cmac_key);

    /* K1 is derived from AES-CMAC's L value.  The message is never a
     * multiple of 16 bytes (18 + 1 + 4*n), so K2 is used for its final block. */
    AES_ECB_encrypt(context, k1);
    most_significant_bit = (uint8_t)(k1[0] & 0x80u);
    LeftShiftOne(k1);
    if (most_significant_bit)
        k1[15] ^= 0x87u;

    for (index = 0; index < (uint8_t)(sizeof(cmac_domain) - 1u); index++)
        UpdateByte(context, mac, block, &block_length, cmac_domain[index]);
    UpdateByte(context, mac, block, &block_length, frame_count);

    for (index = 0; index < frame_count; index++)
    {
        UpdateByte(context, mac, block, &block_length,
                   frame_data[(uint8_t)(index * 2u)]);
        UpdateByte(context, mac, block, &block_length,
                   (uint8_t)~frame_data[(uint8_t)(index * 2u)]);
        UpdateByte(context, mac, block, &block_length,
                   frame_data[(uint8_t)(index * 2u + 1u)]);
        UpdateByte(context, mac, block, &block_length,
                   (uint8_t)~frame_data[(uint8_t)(index * 2u + 1u)]);
    }

    block[block_length++] = 0x80u;
    while (block_length < 16u)
        block[block_length++] = 0;
    most_significant_bit = (uint8_t)(k1[0] & 0x80u);
    LeftShiftOne(k1);
    if (most_significant_bit)
        k1[15] ^= 0x87u;
    for (index = 0; index < 16u; index++)
        block[index] ^= k1[index];
    ProcessBlock(context, mac, block);
    for (index = 0; index < 16u; index++)
        key[index] = mac[index];

    ClearBytes(k1, sizeof(k1));
    ClearBytes(mac, sizeof(mac));
    ClearBytes(block, sizeof(block));
}
