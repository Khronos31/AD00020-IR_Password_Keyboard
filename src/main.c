#include <xc.h>
#include <string.h>
#include "GenericTypeDefs.h"
#include "Compiler.h"
#include "usb_config.h"
#include "USB/usb.h"
#include "HardwareProfile.h"
#include "USB/usb_function_hid.h"
#include "crypto/aes.h"
#include "generated_database.h"

/* PIC18F14K50 USB needs the 48 MHz PLL clock. */
#pragma config CPUDIV = NOCLKDIV
#pragma config USBDIV = OFF
#pragma config FOSC   = HS
#pragma config PLLEN  = ON
#pragma config FCMEN  = OFF
#pragma config IESO   = OFF
#pragma config PWRTEN = OFF
#pragma config BOREN  = OFF
#pragma config BORV   = 30
#pragma config WDTEN  = OFF
#pragma config WDTPS  = 32768
#pragma config MCLRE  = OFF
#pragma config HFOFST = OFF
#pragma config STVREN = ON
#pragma config LVP    = OFF
#pragma config XINST  = OFF
#pragma config BBSIZ  = OFF
#pragma config CP0    = OFF
#pragma config CP1    = OFF
#pragma config CPB    = OFF
#pragma config WRT0   = OFF
#pragma config WRT1   = OFF
#pragma config WRTB   = OFF
#pragma config WRTC   = OFF
#pragma config EBTR0  = OFF
#pragma config EBTR1  = OFF
#pragma config EBTRB  = OFF

#define TIMER0_RELOAD_H          0xFF
#define TIMER0_RELOAD_L          0x6A       /* 100us at 48MHz, 1:8 prescale */
#define FRAME_GAP_TICKS          600        /* 60ms */
#define RAW_MAX_PULSES           100        /* NEC frame plus terminating gap */
#define RAW_BUFFER_BANK_SIZE     50
#define MASTER_KEY_SIZE          16
#define UNLOCK_FRAME_COUNT       4
#define UNLOCK_TIMEOUT_SECONDS   30
#define AUTO_LOCK_SECONDS        180
#define PASSWORD_BUFFER_SIZE     64
#define FUNCTION_CODE_COUNT      4
#define PASSWORD_CODE_COUNT      12

/* The USB stack requires endpoint buffers in the USB dual-port RAM. */
#pragma udata usbram2
static BYTE keyboard_report[8] USB_FIXED_AT(0x239);
static BYTE keyboard_out_report[1] USB_FIXED_AT(0x241);
#pragma udata

static USB_HANDLE usb_in_handle;
static USB_HANDLE usb_out_handle;

/* Capture storage and AES round keys are never needed at the same time. */
typedef union
{
    struct
    {
        volatile WORD raw_buffer0[RAW_BUFFER_BANK_SIZE];
        volatile WORD raw_buffer1[RAW_BUFFER_BANK_SIZE];
    } capture;
    struct AES_ctx aes_context;
} WORKSPACE;

static WORKSPACE workspace;
#define raw_buffer0 workspace.capture.raw_buffer0
#define raw_buffer1 workspace.capture.raw_buffer1
#define aes_context workspace.aes_context

static volatile BYTE raw_count;
static volatile BYTE raw_ready;
static volatile BYTE raw_overflow;
static volatile BYTE capture_active;
static volatile BYTE previous_signal;
static volatile WORD current_ticks;

static volatile WORD hundred_us_ticks;
static volatile BYTE second_tick_pending;
static volatile BYTE crypto_active;

typedef enum
{
    STATE_LOCKED = 0,
    STATE_UNLOCK_INPUT,
    STATE_UNLOCKED
} DEVICE_STATE;

static DEVICE_STATE device_state;
static BYTE unlock_frames[MASTER_KEY_SIZE];
static BYTE unlock_frame_count;
static BYTE master_key[MASTER_KEY_SIZE];
static BYTE active_slot_count;
static BYTE unlock_elapsed_seconds;
static BYTE idle_seconds;

