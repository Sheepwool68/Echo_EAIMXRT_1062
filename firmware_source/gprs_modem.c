#include "gprs_modem.h"
#include <string.h>

static void send_at(gprs_modem_t *m, const char *cmd)
{
    m->transport->write(m->transport->ctx, (const uint8_t *)cmd, strlen(cmd));
}

void gprs_modem_init(gprs_modem_t *m, const gprs_transport_t *t)
{
    memset(m, 0, sizeof(*m));
    m->transport = t;
}

void gprs_modem_toggle(gprs_modem_t *m, int remote_enabled)
{
    const gprs_transport_t *t = m->transport;

    if (remote_enabled) {
        t->set_wake_pin(t->ctx, 0);
        t->set_power_enable(t->ctx, 1);
        t->delay_ms(t->ctx, 300);
        t->open(t->ctx, 115200);
        t->flush_rx(t->ctx);
        t->flush_tx(t->ctx);

        send_at(m, "AT\r");
        send_at(m, "ATE0\r");
        send_at(m, "AT&D1\r");
        send_at(m, "AT+QCFG=\"ledmode\",0\r");
    } else {
        t->open(t->ctx, 115200);
        t->flush_rx(t->ctx);
        t->flush_tx(t->ctx);

        if (m->gprs_state == GPRS_STATE_CONNECTED) {
            t->delay_ms(t->ctx, 1050);
            gprs_modem_disconnect(m, 1);
            t->delay_ms(t->ctx, 1050);
            send_at(m, "AT+QICLOSE=0\r");
        }
        t->delay_ms(t->ctx, 100);
        send_at(m, "AT+QCFG=\"ledmode\",2\r");
        t->set_wake_pin(t->ctx, 1);
        t->delay_ms(t->ctx, 200);
        send_at(m, "AT+QSCLK=1\r");
        t->close(t->ctx);
        m->gprs_state = GPRS_STATE_NOGPRS;
        t->set_power_enable(t->ctx, 0);
    }
}

void gprs_modem_disconnect(gprs_modem_t *m, int remote_type)
{
    const gprs_transport_t *t = m->transport;

    if (remote_type <= 1) {
        uint8_t plus = '+';
        int i;

        t->flush_tx(t->ctx);
        t->flush_rx(t->ctx);

        for (i = 1; i < 4; i++) {
            t->write(t->ctx, &plus, 1);
        }
    }

    m->gprs_state = GPRS_STATE_DISCONNECTED;
}

uint32_t gprs_modem_set_error(gprs_modem_t *m, int remote_type, uint32_t gprs_base_record)
{
    if (remote_type == 1) {
        gprs_modem_disconnect(m, remote_type);
        m->transport->set_power_enable(m->transport->ctx, 0);
        m->gprs_state = GPRS_STATE_DISCONNECTED;
    } else {
        m->gprs_state = GPRS_STATE_NOGPRS;
    }
    m->gprs_status = GPRS_STATUS_ER3;
    return gprs_base_record;
}

gprs_response_result_t gprs_modem_read_response(gprs_modem_t *m, const char *expected,
                                                 char *buf, size_t buf_size,
                                                 uint32_t timeout_ms)
{
    int n;

    if (buf_size == 0) {
        return GPRS_RESP_NONE;
    }
    buf[0] = '\0';

    n = m->transport->read(m->transport->ctx, (uint8_t *)buf, buf_size - 1, timeout_ms);
    if (n <= 0) {
        return GPRS_RESP_NONE;
    }
    buf[n] = '\0';

    return gprs_classify_response(buf, expected);
}
