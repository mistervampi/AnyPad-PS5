/* End-to-end runs of the host stack against the simulated controller. */
#include "sim.h"
#include "../src/crc32.h"
#include "../src/host.h"
#include "../src/util.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

int sdp_find_ids(const unsigned char *p, int n, uint16_t *vid, uint16_t *pid);

static int g_fail;
#define CHECK(c) do { if (!(c)) { printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); g_fail++; } } while (0)

typedef struct {
    int connects, disconnects, states, slot;
    pad_info info;
    pad_state last;
} seen;

static void on_connect(void *ud, int slot, const pad_info *info)
{
    seen *s = ud;
    s->connects++;
    s->slot = slot;
    s->info = *info;
}

static void on_state(void *ud, int slot, const pad_state *st)
{
    seen *s = ud;
    (void)slot;
    s->states++;
    s->last = *st;
}

static void on_disconnect(void *ud, int slot)
{
    seen *s = ud;
    (void)slot;
    s->disconnects++;
}

static const char *DB = "build/test_pads.db";

/* A DualShock 4 full report 0x11: cross and dpad-up held, left stick at
 * (0x20, 0xE0), L2 half pressed. */
static int ds4_report(unsigned char *r)
{
    memset(r, 0, 78);
    r[0] = 0x11;
    r[1] = 0xC0;
    r[3] = 0x20; r[4] = 0xE0; r[5] = 0x80; r[6] = 0x80;
    r[7] = 0x20 | 0x00;     /* cross, hat north */
    r[10] = 0x80;
    r[32] = 0x08;           /* battery 8/8, no cable */
    return 78;
}

static void run(host_t *h, int ms)
{
    long end = sim_now() + ms;
    while (sim_now() < end) host_poll(h, 5);
}

static host_t *open_host(sim_t *sim, seen *s)
{
    host_events ev = { on_connect, on_state, on_disconnect, s, NULL };
    return host_open(sim_hci(sim), DB, &ev);
}

static int test_pairing_seed(int drop_every, unsigned seed, int quiet);
static void test_pairing(int drop_every) { test_pairing_seed(drop_every, 0, 0); }

static int test_pairing_seed(int drop_every, unsigned seed, int quiet)
{
    static const unsigned char addr[6] = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06 };
    sim_pad pad;
    sim_t *sim;
    host_t *h;
    seen s;
    unsigned char out[128];
    int n;

    int fail_before = g_fail;

    if (!quiet) printf("pairing a DualShock 4 (dropping every %d packets)\n", drop_every);
    unlink(DB);
    memset(&pad, 0, sizeof pad);
    memset(&s, 0, sizeof s);
    pad.mode = SIM_WAIT_PAIRING;
    memcpy(pad.addr, addr, 6);
    pad.cod = 0x002508;
    pad.vid = 0x054C;
    pad.pid = 0x05C4;
    pad.report_len = ds4_report(pad.report);
    pad.needs_output = 1;
    pad.drop_every = drop_every;
    pad.seed = seed;

    sim = sim_new(&pad);
    h = open_host(sim, &s);
    CHECK(h != NULL);
    if (!h) return 0;

    host_pair(h, 30);
    run(h, 15000);

    CHECK(s.connects == 1);
    CHECK(s.info.vid == 0x054C && s.info.pid == 0x05C4);
    CHECK(s.info.profile && strcmp(s.info.profile, "DualShock 4") == 0);
    CHECK(sim_sdp_queries(sim) >= 1);
    CHECK(host_paired_count(h) == 1);
    CHECK(sim_outputs(sim) >= 1);           /* switched it to report 0x11 */
    CHECK(s.states > 0);
    CHECK(s.last.buttons & 0x4000);         /* cross */
    CHECK(s.last.buttons & 0x10);           /* up */
    CHECK(s.last.lx == 0x20 && s.last.ly == 0xE0);
    CHECK(s.last.l2 == 0x80 && (s.last.buttons & 0x100));
    CHECK(s.last.has_battery && s.last.battery_pct == 100);

    /* Rumble and light bar reach the pad, with a CRC it accepts. */
    {
        pad_output o = { 200, 100, 1, 2, 3, 1 };
        CHECK(host_set_output(h, s.slot, &o));
        n = sim_last_output(sim, out, sizeof out);
        CHECK(n == 79);
        CHECK(out[0] == 0xA2 && out[1] == 0x11);
        CHECK(out[7] == 100 && out[8] == 200);
        CHECK(out[9] == 1 && out[10] == 2 && out[11] == 3);
        CHECK(crc32_calc(out, 75) == ((uint32_t)out[75] | (uint32_t)out[76] << 8 |
                                      (uint32_t)out[77] << 16 | (uint32_t)out[78] << 24));
    }

    /* Input changes are followed. */
    pad.report[7] = 0x08 | 0x10;            /* square, hat centred */
    sim_set_report(sim, pad.report, pad.report_len);
    run(h, 200);
    CHECK((s.last.buttons & 0x8000) && !(s.last.buttons & 0x4000));
    CHECK(!(s.last.buttons & 0x10));

    sim_disconnect(sim);
    run(h, 100);
    CHECK(s.disconnects == 1);
    CHECK(!host_connected(h, s.slot));
    if (drop_every && !quiet) printf("  (%d packets dropped)\n", sim_dropped(sim));

    host_close(h);
    sim_free(sim);
    return g_fail == fail_before;
}