static BYTE aes_scratch[16];
static BYTE database_header[ADPK_DB_HEADER_SIZE];
static BYTE password_buffer[PASSWORD_BUFFER_SIZE];

static BYTE output_active;
static BYTE output_index;
static BYTE output_key_down;
static BYTE output_enter_sent;
static BYTE output_finished;
static BYTE output_cancel_release;

static const BYTE code_unlock[4] = {0x01, 0xFE, 0x48, 0xB7};
static const BYTE code_lock[4] = {0x01, 0xFE, 0x58, 0xA7};
static const BYTE function_codes[FUNCTION_CODE_COUNT][4] =
{
    {0x01, 0xFE, 0x78, 0x87},
    {0x01, 0xFE, 0x80, 0x7F},
    {0x01, 0xFE, 0x40, 0xBF},
    {0x01, 0xFE, 0xC0, 0x3F}
};
static const BYTE password_codes[PASSWORD_CODE_COUNT][4] =
{
    {0x01, 0xFE, 0x20, 0xDF},
    {0x01, 0xFE, 0xA0, 0x5F},
    {0x01, 0xFE, 0x60, 0x9F},
    {0x01, 0xFE, 0xE0, 0x1F},
    {0x01, 0xFE, 0x10, 0xEF},
    {0x01, 0xFE, 0x90, 0x6F},
    {0x01, 0xFE, 0x50, 0xAF},
    {0x01, 0xFE, 0xD8, 0x27},
    {0x01, 0xFE, 0xF8, 0x07},
    {0x01, 0xFE, 0x30, 0xCF},
    {0x01, 0xFE, 0xB0, 0x4F},
    {0x01, 0xFE, 0x70, 0x8F}
};

static void InitializeSystem(void);
static void UserInit(void);
static void ProcessIO(void);
static void ProcessTimers(void);
static void ReceiverTick(void);
static WORD RawPulseAt(WORD index);
static void StoreRawPulse(WORD index, WORD value);
static BYTE DecodeNECCode(BYTE *code);
static BYTE PulseMatches(WORD actual, WORD target);
static BYTE DecodedNECByte(BYTE byte_index);
static BYTE CodeEquals(const BYTE *left, const BYTE *right);
static BYTE IsMasterCode(const BYTE *code);
static BYTE PasswordSlotForCode(const BYTE *code);
static void HandleReceivedCode(const BYTE *code);
static void EnterUnlockMode(void);
static void LockDatabase(void);
static void TryUnlock(void);
static BYTE ValidateDatabase(void);
static void DecryptDatabaseRange(WORD offset, BYTE *destination, WORD length);
static void StartPasswordOutput(BYTE slot);
static BYTE CharacterToUsage(BYTE character, BYTE *modifier, BYTE *usage);
static void ServiceKeyboardOutput(void);
static void AbortPasswordOutput(void);
static void SecureZero(BYTE *buffer, WORD length);

void Low_ISR(void) __interrupt(low_priority);
void Low_ISR(void)
{
    if (INTCONbits.TMR0IF)
    {
        INTCONbits.TMR0IF = 0;
        TMR0H = TIMER0_RELOAD_H;
        TMR0L = TIMER0_RELOAD_L;
        ReceiverTick();
    }
}

void USBCBCheckOtherReq(void)
{
    USBCheckHIDRequest();
}

void USBCBStdSetDscHandler(void)
{
}

void USBCBInitEP(void)
{
    USBEnableEndpoint(HID_EP, USB_OUT_ENABLED | USB_IN_ENABLED |
                      USB_HANDSHAKE_ENABLED | USB_DISALLOW_SETUP);
}

void USBCBSuspend(void)
{
}

void USBCBWakeFromSuspend(void)
{
}

void USBCB_SOF_Handler(void)
{
}

void USBCBErrorHandler(void)
{
}

