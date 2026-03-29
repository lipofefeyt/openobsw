#include "obsw/obsw.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>

static void sim_responder(uint8_t flags, const obsw_tc_t *tc, void *ctx)
{
    (void)ctx;
    fprintf(stdout, "ACK apid=0x%03X svc=%u subsvc=%u flags=0x%02X seq=%u\n",
            tc->apid, tc->service, tc->subservice, flags, tc->seq_count);
    fflush(stdout);
}

static int handle_s17_ping(const obsw_tc_t *tc,
                            obsw_tc_responder_t respond,
                            void *ctx)
{
    (void)ctx;
    fprintf(stderr, "[OBSW] S17/1 are-you-alive received on APID 0x%03X\n",
            tc->apid);
    respond(OBSW_TC_ACK_ACCEPT | OBSW_TC_ACK_COMPLETE, tc, NULL);
    return 0;
}

static obsw_tc_route_t routes[] = {
    { .apid = 0xFFFF, .service = 17, .subservice = 1,
      .handler = handle_s17_ping, .ctx = NULL },
};

int main(void)
{
    obsw_tc_dispatcher_t dispatcher;
    obsw_tc_dispatcher_init(&dispatcher,
                             routes, sizeof(routes) / sizeof(routes[0]),
                             sim_responder, NULL);

    fprintf(stderr, "[OBSW] Host sim started. Awaiting TC frames on stdin.\n");

    uint8_t frame[1024];
    while (1) {
        uint8_t len_buf[2];
        if (fread(len_buf, 1, 2, stdin) != 2) break;

        uint16_t frame_len = (uint16_t)((len_buf[0] << 8) | len_buf[1]);
        if (frame_len == 0 || frame_len > sizeof(frame)) break;

        if (fread(frame, 1, frame_len, stdin) != frame_len) break;

        int rc = obsw_tc_dispatcher_feed(&dispatcher, frame, frame_len);
        if (rc == OBSW_TC_ERR_NO_ROUTE)
            fprintf(stderr, "[OBSW] No route for incoming TC\n");
        else if (rc != OBSW_TC_OK)
            fprintf(stderr, "[OBSW] Dispatcher error: %d\n", rc);
    }

    fprintf(stderr, "[OBSW] Sim terminated.\n");
    return 0;
}