/* Many runs with different loss patterns: how often does pairing succeed? */
static void test_pairing_stress(void)
{
    int ok = 0, runs = 40, before = g_fail;
    unsigned seed;

    printf("pairing under 1-in-4 packet loss, %d runs\n", runs);
    for (seed = 1; seed <= (unsigned)runs; seed++) ok += test_pairing_seed(4, seed * 7919, 1);
    printf("  %d/%d runs fully passed\n", ok, runs);
    g_fail = before + (ok < runs);
}

static void test_reconnect(void)
{
    static const unsigned char addr[6] = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06 };
    sim_pad pad;
    sim_t *sim;
    host_t *h;
    seen s;

    printf("a paired DualShock 4 reconnecting by itself\n");
    memset(&pad, 0, sizeof pad);
    memset(&s, 0, sizeof s);
    pad.mode = SIM_RECONNECT;
    memcpy(pad.addr, addr, 6);
    pad.cod = 0x002508;
    pad.vid = 0x054C;
    pad.pid = 0x05C4;
    pad.report_len = ds4_report(pad.report);
    pad.needs_output = 1;

    /* The pad keeps the key it got when it was paired in the test before. */
    {
        FILE *f = fopen(DB, "rb");
        unsigned char rec[8 + 64];      /* magic, then the first record */
        int ok = f && fread(rec, 1, sizeof rec, f) == sizeof rec;
        if (f) fclose(f);
        CHECK(ok);
        if (!ok) return;
        sim = sim_new(&pad);
        sim_give_key(sim, rec + 8 + 6);
    }
    h = open_host(sim, &s);
    CHECK(h != NULL);
    if (!h) return;

    sim_power_on(sim);
    run(h, 8000);
    CHECK(s.connects == 1);
    CHECK(sim_sdp_queries(sim) == 0);       /* ids came from the store */
    CHECK(s.states > 0 && (s.last.buttons & 0x4000));

    host_close(h);
    sim_free(sim);
}

/* An earlier run left its link to the pad open on the chip: paging the pad
 * answers "connection already exists" until the stale link is closed. */
static void test_stale_link(void)
{
    static const unsigned char addr[6] = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06 };
    sim_pad pad;
    sim_t *sim;
    host_t *h;
    seen s;

    printf("a stale link from an earlier run is closed and the pad then pairs\n");
    unlink(DB);
    memset(&pad, 0, sizeof pad);
    memset(&s, 0, sizeof s);
    pad.mode = SIM_WAIT_PAIRING;
    memcpy(pad.addr, addr, 6);
    pad.cod = 0x002508;
    pad.vid = 0x054C;
    pad.pid = 0x05C4;
    pad.report_len = ds4_report(pad.report);
    pad.needs_output = 1;
    sim = sim_new(&pad);
    sim_stale_link(sim, 0x34);
    h = open_host(sim, &s);
    CHECK(h != NULL);
    if (!h) { sim_free(sim); return; }
    host_pair(h, 60);
    run(h, 30000);
    CHECK(sim_stale_cleared(sim) == 1);
    CHECK(s.connects == 1);
    host_close(h);
    sim_free(sim);
}