BOOL USER_USB_CALLBACK_EVENT_HANDLER(USB_EVENT event, void *pdata, WORD size)
{
    switch (event)
    {
        case EVENT_CONFIGURED:
            USBCBInitEP();
            break;
        case EVENT_EP0_REQUEST:
            USBCBCheckOtherReq();
            break;
        case EVENT_SET_DESCRIPTOR:
            USBCBStdSetDscHandler();
            break;
        case EVENT_SOF:
            USBCB_SOF_Handler();
            break;
        case EVENT_SUSPEND:
            USBCBSuspend();
            break;
        case EVENT_RESUME:
            USBCBWakeFromSuspend();
            break;
        case EVENT_BUS_ERROR:
            USBCBErrorHandler();
            break;
        default:
            break;
    }
    return TRUE;
}

static void SecureZero(BYTE *buffer, WORD length)
{
    volatile BYTE *target = (volatile BYTE *)buffer;
    while (length--)
        *target++ = 0;
}

static void ClearKeyboardReport(void)
{
    BYTE index;
    for (index = 0; index < 8u; index++)
        keyboard_report[index] = 0;
}

static void InitializeSystem(void)
{
    UserInit();
    USBDeviceInit();
}

static void UserInit(void)
{
    ANSEL = 0x00;
    ANSELH = 0x00;

    TRISB = 0x00;
    /* RC7 is the active-low output of the demodulating IR receiver. */
    LATC = 0xDF;
    TRISC = 0x80;

    raw_count = 0;
    raw_ready = 0;
    raw_overflow = 0;
    capture_active = 0;
    previous_signal = 1;
    current_ticks = 0;
    hundred_us_ticks = 0;
    second_tick_pending = 0;
    crypto_active = 0;

    device_state = STATE_LOCKED;
    unlock_frame_count = 0;
    active_slot_count = 0;
    unlock_elapsed_seconds = 0;
    idle_seconds = 0;
    output_active = 0;
    output_index = 0;
    output_key_down = 1;
    output_enter_sent = 0;
    output_finished = 0;
    output_cancel_release = 0;

    SecureZero(unlock_frames, sizeof(unlock_frames));
    SecureZero(master_key, sizeof(master_key));
    SecureZero((BYTE *)&aes_context, sizeof(aes_context));
    SecureZero(aes_scratch, sizeof(aes_scratch));
    SecureZero(database_header, sizeof(database_header));
    SecureZero(password_buffer, sizeof(password_buffer));

    usb_in_handle = 0;
    usb_out_handle = 0;
    ClearKeyboardReport();
    keyboard_out_report[0] = 0;

    /* Timer0: 16-bit, internal instruction clock, 1:8 prescaler. */
    T0CON = 0x82;
    TMR0H = TIMER0_RELOAD_H;
    TMR0L = TIMER0_RELOAD_L;
    INTCONbits.TMR0IF = 0;
    INTCONbits.TMR0IE = 1;
    INTCON2bits.TMR0IP = 0;
    RCONbits.IPEN = 1;
}

static void ReceiverTick(void)
{
    BYTE signal = PORTCbits.RC7 ? 1 : 0;

    if (hundred_us_ticks < 9999u)
        hundred_us_ticks++;
    else
    {
        hundred_us_ticks = 0;
        second_tick_pending = 1;
    }

    if (crypto_active)
        return;

    if (raw_ready)
        return;

    if (!capture_active)
    {
        if (!signal)
        {
            capture_active = 1;
            raw_count = 0;
            raw_overflow = 0;
            current_ticks = 0;
            previous_signal = 0;
        }
        return;
    }

    if (current_ticks < FRAME_GAP_TICKS)
        current_ticks++;

    if (signal != previous_signal)
    {
        if (raw_count < RAW_MAX_PULSES)
            StoreRawPulse(raw_count++, current_ticks);
        else
            raw_overflow = 1;
        current_ticks = 0;
        previous_signal = signal;
    }

    if (previous_signal && current_ticks >= FRAME_GAP_TICKS)
    {
        if (raw_count < RAW_MAX_PULSES)
            StoreRawPulse(raw_count++, current_ticks);
        else
            raw_overflow = 1;
        capture_active = 0;
        raw_ready = 1;
    }
}

