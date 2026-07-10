#include "gprs_response_parser.h"
#include <string.h>
#include <stdlib.h>

/* ------------------------------------------------------------------ */
/* CSQ / generic response classification                               */
/* ------------------------------------------------------------------ */

gprs_response_result_t gprs_classify_response(const char *received, const char *expected)
{
    if (received == NULL || received[0] == '\0') {
        return GPRS_RESP_NONE;
    }
    if (strstr(received, expected) != NULL) {
        return GPRS_RESP_MATCHED;
    }
    return GPRS_RESP_UNMATCHED;
}

int gprs_parse_csq(const char *received, const char *csq_marker, int *out_csq)
{
    const char *ptr;
    int csq;

    if (received == NULL) {
        return 0;
    }
    ptr = strstr(received, csq_marker);
    if (ptr == NULL) {
        return 0;
    }
    if (strlen(ptr) < 7) {
        return 0;
    }
    if (ptr[5] < '0' || ptr[5] > '9' || ptr[6] < '0' || ptr[6] > '9') {
        return 0;
    }

    csq = (ptr[5] - '0') * 10 + (ptr[6] - '0');
    if (csq > 0 && csq < 32) {
        if (out_csq != NULL) {
            *out_csq = csq;
        }
        return 1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* ProcessRemoteConfigSettings                                          */
/* ------------------------------------------------------------------ */

int rcfg_process_buffer(const uint8_t *buf, size_t buf_len,
                         rcfg_event_cb cb, void *cb_ctx,
                         int *out_mark_client_outreach)
{
    const uint8_t *s = buf;
    int iLen, i;
    size_t rewind_iterations;

    if (out_mark_client_outreach != NULL) {
        *out_mark_client_outreach = 0;
    }
    if (buf_len < 3) {
        return 0;
    }

    s += 1;
    iLen = ((int)s[0] << 8) & 0xFF00;
    s += 1;
    iLen += (int)s[0] & 0xFF;
    s += 1;

    if (iLen == 6) {
        s += 6;
        i = 7;
    } else {
        i = 4;
    }

    if (iLen == 0) {
        if (out_mark_client_outreach != NULL) {
            *out_mark_client_outreach = 1;
        }
    }

    while (iLen > 0 && i < iLen - 2) {
        uint8_t b0;
        rcfg_event_t ev;

        if ((size_t)(s - buf) >= buf_len) {
            break;
        }
        b0 = s[0];
        memset(&ev, 0, sizeof(ev));

        if (b0 >= 0x0C && b0 <= 0x13) {
            ev.type = RCFG_EVT_NOOP; ev.noop_command_byte = b0;
            if (cb != NULL) cb(cb_ctx, &ev);
            s += 2; i += 2;
        }
        else if (b0 == 0x21) {
            ev.type = RCFG_EVT_BEEPER_SET;
            ev.beeper_on = (s[1] != 0) ? 1 : 0;
            if (cb != NULL) cb(cb_ctx, &ev);
            s += 2; i += 2;
        }
        else if (b0 == 0x1E) {
            ev.type = RCFG_EVT_NOOP; ev.noop_command_byte = b0;
            if (cb != NULL) cb(cb_ctx, &ev);
            s += 3; i += 3;
        }
        else if (b0 == 0x1D) {
            ev.type = RCFG_EVT_NOOP; ev.noop_command_byte = b0;
            if (cb != NULL) cb(cb_ctx, &ev);
            s += 2; i += 2;
        }
        else if (b0 == 0x18) {
            ev.type = RCFG_EVT_NOOP; ev.noop_command_byte = b0;
            if (cb != NULL) cb(cb_ctx, &ev);
            s += 3; i += 3;
        }
        else if (b0 == 0x19) {
            ev.type = RCFG_EVT_NOOP; ev.noop_command_byte = b0;
            if (cb != NULL) cb(cb_ctx, &ev);
            s += 3; i += 3;
        }
        else if (b0 == 0x14 || b0 == 0x15) {
            ev.type = RCFG_EVT_NOOP; ev.noop_command_byte = b0;
            if (cb != NULL) cb(cb_ctx, &ev);
            s += 2; i += 2;
        }
        else if (b0 == 0x1F) {
            ev.type = RCFG_EVT_NOOP; ev.noop_command_byte = b0;
            if (cb != NULL) cb(cb_ctx, &ev);
            s += 2; i += 2;
        }
        else if (b0 == 0x26 || b0 == 0x27) {
            ev.type = RCFG_EVT_NOOP; ev.noop_command_byte = b0;
            if (cb != NULL) cb(cb_ctx, &ev);
            s += 2; i += 2;
        }
        else if (b0 == 0x53) {
            ev.type = RCFG_EVT_STOP_READING;
            if (cb != NULL) cb(cb_ctx, &ev);
            s += 1; i += 1;
        }
        else if (b0 == 0x52) {
            ev.type = RCFG_EVT_START_READING;
            if (cb != NULL) cb(cb_ctx, &ev);
            s += 1; i += 1;
        }
        else if (b0 == 0x2E) {
            ev.type = RCFG_EVT_SEND_TO_REMOTE_SET;
            ev.send_to_remote_value = s[1];
            if (cb != NULL) cb(cb_ctx, &ev);
            s += 2; i += 2;
        }
        else if (b0 == 0x39 || b0 == 0x38) {
            ev.type = RCFG_EVT_REWIND;
            ev.is_remote_rewind = (b0 == 0x39) ? 1 : 0;
            ev.rewind_data = s;
            ev.rewind_data_len = buf_len - (size_t)(s - buf);
            if (cb != NULL) cb(cb_ctx, &ev);

            rewind_iterations = 0;
            do {
                s += 1; i += 1; rewind_iterations += 1;
                if ((size_t)(s - buf) >= buf_len) {
                    break;
                }
            } while (s[0] != 0x03 && rewind_iterations < 50);
        }
        else {
            i += iLen;
        }
    }

    return iLen;
}

/* ------------------------------------------------------------------ */
/* Remote_CheckForResponse dispatch                                     */
/* ------------------------------------------------------------------ */

void gprs_process_response_buffer(const uint8_t *buf, size_t len,
                                   gprs_rx_event_cb cb, void *cb_ctx)
{
    size_t i = 0;

    while (i < len) {
        if (buf[i] == 0x03) {
            gprs_rx_event_t ev;
            size_t declared_total;

            memset(&ev, 0, sizeof(ev));
            ev.type = GPRS_RX_CONFIG_RECORD;
            ev.config_record_data = &buf[i];
            ev.config_record_len = len - i;
            if (cb != NULL) {
                cb(cb_ctx, &ev);
            }

            if (len - i >= 3) {
                int declared_len = ((int)buf[i + 1] << 8) | (int)buf[i + 2];
                declared_total = 3 + (size_t)(declared_len > 0 ? declared_len : 0);
                if (declared_total < 6) {
                    declared_total = 6;
                }
            } else {
                declared_total = 6;
            }
            if (i + declared_total > len) {
                declared_total = len - i;
            }
            i += declared_total;
        }
        else if (i + 1 < len && buf[i] == 0x4F && buf[i + 1] == 0x4B) {
            gprs_rx_event_t ev;
            const char *digits = (const char *)&buf[i + 2];
            size_t digit_len = 0;
            size_t max_digits = len - (i + 2);
            long val;
            char numbuf[16];

            while (digit_len < max_digits && digit_len < sizeof(numbuf) - 1
                   && digits[digit_len] >= '0' && digits[digit_len] <= '9') {
                numbuf[digit_len] = digits[digit_len];
                digit_len++;
            }
            numbuf[digit_len] = '\0';
            val = (digit_len > 0) ? atol(numbuf) : 0;

            memset(&ev, 0, sizeof(ev));
            ev.type = GPRS_RX_OK_ACK;

            if (val > 0) {
                ev.ack_valid = 1;
                ev.ack_record_no = (uint32_t)(val + 1);
                if (cb != NULL) {
                    cb(cb_ctx, &ev);
                }
                i += 2 + digit_len;
            } else {
                ev.ack_valid = 0;
                if (cb != NULL) {
                    cb(cb_ctx, &ev);
                }
                i += 11;
            }
        }
        else {
            i++;
        }
    }
}