/* The chip's end-of-inquiry event can be lost: the search must carry on by
 * itself (the simulator never sends one). */
static void test_inquiry_watchdog(void)
{
    static const unsigned char addr[6] = { 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F };
    sim_pad pad;
    sim_t *sim;
    host_t *h;
    seen s;

    printf("a search whose end event is lost carries on\n");
    unlink(DB);
    memset(&pad, 0, sizeof pad);
    memset(&s, 0, sizeof s);
    pad.mode = SIM_RECONNECT;               /* no pad in pairing mode: nothing is found */
    memcpy(pad.addr, addr, 6);
    sim = sim_new(&pad);
    h = open_host(sim, &s);
    CHECK(h != NULL);
    if (!h) { sim_free(sim); return; }
    host_pair(h, 60);
    run(h, 50000);
    CHECK(sim_inquiries(sim) >= 3);         /* started again after the watchdog, not once and stuck */
    host_close(h);
    sim_free(sim);
}

static void test_unknown_pad_refused(void)
{
    static const unsigned char addr[6] = { 0x11, 0x12, 0x13, 0x14, 0x15, 0x16 };
    sim_pad pad;
    sim_t *sim;
    host_t *h;
    seen s;

    printf("a stranger connecting is ignored\n");
    memset(&pad, 0, sizeof pad);
    memset(&s, 0, sizeof s);
    pad.mode = SIM_RECONNECT;
    memcpy(pad.addr, addr, 6);
    pad.cod = 0x002508;
    sim = sim_new(&pad);
    h = open_host(sim, &s);
    sim_power_on(sim);
    run(h, 3000);
    CHECK(s.connects == 0);
    CHECK(!sim_link_up(sim));
    host_close(h);
    sim_free(sim);
}

static void test_transport_lost(void)
{
    static const unsigned char addr[6] = { 0x21, 0x22, 0x23, 0x24, 0x25, 0x26 };
    sim_pad pad;
    sim_t *sim;
    host_t *h;
    seen s;

    printf("a controller that stops answering is noticed\n");
    memset(&pad, 0, sizeof pad);
    memset(&s, 0, sizeof s);
    pad.mode = SIM_RECONNECT;
    memcpy(pad.addr, addr, 6);
    sim = sim_new(&pad);
    h = open_host(sim, &s);
    run(h, 30000);
    CHECK(!host_transport_lost(h));     /* idle but answering probes */
    sim_go_deaf(sim);
    run(h, 30000);
    CHECK(host_transport_lost(h));
    host_close(h);
    sim_free(sim);
}

/* A pad no profile knows, paired and read through its HID descriptor. */
static const unsigned char k_desc[] = {
    0x05, 0x01, 0x09, 0x05, 0xA1, 0x01,
    0x15, 0x00, 0x26, 0xFF, 0x00, 0x75, 0x08, 0x95, 0x04,
    0x09, 0x30, 0x09, 0x31, 0x09, 0x32, 0x09, 0x35, 0x81, 0x02,
    0x15, 0x00, 0x25, 0x07, 0x75, 0x04, 0x95, 0x01, 0x09, 0x39, 0x81, 0x42,
    0x75, 0x04, 0x95, 0x01, 0x81, 0x03,
    0x05, 0x09, 0x19, 0x01, 0x29, 0x10, 0x15, 0x00, 0x25, 0x01,
    0x75, 0x01, 0x95, 0x10, 0x81, 0x02,
    0x05, 0x02, 0x15, 0x00, 0x26, 0xFF, 0x00, 0x75, 0x08, 0x95, 0x02,
    0x09, 0xC5, 0x09, 0xC4, 0x81, 0x02,
    0xC0,
};