static WORD RawPulseAt(WORD index)
{
    if (index < RAW_BUFFER_BANK_SIZE)
        return raw_buffer0[index];
    return raw_buffer1[index - RAW_BUFFER_BANK_SIZE];
}

static void StoreRawPulse(WORD index, WORD value)
{
    if (index < RAW_BUFFER_BANK_SIZE)
        raw_buffer0[index] = value;
    else
        raw_buffer1[index - RAW_BUFFER_BANK_SIZE] = value;
}

static BYTE PulseMatches(WORD actual, WORD target)
{
    return (actual >= (target * 7u) / 10u &&
            actual <= (target * 13u) / 10u);
}

static BYTE DecodedNECByte(BYTE byte_index)
{
    BYTE value = 0;
    BYTE reversed = 0;
    BYTE bit;

    for (bit = 0; bit < 8u; bit++)
    {
        if (RawPulseAt(2u + ((WORD)byte_index * 16u) +
                       ((WORD)bit * 2u) + 1u) > 8u)
            value |= (BYTE)(1u << bit);
    }

    for (bit = 0; bit < 8u; bit++)
    {
        reversed <<= 1;
        reversed |= (value >> bit) & 1u;
    }
    return reversed;
}

static BYTE DecodeNECCode(BYTE *code)
{
    WORD pulse_index;
    BYTE bit_count = 0;
    BYTE index;

    if (raw_count < 68u ||
        !PulseMatches(RawPulseAt(0), 90u) ||
        !PulseMatches(RawPulseAt(1), 45u))
        return 0;

    pulse_index = 2u;
    while (pulse_index + 1u < raw_count && bit_count < 32u)
    {
        if (!PulseMatches(RawPulseAt(pulse_index), 6u) ||
            RawPulseAt(pulse_index + 1u) > 40u ||
            (!PulseMatches(RawPulseAt(pulse_index + 1u), 6u) &&
             !PulseMatches(RawPulseAt(pulse_index + 1u), 17u)))
            return 0;
        bit_count++;
        pulse_index += 2u;
    }

    if (bit_count != 32u || pulse_index >= raw_count ||
        !PulseMatches(RawPulseAt(pulse_index), 6u))
        return 0;

    for (index = 0; index < 4u; index++)
        code[index] = DecodedNECByte(index);

    /* Standard NEC carries each address and command byte with its inverse. */
    if ((BYTE)(code[0] ^ code[1]) != 0xFFu ||
        (BYTE)(code[2] ^ code[3]) != 0xFFu)
        return 0;
    return 1;
}

static BYTE CodeEquals(const BYTE *left, const BYTE *right)
{
    BYTE index;
    for (index = 0; index < 4u; index++)
    {
        if (left[index] != right[index])
            return 0;
    }
    return 1;
}

static BYTE IsMasterCode(const BYTE *code)
{
    BYTE index;

    for (index = 0; index < FUNCTION_CODE_COUNT; index++)
    {
        if (CodeEquals(code, function_codes[index]))
            return 1;
    }
    for (index = 0; index < PASSWORD_CODE_COUNT; index++)
    {
        if (CodeEquals(code, password_codes[index]))
            return 1;
    }
    return 0;
}

static BYTE PasswordSlotForCode(const BYTE *code)
{
    BYTE index;
    for (index = 0; index < PASSWORD_CODE_COUNT; index++)
    {
        if (CodeEquals(code, password_codes[index]))
            return (BYTE)(index + 1u);
    }
    return 0;
}

static void AbortPasswordOutput(void)
{
    if (output_active && !output_key_down)
        output_cancel_release = 1;
    output_active = 0;
    output_finished = 0;
    output_key_down = 1;
    output_enter_sent = 0;
    output_index = 0;
    SecureZero(password_buffer, sizeof(password_buffer));
    ClearKeyboardReport();
}

