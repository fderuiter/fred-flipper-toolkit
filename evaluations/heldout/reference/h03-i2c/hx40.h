// Reviewer-side reference solution for FRED held-out task H03. MIT licensed, original.
// Pure C99 core for the synthetic HX-40 sensor: CRC-8 word check, conversion,
// and the stuck-bus recovery policy. The FAP wrapper performs the bus I/O and
// the open-drain SCL clocking; this core only decides what to do next.
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define HX40_ADDR_7BIT          0x44u
#define HX40_CMD_MEASURE        0xF5u
#define HX40_TIMEOUTS_BEFORE_RECOVERY 2u
#define HX40_MAX_RECOVERIES     3u /* consecutive, before declaring the bus stuck */
#define HX40_STUCK_RETRY_POLLS  15u /* while stuck, try once every 15 polls (30 s) */

typedef struct {
    int32_t rh_centi; /* 0.01 % */
    int32_t t_centi; /* 0.01 degC */
} Hx40Reading;

typedef enum {
    Hx40Ok,
    Hx40CrcError,
} Hx40Decode;

uint8_t hx40_crc8(const uint8_t* data, uint32_t len);
Hx40Decode hx40_decode(const uint8_t frame[6], Hx40Reading* out);

typedef enum {
    Hx40EventReading, /* decoded frame, CRC ok */
    Hx40EventCrcError,
    Hx40EventNack, /* sensor busy or absent: retry next poll, no recovery */
    Hx40EventTimeout, /* bus timeout / SDA low */
} Hx40Event;

typedef enum {
    Hx40ActionNone,
    Hx40ActionRecover, /* clock SCL (<= 9 pulses, open-drain), STOP, re-init bus */
    Hx40ActionSkipPoll, /* stuck: do not touch the bus this poll */
} Hx40Action;

typedef struct {
    uint32_t consecutive_timeouts;
    uint32_t consecutive_recoveries;
    uint32_t stuck_polls;
    bool stuck; /* user-visible */
    uint32_t readings, crc_errors, nacks, timeouts, recoveries;
} Hx40Policy;

void hx40_policy_init(Hx40Policy* p);
/* Called before each poll; returns Hx40ActionSkipPoll while backing off. */
Hx40Action hx40_policy_before_poll(Hx40Policy* p);
/* Called with each transaction outcome; returns the follow-up action. */
Hx40Action hx40_policy_on_event(Hx40Policy* p, Hx40Event e);