static void test_generic_pad(void)
{
    static const unsigned char addr[6] = { 0x31, 0x32, 0x33, 0x34, 0x35, 0x36 };
    sim_pad pad;
    sim_t *sim;
    host_t *h;
    seen s;

    printf("pairing an unknown pad: generic profile from its descriptor\n");
    unlink(DB);
    memset(&pad, 0, sizeof pad);
    memset(&s, 0, sizeof s);
    pad.mode = SIM_WAIT_PAIRING;
    memcpy(pad.addr, addr, 6);
    pad.cod = 0x002508;
    pad.vid = 0x2DC8;
    pad.pid = 0x6001;
    memcpy(pad.desc, k_desc, sizeof k_desc);
    pad.desc_len = (int)sizeof k_desc;
    /* left stick right, hat south, button 1 (A), accelerator half */
    {
        static const unsigned char r[9] = { 0xFF, 0x80, 0x80, 0x80, 0x04, 0x01, 0x00, 0x00, 0x80 };
        memcpy(pad.report, r, sizeof r);
        pad.report_len = (int)sizeof r;
    }
    sim = sim_new(&pad);
    h = open_host(sim, &s);
    host_pair(h, 30);
    run(h, 10000);
    CHECK(s.connects == 1);
    CHECK(s.info.profile && strcmp(s.info.profile, "Generic HID") == 0);
    CHECK(sim_sdp_continuations(sim) >= 1);     /* the descriptor came in pieces */
    CHECK(s.states > 0);
    CHECK(s.last.lx == 255 && s.last.ly == 128);
    CHECK((s.last.buttons & 0x40) && (s.last.buttons & 0x4000));    /* down, cross */
    CHECK(s.last.r2 == 0x80 && (s.last.buttons & 0x200));
    host_close(h);
    sim_free(sim);
}

/* A Switch Pro controller: simple mode until its wake-up sequence. */
static void test_switch_pad(void)
{
    static const unsigned char addr[6] = { 0x41, 0x42, 0x43, 0x44, 0x45, 0x46 };
    sim_pad pad;
    sim_t *sim;
    host_t *h;
    seen s;

    printf("pairing a Switch Pro controller: woken up into its full report\n");
    memset(&pad, 0, sizeof pad);
    memset(&s, 0, sizeof s);
    pad.mode = SIM_WAIT_PAIRING;
    memcpy(pad.addr, addr, 6);
    pad.cod = 0x002508;
    pad.vid = 0x057E;
    pad.pid = 0x2009;
    pad.needs_output = 1;
    pad.basic[0] = 0x3F;                    /* simple mode: nothing pressed */
    pad.basic[3] = 8;
    pad.basic_len = 12;
    memset(pad.report, 0, 49);
    pad.report[0] = 0x30;
    pad.report[3] = 0x08;                   /* A */
    pad.report[6] = 0x00; pad.report[7] = 0x08; pad.report[8] = 0x80;  /* centred */
    pad.report[9] = 0x00; pad.report[10] = 0x08; pad.report[11] = 0x80;
    pad.report_len = 49;
    sim = sim_new(&pad);
    h = open_host(sim, &s);
    host_pair(h, 30);
    run(h, 10000);
    CHECK(s.connects == 1);
    CHECK(s.info.profile && strcmp(s.info.profile, "Switch Pro") == 0);
    CHECK(sim_outputs(sim) >= 4);           /* the whole wake-up sequence */
    CHECK(s.last.buttons & 0x2000);         /* A, from the full report: circle */
    CHECK(s.last.lx == 128 && s.last.ly == 128);
    host_close(h);
    sim_free(sim);
}

/* What the host changes in the shared controller, it puts back on close,
 * unless someone else changed it meanwhile. */
static void test_restore(void)
{
    static const unsigned char addr[6] = { 0x61, 0x62, 0x63, 0x64, 0x65, 0x66 };
    sim_pad pad;
    sim_t *sim;
    host_t *h;
    seen s;

    printf("the controller's page scan is put back on close, unless changed by another\n");
    memset(&pad, 0, sizeof pad);
    memset(&s, 0, sizeof s);
    pad.mode = SIM_RECONNECT;
    memcpy(pad.addr, addr, 6);
    sim = sim_new(&pad);
    h = open_host(sim, &s);
    CHECK(h && sim_scan(sim) == 0x02);
    host_close(h);
    CHECK(sim_scan(sim) == 0x00);

    h = open_host(sim, &s);
    sim_set_scan(sim, 0x03);            /* the system turns on inquiry scan too */
    host_close(h);
    CHECK(sim_scan(sim) == 0x03);       /* left alone */
    sim_free(sim);
}

/* Garbage on every channel of a working link, and garbage events: the
 * host must neither fault nor lose its pad over it. */