static void LockDatabase(void)
{
    AbortPasswordOutput();
    SecureZero(master_key, sizeof(master_key));
    SecureZero(unlock_frames, sizeof(unlock_frames));
    SecureZero((BYTE *)&aes_context, sizeof(aes_context));
    SecureZero(aes_scratch, sizeof(aes_scratch));
    SecureZero(database_header, sizeof(database_header));
    unlock_frame_count = 0;
    active_slot_count = 0;
    unlock_elapsed_seconds = 0;
    idle_seconds = 0;
    device_state = STATE_LOCKED;
}

static void EnterUnlockMode(void)
{
    LockDatabase();
    device_state = STATE_UNLOCK_INPUT;
    unlock_elapsed_seconds = 0;
}

static void DecryptDatabaseRange(WORD offset, BYTE *destination, WORD length)
{
    WORD block = offset / 16u;
    BYTE intra = (BYTE)(offset % 16u);
    BYTE index;
    WORD remaining = length;
    WORD cipher_index = offset;

    crypto_active = 1;
    capture_active = 0;
    raw_ready = 0;
    raw_count = 0;
    current_ticks = 0;

    AES_init_ctx_iv(&aes_context, master_key, adpk_db_iv);
    while (block--)
    {
        SecureZero(aes_scratch, sizeof(aes_scratch));
        AES_CTR_xcrypt_buffer(&aes_context, aes_scratch, 16u);
    }

    while (remaining)
    {
        SecureZero(aes_scratch, sizeof(aes_scratch));
        AES_CTR_xcrypt_buffer(&aes_context, aes_scratch, 16u);
        for (index = intra; index < 16u && remaining; index++)
        {
            destination[length - remaining] =
                (BYTE)(adpk_db_ciphertext[cipher_index] ^ aes_scratch[index]);
            cipher_index++;
            remaining--;
        }
        intra = 0;
    }
    SecureZero(aes_scratch, sizeof(aes_scratch));
    crypto_active = 0;
}

static BYTE ValidateDatabase(void)
{
    DecryptDatabaseRange(0, database_header, ADPK_DB_HEADER_SIZE);
    if (database_header[0] != 'A' || database_header[1] != 'D' ||
        database_header[2] != 'P' || database_header[3] != 'K' ||
        database_header[4] != ADPK_DB_VERSION ||
        database_header[5] != ADPK_DB_SLOT_COUNT ||
        database_header[6] != ADPK_DB_MAX_PASSWORD_LENGTH ||
        database_header[7] > ADPK_DB_SLOT_COUNT)
        return 0;
    active_slot_count = database_header[7];
    return 1;
}

static void TryUnlock(void)
{
    memcpy(master_key, unlock_frames, MASTER_KEY_SIZE);
    SecureZero(unlock_frames, sizeof(unlock_frames));
    unlock_frame_count = 0;

    if (!ValidateDatabase())
    {
        LockDatabase();
        return;
    }

    SecureZero(database_header, sizeof(database_header));
    device_state = STATE_UNLOCKED;
    idle_seconds = 0;
}

static void StartPasswordOutput(BYTE slot)
{
    WORD offset;
    BYTE index;
    BYTE length;

    if (!active_slot_count || slot > active_slot_count || slot > ADPK_DB_SLOT_COUNT)
        return;

    offset = ADPK_DB_HEADER_SIZE + ((WORD)(slot - 1u) * ADPK_DB_RECORD_SIZE);
    SecureZero(password_buffer, sizeof(password_buffer));
    DecryptDatabaseRange(offset, password_buffer, ADPK_DB_RECORD_SIZE);
    length = password_buffer[0];
    if (!length || length > ADPK_DB_MAX_PASSWORD_LENGTH)
    {
        SecureZero(password_buffer, sizeof(password_buffer));
        return;
    }
    for (index = 0; index < length; index++)
    {
        if (password_buffer[index + 1u] < 0x20u ||
            password_buffer[index + 1u] > 0x7Eu)
        {
            SecureZero(password_buffer, sizeof(password_buffer));
            return;
        }
    }

    output_active = 1;
    output_index = 0;
    output_key_down = 1;
    output_enter_sent = 0;
    output_finished = 0;
}