static void test_garbage(void)
{
    static const unsigned char addr[6] = { 0x71, 0x72, 0x73, 0x74, 0x75, 0x76 };
    static const unsigned cids[] = { 0x0001, 0x0040, 0x0041, 0x0042, 0x0099 };
    static const unsigned char codes[] = { 0x03, 0x05, 0x06, 0x08, 0x0E, 0x0F, 0x13, 0x17, 0x18, 0x22, 0x2F, 0x31, 0x3E };
    unsigned seed = 777;
    sim_pad pad;
    sim_t *sim;
    host_t *h;
    seen s;
    int i;

    printf("garbage frames and events on a working Classic link\n");
    unlink(DB);
    memset(&pad, 0, sizeof pad);
    memset(&s, 0, sizeof s);
    pad.mode = SIM_WAIT_PAIRING;
    memcpy(pad.addr, addr, 6);
    pad.cod = 0x002508;
    pad.vid = 0x054C;
    pad.pid = 0x05C4;
    pad.report_len = ds4_report(pad.report);
    pad.needs_output = 1;
    sim = sim_new(&pad);
    h = open_host(sim, &s);
    host_pair(h, 30);
    run(h, 8000);
    CHECK(s.connects == 1);
    for (i = 0; i < 20000; i++) {
        unsigned char junk[300];
        int n, k;
        seed = seed * 1103515245u + 12345u;
        n = (int)((seed >> 8) % 280);
        for (k = 0; k < n; k++) {
            seed = seed * 1103515245u + 12345u;
            junk[k] = (unsigned char)(seed >> 16);
        }
        if (i % 4 == 3) sim_inject_event(sim, codes[(seed >> 4) % sizeof codes], junk, n < 255 ? n : 255);
        else sim_inject_acl(sim, cids[(seed >> 4) % 5], junk, n);
        if (i % 16 == 0) host_poll(h, 1);
    }
    run(h, 500);
    printf("  connects %d, disconnects %d, connected now %d\n", s.connects, s.disconnects,
           host_connected(h, s.slot));
    /* Garbage events can end the link (a Disconnection Complete for our
     * handle looks like any other); the books must still balance. */
    CHECK(host_connected(h, s.slot) == (s.connects - s.disconnects == 1));
    host_close(h);
    sim_free(sim);
}

/* A controller that never answers: the host gives up at once, and asks the
 * transport once why. */
static void test_dead_controller(void)
{
    static const unsigned char addr[6] = { 0x91, 0x92, 0x93, 0x94, 0x95, 0x96 };
    sim_pad pad;
    sim_t *sim;
    host_t *h;
    seen s;
    int before = sim_diag_calls();

    printf("a controller that does not answer: host_open fails and the transport reports\n");
    memset(&pad, 0, sizeof pad);
    memset(&s, 0, sizeof s);
    pad.mode = SIM_RECONNECT;
    memcpy(pad.addr, addr, 6);
    sim = sim_new(&pad);
    sim_go_deaf(sim);
    h = open_host(sim, &s);
    CHECK(h == NULL);
    CHECK(sim_diag_calls() == before + 1);
    sim_free(sim);
}

static void test_sdp_parse(void)
{
    /* As a real pad answers: {{0x0200 ver, 0x0201 vid, 0x0202 pid}} */
    static const unsigned char list[] = {
        0x35, 0x14, 0x35, 0x12,
        0x09, 0x02, 0x00, 0x09, 0x01, 0x03,
        0x09, 0x02, 0x01, 0x09, 0x05, 0x4C,
        0x09, 0x02, 0x02, 0x09, 0x0C, 0xE6,
    };
    uint16_t vid = 0, pid = 0;

    printf("SDP Device ID parsing\n");
    CHECK(sdp_find_ids(list, sizeof list, &vid, &pid));
    CHECK(vid == 0x054C && pid == 0x0CE6);
    CHECK(!sdp_find_ids(list, 10, &vid, &pid));
}

int main(void)
{
    test_sdp_parse();
    test_pairing(0);
    test_reconnect();
    test_pairing(7);
    test_stale_link();
    test_inquiry_watchdog();
    test_unknown_pad_refused();
    test_transport_lost();
    test_restore();
    test_dead_controller();
    test_garbage();
    test_generic_pad();
    test_switch_pad();
    test_pairing_stress();
    printf(g_fail ? "%d check(s) failed\n" : "all host checks passed\n", g_fail);
    return g_fail != 0;
}