static void HandleReceivedCode(const BYTE *code)
{
    BYTE slot;

    if (CodeEquals(code, code_unlock))
    {
        EnterUnlockMode();
        return;
    }
    if (CodeEquals(code, code_lock))
    {
        LockDatabase();
        return;
    }

    if (device_state == STATE_UNLOCK_INPUT)
    {
        if (!IsMasterCode(code))
        {
            LockDatabase();
            return;
        }
        if (unlock_frame_count < UNLOCK_FRAME_COUNT)
        {
            memcpy(&unlock_frames[(WORD)unlock_frame_count * 4u], code, 4u);
            unlock_frame_count++;
        }
        if (unlock_frame_count == UNLOCK_FRAME_COUNT)
            TryUnlock();
        return;
    }

    if (device_state != STATE_UNLOCKED)
        return;

    slot = PasswordSlotForCode(code);
    if (slot)
        StartPasswordOutput(slot);
}

static void ProcessReceivedFrame(void)
{
    BYTE code[4];
    BYTE valid;

    valid = DecodeNECCode(code);
    raw_ready = 0;
    raw_count = 0;
    raw_overflow = 0;
    current_ticks = 0;

    if (valid)
        HandleReceivedCode(code);
}

static void ProcessTimers(void)
{
    if (!second_tick_pending)
        return;
    second_tick_pending = 0;

    if (device_state == STATE_UNLOCK_INPUT)
    {
        if (unlock_elapsed_seconds < UNLOCK_TIMEOUT_SECONDS)
            unlock_elapsed_seconds++;
        if (unlock_elapsed_seconds >= UNLOCK_TIMEOUT_SECONDS)
            LockDatabase();
    }
    else if (device_state == STATE_UNLOCKED)
    {
        if (idle_seconds < AUTO_LOCK_SECONDS)
            idle_seconds++;
        if (idle_seconds >= AUTO_LOCK_SECONDS)
            LockDatabase();
    }
}

static BYTE CharacterToUsage(BYTE character, BYTE *modifier, BYTE *usage)
{
    *modifier = 0;
    if (character >= 'a' && character <= 'z')
    {
        *usage = (BYTE)(character - 'a' + 0x04);
        return 1;
    }
    if (character >= 'A' && character <= 'Z')
    {
        *modifier = 0x02;
        *usage = (BYTE)(character - 'A' + 0x04);
        return 1;
    }
    if (character >= '1' && character <= '9')
    {
        *usage = (BYTE)(character - '1' + 0x1E);
        return 1;
    }
    if (character == '0') { *usage = 0x27; return 1; }
    if (character == ' ') { *usage = 0x2C; return 1; }
    if (character == '-') { *usage = 0x2D; return 1; }
    if (character == '=') { *usage = 0x2E; return 1; }
    if (character == '[') { *usage = 0x2F; return 1; }
    if (character == ']') { *usage = 0x30; return 1; }
    if (character == '\\') { *usage = 0x31; return 1; }
    if (character == ';') { *usage = 0x33; return 1; }
    if (character == '\'') { *usage = 0x34; return 1; }
    if (character == '`') { *usage = 0x35; return 1; }
    if (character == ',') { *usage = 0x36; return 1; }
    if (character == '.') { *usage = 0x37; return 1; }
    if (character == '/') { *usage = 0x38; return 1; }

    if (character == '!') { *modifier = 0x02; *usage = 0x1E; return 1; }
    if (character == '@') { *modifier = 0x02; *usage = 0x1F; return 1; }
    if (character == '#') { *modifier = 0x02; *usage = 0x20; return 1; }
    if (character == '$') { *modifier = 0x02; *usage = 0x21; return 1; }
    if (character == '%') { *modifier = 0x02; *usage = 0x22; return 1; }
    if (character == '^') { *modifier = 0x02; *usage = 0x23; return 1; }
    if (character == '&') { *modifier = 0x02; *usage = 0x24; return 1; }
    if (character == '*') { *modifier = 0x02; *usage = 0x25; return 1; }
    if (character == '(') { *modifier = 0x02; *usage = 0x26; return 1; }
    if (character == ')') { *modifier = 0x02; *usage = 0x27; return 1; }
    if (character == '_') { *modifier = 0x02; *usage = 0x2D; return 1; }
    if (character == '+') { *modifier = 0x02; *usage = 0x2E; return 1; }
    if (character == '{') { *modifier = 0x02; *usage = 0x2F; return 1; }
    if (character == '}') { *modifier = 0x02; *usage = 0x30; return 1; }
    if (character == '|') { *modifier = 0x02; *usage = 0x31; return 1; }
    if (character == ':') { *modifier = 0x02; *usage = 0x33; return 1; }
    if (character == '"') { *modifier = 0x02; *usage = 0x34; return 1; }
    if (character == '~') { *modifier = 0x02; *usage = 0x35; return 1; }
    if (character == '<') { *modifier = 0x02; *usage = 0x36; return 1; }
    if (character == '>') { *modifier = 0x02; *usage = 0x37; return 1; }
    if (character == '?') { *modifier = 0x02; *usage = 0x38; return 1; }
    return 0;
}

static void ServiceKeyboardOutput(void)
{
    BYTE modifier;
    BYTE usage;
    BYTE character;

    if (HIDTxHandleBusy(usb_in_handle))
        return;

    if (output_cancel_release)
    {
        ClearKeyboardReport();
        usb_in_handle = HIDTxPacket(HID_EP, keyboard_report, 8);
        output_cancel_release = 0;
        return;
    }

    if (!output_active)
        return;

    if (output_finished)
    {
        output_active = 0;
        output_finished = 0;
        SecureZero(password_buffer, sizeof(password_buffer));
        idle_seconds = 0;
        return;
    }

    ClearKeyboardReport();
    if (output_key_down)
    {
        if (output_index < password_buffer[0])
        {
            character = password_buffer[output_index + 1u];
            if (!CharacterToUsage(character, &modifier, &usage))
            {
                output_finished = 1;
                return;
            }
            keyboard_report[0] = modifier;
            keyboard_report[2] = usage;
            output_index++;
            output_key_down = 0;
        }
        else if (!output_enter_sent)
        {
            keyboard_report[2] = 0x28; /* Enter */
            output_enter_sent = 1;
            output_key_down = 0;
        }
        else
        {
            output_finished = 1;
            return;
        }
    }
    else
        output_key_down = 1;

    usb_in_handle = HIDTxPacket(HID_EP, keyboard_report, 8);
}

static void ProcessIO(void)
{
    ProcessTimers();

    if ((USBDeviceState < CONFIGURED_STATE) || USBSuspendControl)
        return;

    if (!HIDRxHandleBusy(usb_out_handle))
        usb_out_handle = HIDRxPacket(HID_EP, keyboard_out_report,
                                      HID_INT_OUT_EP_SIZE);

    if (raw_ready && output_active && !HIDTxHandleBusy(usb_in_handle))
        AbortPasswordOutput();
    if (raw_ready && !output_active && !output_cancel_release)
        ProcessReceivedFrame();

    ServiceKeyboardOutput();
}

void main(void)
{
    InitializeSystem();
    USBDeviceAttach();
    INTCONbits.GIEH = 1;
    INTCONbits.GIEL = 1;

    while (1)
    {
        USBDeviceTasks();
        ProcessIO();
    }
}
