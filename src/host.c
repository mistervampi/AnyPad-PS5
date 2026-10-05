#include "host_int.h"
#include "log.h"
#include "util.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const pad_output k_player_colours[HOST_MAX_PADS] = {
    { 0, 0, 0x00, 0x00, 0x40, 1 },      /* blue */
    { 0, 0, 0x40, 0x00, 0x00, 2 },      /* red */
    { 0, 0, 0x00, 0x40, 0x00, 3 },      /* green */
    { 0, 0, 0x20, 0x00, 0x20, 4 },      /* pink */
};


/* ---- helpers -------------------------------------------------------------- */

const char *addr_str(const unsigned char *a)
{
    static char buf[4][18];
    static int which;
    char *s = buf[which++ & 3];

    snprintf(s, 18, "%02X:%02X:%02X:%02X:%02X:%02X", a[5], a[4], a[3], a[2], a[1], a[0]);
    return s;
}

int send_cmd(host_t *h, unsigned op, const void *p, int len)
{
    return h->hci.ops->cmd(h->hci.ctx, op, p, len);
}

int slot_of(host_t *h, const link_t *l) { return (int)(l - h->links); }

link_t *link_by_addr(host_t *h, const unsigned char *a)
{
    int i;

    for (i = 0; i < HOST_MAX_PADS; i++)
        if (h->links[i].used && memcmp(h->links[i].addr, a, 6) == 0) return &h->links[i];
    return NULL;
}

static link_t *link_by_handle(host_t *h, unsigned handle)
{
    int i;

    handle &= 0x0FFF;
    for (i = 0; i < HOST_MAX_PADS; i++)
        if (h->links[i].used && h->links[i].connected && h->links[i].handle == handle)
            return &h->links[i];
    return NULL;
}

static void chan_init(chan *c, unsigned psm, unsigned scid)
{
    memset(c, 0, sizeof *c);
    c->psm = psm;
    c->scid = scid;
}

link_t *link_new(host_t *h, const unsigned char *addr)
{
    int i;

    for (i = 0; i < HOST_MAX_PADS; i++) {
        link_t *l = &h->links[i];
        if (l->used) continue;
        memset(l, 0, sizeof *l);
        l->used = 1;
        memcpy(l->addr, addr, 6);
        l->t_create = now_ms();
        chan_init(&l->sdp, PSM_SDP, CID_SDP);
        chan_init(&l->ctrl, PSM_HID_CTRL, CID_CTRL);
        chan_init(&l->intr, PSM_HID_INTR, CID_INTR);
        pad_state_reset(&l->st);
        l->out = k_player_colours[i];
        return l;
    }
    return NULL;
}

void link_free(host_t *h, link_t *l)
{
    if (l->ready && h->ev.on_disconnect) h->ev.on_disconnect(h->ev.ud, slot_of(h, l));
    memset(l, 0, sizeof *l);
}

void link_disconnect(host_t *h, link_t *l, const char *why)
{
    unsigned char p[3];

    if (!l->connected) {
        log_line("pad %s: dropped (%s)", addr_str(l->addr), why);
        link_free(h, l);
        return;
    }
    if (l->disconnecting) return;
    log_line("pad %s: disconnecting (%s)", addr_str(l->addr), why);
    put16(p, l->handle);
    p[2] = 0x13;                        /* remote user terminated */
    send_cmd(h, OP_DISCONNECT, p, 3);
    l->disconnecting = 1;
}

/* ---- paired-pad store ----------------------------------------------------- */

db_rec *db_find(host_t *h, const unsigned char *addr)
{
    int i;

    for (i = 0; i < h->ndb; i++)
        if (memcmp(h->db[i].addr, addr, 6) == 0) return &h->db[i];
    return NULL;
}

/* The store file: a magic string, then one fixed-size record per pad:
 *   addr 6, key 16, key type 1, vid 2, pid 2, le 1, addr type 1, ediv 2,
 *   rand 8, irk 16, has irk 1, padding */
void db_save(host_t *h)
{
    char tmp[300];
    FILE *f;
    int i, ok;

    /* Written beside, then renamed over: a crash leaves the old or the new
     * store, never half of one. */
    snprintf(tmp, sizeof tmp, "%s.new", h->db_path);
    f = fopen(tmp, "wb");
    if (!f) {
        log_line("store: cannot write %s: errno %d", tmp, errno);
        return;
    }
    fwrite(DB_MAGIC, 1, 8, f);
    for (i = 0; i < h->ndb; i++) {
        const db_rec *d = &h->db[i];
        unsigned char r[DB_REC];

        memset(r, 0, sizeof r);
        memcpy(r, d->addr, 6);
        memcpy(r + 6, d->key, 16);
        r[22] = d->type;
        put16(r + 23, d->vid);
        put16(r + 25, d->pid);
        r[27] = d->le;
        r[28] = d->addr_type;
        put16(r + 29, d->ediv);
        memcpy(r + 31, d->rand, 8);
        memcpy(r + 39, d->irk, 16);
        r[55] = d->has_irk;
        fwrite(r, 1, sizeof r, f);
    }
    ok = fflush(f) == 0 && !ferror(f);
    if (fclose(f) != 0) ok = 0;
    if (!ok || rename(tmp, h->db_path) != 0) {
        log_line("store: could not save %s: errno %d", h->db_path, errno);
        remove(tmp);
    }
}

static void db_load(host_t *h)
{
    FILE *f = fopen(h->db_path, "rb");
    unsigned char r[DB_REC];

    h->ndb = 0;
    if (!f) return;
    if (fread(r, 1, 8, f) != 8 || memcmp(r, DB_MAGIC, 8) != 0) {
        log_line("store: %s is not a AnyPad PS5 store, ignored", h->db_path);
        fclose(f);
        return;
    }
    while (h->ndb < DB_MAX && fread(r, 1, sizeof r, f) == sizeof r) {
        db_rec *d = &h->db[h->ndb++];
        memcpy(d->addr, r, 6);
        memcpy(d->key, r + 6, 16);
        d->type = r[22];
        d->vid = (uint16_t)le16(r + 23);
        d->pid = (uint16_t)le16(r + 25);
        d->le = r[27];
        d->addr_type = r[28];
        d->ediv = (uint16_t)le16(r + 29);
        memcpy(d->rand, r + 31, 8);
        memcpy(d->irk, r + 39, 16);
        d->has_irk = r[55];
    }
    fclose(f);
    log_line("store: %d paired pad(s)", h->ndb);
}

db_rec *db_put(host_t *h, const unsigned char *addr)
{
    db_rec *d = db_find(h, addr);

    if (d) return d;
    if (h->ndb == DB_MAX) {             /* full: forget the oldest */
        memmove(h->db, h->db + 1, sizeof h->db[0] * (DB_MAX - 1));
        h->ndb--;
    }
    d = &h->db[h->ndb++];
    memset(d, 0, sizeof *d);
    memcpy(d->addr, addr, 6);
    return d;
}

void db_forget(host_t *h, const unsigned char *addr)
{
    db_rec *d = db_find(h, addr);

    if (!d) return;
    memmove(d, d + 1, sizeof *d * (size_t)(h->ndb - (d - h->db) - 1));
    h->ndb--;
    db_save(h);
}

/* ---- ACL out ---------------------------------------------------------------- */

static void pool_expire(acl_pool *p)
{
    long now = now_ms();

    while (p->count && now - p->inflight[p->head] > INFLIGHT_MS) {
        p->head = (p->head + 1) % INFLIGHT_MAX;
        p->count--;
        if (p->credits < p->max) p->credits++;
    }
}

/* Packets the controller reports sent: their buffers are free again. */
static void pool_done(acl_pool *p, int n)
{
    int i;

    for (i = 0; i < n && p->count; i++) {
        p->head = (p->head + 1) % INFLIGHT_MAX;
        p->count--;
    }
    p->credits += n;
    if (p->credits > p->max) p->credits = p->max;
}

static acl_pool *pool_of(host_t *h, const link_t *l)
{
    return l->le && h->le_separate ? &h->lep : &h->br;
}

/* Sends one L2CAP frame, unfragmented. With no controller buffer free the
 * frame is dropped; everything sent here is retried or refreshed. */
int l2_send(host_t *h, link_t *l, unsigned cid, const unsigned char *d, int len)
{
    unsigned char pkt[HCI_PKT_MAX];
    acl_pool *pool = pool_of(h, l);
    unsigned mtu = l->le && h->le_separate ? h->le_mtu : h->acl_mtu;

    if (!l->connected) return 0;
    if (8 + len > (int)sizeof pkt || (mtu && 4 + len > (int)mtu)) {
        log_line("l2cap: %d-byte frame too large", len);
        return 0;
    }
    pool_expire(pool);
    if (pool->credits <= 0) {
        log_line("l2cap: no controller buffer free, frame dropped");
        return 0;
    }
    /* Start of a frame: auto-flushable on Classic, non-flushable on LE. */
    put16(pkt, (l->handle & 0x0FFF) | (l->le ? 0x0000 : 0x2000));
    put16(pkt + 2, (unsigned)(4 + len));
    put16(pkt + 4, (unsigned)len);
    put16(pkt + 6, cid);
    memcpy(pkt + 8, d, (size_t)len);
    if (!h->hci.ops->acl_send(h->hci.ctx, pkt, 8 + len)) return 0;

    pool->credits--;
    if (pool->count == INFLIGHT_MAX) {
        pool->head = (pool->head + 1) % INFLIGHT_MAX;
        pool->count--;
    }
    pool->inflight[(pool->head + pool->count) % INFLIGHT_MAX] = now_ms();
    pool->count++;
    return 1;
}

static int sig_send(host_t *h, link_t *l, unsigned char code, unsigned char id,
                    const unsigned char *d, int len)
{
    unsigned char p[64];

    if (len > (int)sizeof p - 4) return 0;
    p[0] = code;
    p[1] = id;
    put16(p + 2, (unsigned)len);
    if (len) memcpy(p + 4, d, (size_t)len);
    return l2_send(h, l, CID_SIGNALING, p, 4 + len);
}

static unsigned char next_sig_id(host_t *h)
{
    if (++h->sig_id == 0) h->sig_id = 1;
    return h->sig_id;
}

/* ---- L2CAP channels ------------------------------------------------------- */

static chan *chan_by_scid(link_t *l, unsigned cid)
{
    if (l->sdp.st != CH_CLOSED && l->sdp.scid == cid) return &l->sdp;
    if (l->ctrl.st != CH_CLOSED && l->ctrl.scid == cid) return &l->ctrl;
    if (l->intr.st != CH_CLOSED && l->intr.scid == cid) return &l->intr;
    return NULL;
}

static chan *chan_by_psm(link_t *l, unsigned psm)
{
    if (psm == PSM_HID_CTRL) return &l->ctrl;
    if (psm == PSM_HID_INTR) return &l->intr;
    return NULL;            /* SDP is only ever opened by us */
}

static void chan_set(chan *c, chan_state st)
{
    c->st = st;
    c->t_state = now_ms();
    if (st != CH_OPEN) c->cfg_ours_ok = c->cfg_theirs_ok = 0;
    if (st == CH_CONFIG) c->cfg_tries = 0;
}

static void chan_send_cfg(host_t *h, link_t *l, chan *c)
{
    unsigned char r[4];

    put16(r, c->dcid);
    put16(r + 2, 0);                    /* flags; no options: defaults */
    sig_send(h, l, SIG_CFG_REQ, next_sig_id(h), r, 4);
    c->t_cfg = now_ms();
    c->cfg_tries++;
}

static void chan_check_open(link_t *l, chan *c)
{
    if (c->st == CH_CONFIG && c->cfg_ours_ok && c->cfg_theirs_ok) {
        chan_set(c, CH_OPEN);
        log_line("pad %s: channel %#x open", addr_str(l->addr), c->psm);
    }
}

static void chan_connect(host_t *h, link_t *l, chan *c)
{
    unsigned char r[4];

    chan_set(c, CH_CONNECTING);
    put16(r, c->psm);
    put16(r + 2, c->scid);
    sig_send(h, l, SIG_CONN_REQ, next_sig_id(h), r, 4);
}

static void chan_close(host_t *h, link_t *l, chan *c)
{
    unsigned char r[4];

    if (c->st == CH_OPEN || c->st == CH_CONFIG) {
        put16(r, c->dcid);
        put16(r + 2, c->scid);
        sig_send(h, l, SIG_DISC_REQ, next_sig_id(h), r, 4);
    }
    chan_set(c, CH_CLOSED);
}

/* The pad closed a channel, or it never finished: without both HID
 * channels the pad is of no use. */
static void chan_lost(host_t *h, link_t *l, chan *c)
{
    chan_set(c, CH_CLOSED);
    if (c != &l->sdp && l->ready) link_disconnect(h, l, "HID channel closed");
}

static void chan_tick(host_t *h, link_t *l, chan *c, long now)
{
    if (c->st == CH_CONNECTING && now - c->t_state > T_CHAN_CONNECT) {
        log_line("pad %s: channel %#x: no connection response", addr_str(l->addr), c->psm);
        chan_set(c, CH_CLOSED);         /* opened again by the link's step */
    } else if (c->st == CH_CONFIG && now - c->t_state > T_CFG_TIMEOUT) {
        /* Only the pad can repeat its configuration request; if it went to
         * the system's driver, start the channel over. */
        log_line("pad %s: channel %#x: configuration stuck, reopening (ours %d theirs %d tries %d dcid %#x)", addr_str(l->addr), c->psm, c->cfg_ours_ok, c->cfg_theirs_ok, c->cfg_tries, c->dcid);
        chan_close(h, l, c);
    } else if (c->st == CH_CONFIG && !c->cfg_ours_ok && now - c->t_cfg >= T_CFG_RESEND) {
        if (c->cfg_tries >= CFG_TRIES) {
            log_line("pad %s: channel %#x: configuration never answered", addr_str(l->addr), c->psm);
            chan_close(h, l, c);
        } else {
            chan_send_cfg(h, l, c);
        }
    }
}

static void on_signaling(host_t *h, link_t *l, const unsigned char *d, int len)
{
    while (len >= 4) {
        unsigned char code = d[0], id = d[1], r[16];
        int clen = (int)le16(d + 2);
        const unsigned char *c = d + 4;
        chan *ch;

        if (4 + clen > len) break;

        switch (code) {
        case SIG_CONN_REQ:      /* psm, source cid */
            if (clen < 4) break;
            ch = chan_by_psm(l, le16(c));
            put16(r + 2, le16(c + 2));
            put16(r + 6, 0);
            if (!ch) {
                put16(r, 0);
                put16(r + 4, 0x0002);   /* PSM not supported */
                sig_send(h, l, SIG_CONN_RSP, id, r, 8);
                break;
            }
            /* The pad opens its own channel: take it, even over one of
             * ours still being set up. */
            chan_set(ch, CH_CONFIG);
            ch->dcid = le16(c + 2);
            put16(r, ch->scid);
            put16(r + 4, 0);            /* success */
            sig_send(h, l, SIG_CONN_RSP, id, r, 8);
            chan_send_cfg(h, l, ch);
            log_line("pad %s: accepted channel %#x", addr_str(l->addr), ch->psm);
            break;

        case SIG_CONN_RSP:      /* dcid, scid, result, status */
            if (clen < 8) break;
            ch = chan_by_scid(l, le16(c + 2));
            if (!ch || ch->st != CH_CONNECTING) break;
            if (le16(c + 4) == 1) break;            /* pending */
            if (le16(c + 4) != 0) {
                log_line("pad %s: channel %#x refused (%u)", addr_str(l->addr), ch->psm, le16(c + 4));
                chan_set(ch, CH_CLOSED);
                break;
            }
            ch->dcid = le16(c);
            chan_set(ch, CH_CONFIG);
            chan_send_cfg(h, l, ch);
            break;

        case SIG_CFG_REQ:       /* dcid (ours), flags, options */
            if (clen < 4) break;
            ch = chan_by_scid(l, le16(c));
            /* Before our connection response arrived the pad's cid is not
             * known: let the channel time out and start over. */
            if (!ch || ch->st == CH_CONNECTING) break;
            put16(r, ch->dcid);
            put16(r + 2, 0);
            put16(r + 4, 0);            /* success, options as asked */
            sig_send(h, l, SIG_CFG_RSP, id, r, 6);
            ch->cfg_theirs_ok = 1;
            chan_check_open(l, ch);
            break;

        case SIG_CFG_RSP:       /* scid (ours), flags, result */
            if (clen < 6) break;
            ch = chan_by_scid(l, le16(c));
            if (!ch) break;
            if (le16(c + 4) == 0) {
                ch->cfg_ours_ok = 1;
                chan_check_open(l, ch);
            } else {
                log_line("pad %s: channel %#x configuration result %u",
                         addr_str(l->addr), ch->psm, le16(c + 4));
            }
            break;

        case SIG_DISC_REQ:      /* dcid (ours), scid */
            if (clen < 4) break;
            sig_send(h, l, SIG_DISC_RSP, id, c, 4);
            ch = chan_by_scid(l, le16(c));
            if (ch) chan_lost(h, l, ch);
            break;

        case SIG_DISC_RSP:
            break;

        case SIG_ECHO_REQ:
            sig_send(h, l, SIG_ECHO_RSP, id, c, clen < 32 ? clen : 32);
            break;

        case SIG_INFO_REQ:
            if (clen < 2) break;
            memset(r, 0, sizeof r);
            put16(r, le16(c));
            if (le16(c) == 2) {                 /* extended features: none */
                sig_send(h, l, SIG_INFO_RSP, id, r, 8);
            } else if (le16(c) == 3) {          /* fixed channels: signaling */
                r[4] = 0x02;
                sig_send(h, l, SIG_INFO_RSP, id, r, 12);
            } else {
                put16(r + 2, 1);                /* not supported */
                sig_send(h, l, SIG_INFO_RSP, id, r, 4);
            }
            break;

        case SIG_REJECT:
            log_line("pad %s: command rejected (reason %u)", addr_str(l->addr),
                     clen >= 2 ? le16(c) : 0);
            break;

        default:
            break;
        }
        d += 4 + clen;
        len -= 4 + clen;
    }
}

/* ---- SDP client: the Device ID record ------------------------------------- */

/* One data element header: sets type and content length, returns the
 * header length or 0 if it does not fit. */
static int de_header(const unsigned char *p, int avail, int *type, int *clen)
{
    static const int fixed[5] = { 1, 2, 4, 8, 16 };
    int size;

    if (avail < 1) return 0;
    *type = p[0] >> 3;
    size = p[0] & 7;
    if (*type == 0) { *clen = 0; return 1; }
    if (size < 5) { *clen = fixed[size]; return 1 + *clen <= avail ? 1 : 0; }
    if (size == 5) { if (avail < 2) return 0; *clen = p[1]; return 2 + *clen <= avail ? 2 : 0; }
    if (size == 6) { if (avail < 3) return 0; *clen = (int)be16(p + 1); return 3 + *clen <= avail ? 3 : 0; }
    if (avail < 5) return 0;
    {   /* 32-bit length: compared unsigned, as a huge one is no int */
        uint32_t big = be32(p + 1);
        if (big > (uint32_t)(avail - 5)) return 0;
        *clen = (int)big;
    }
    return 5;
}

/* Walks an attribute list depth-first, flattening the sequences: the
 * Device ID's vendor (0x0201) and product (0x0202) are each an attribute
 * id followed by a 16-bit value. Returns 1 if both were found. */
int sdp_find_ids(const unsigned char *p, int n, uint16_t *vid, uint16_t *pid)
{
    int i = 0, want = 0, got = 0;

    while (i < n) {
        int type, clen, hl = de_header(p + i, n - i, &type, &clen);

        if (!hl) return 0;
        if (type == 6 || type == 7) {   /* sequence: step inside */
            i += hl;
            continue;
        }
        if (type == 1 && clen == 2) {
            unsigned v = be16(p + i + hl);
            if (want == 1) { *vid = (uint16_t)v; got |= 1; want = 0; }
            else if (want == 2) { *pid = (uint16_t)v; got |= 2; want = 0; }
            else if (v == 0x0201) want = 1;
            else if (v == 0x0202) want = 2;
        } else {
            want = 0;
        }
        i += hl + clen;
    }
    return got == 3;
}

/* The HID descriptor list: {{0x22, descriptor bytes}}. Finds the bytes
 * that follow a one-byte 0x22 (report descriptor type). */
static int sdp_find_descriptor(const unsigned char *p, int n, const unsigned char **desc, int *dlen)
{
    int i = 0, after_type = 0;

    while (i < n) {
        int type, clen, hl = de_header(p + i, n - i, &type, &clen);

        if (!hl) return 0;
        if (type == 6 || type == 7) {
            i += hl;
            continue;
        }
        if (type == 4 && after_type) {          /* text string: the descriptor */
            *desc = p + i + hl;
            *dlen = clen;
            return 1;
        }
        after_type = type == 1 && clen == 1 && p[i + hl] == 0x22;
        i += hl + clen;
    }
    return 0;
}

static void sdp_send_request(host_t *h, link_t *l)
{
    unsigned char req[64], body[32];
    unsigned pdu;
    int n = 5;

    if (l->sdp_phase == SDP_PHASE_SEARCH) {
        uint16_t uuid = l->sdp_query == SDP_Q_IDS ? 0x1200 : 0x1124;
        static const unsigned char prefix[] = { 0x35, 0x03, 0x19 };

        memcpy(body, prefix, sizeof prefix);
        body[3] = (unsigned char)(uuid >> 8);
        body[4] = (unsigned char)uuid;
        body[5] = 0x00;
        body[6] = SDP_MAX_HANDLES;
        pdu = 0x02;                         /* ServiceSearchRequest */
        memcpy(req + n, body, 7);
        n += 7;
    } else {
        uint32_t handle = l->sdp_handles[l->sdp_handle_idx];

        body[0] = (unsigned char)(handle >> 24);
        body[1] = (unsigned char)(handle >> 16);
        body[2] = (unsigned char)(handle >> 8);
        body[3] = (unsigned char)handle;
        body[4] = 0x03;
        body[5] = 0xF0;                     /* accept up to 1008 attribute bytes */
        body[6] = 0x35;
        body[7] = 0x05;
        body[8] = 0x0A;
        body[9] = 0x00;
        body[10] = 0x00;
        body[11] = 0xFF;
        body[12] = 0xFF;
        pdu = 0x04;                         /* ServiceAttributeRequest */
        memcpy(req + n, body, 13);
        n += 13;
    }
    req[n++] = (unsigned char)l->sdp_cont_len;
    memcpy(req + n, l->sdp_cont, (size_t)l->sdp_cont_len);
    n += l->sdp_cont_len;

    l->sdp_tid = (l->sdp_tid + 1) & 0xFFFF;
    req[0] = (unsigned char)pdu;
    req[1] = (unsigned char)(l->sdp_tid >> 8);
    req[2] = (unsigned char)l->sdp_tid;
    req[3] = (unsigned char)((n - 5) >> 8);
    req[4] = (unsigned char)(n - 5);
    l2_send(h, l, l->sdp.dcid, req, n);
    l->sdp_sent = 1;
    l->sdp_tries++;
    l->t_sdp = now_ms();
}

static void sdp_start(link_t *l, int query)
{
    l->sdp_query = query;
    l->sdp_phase = SDP_PHASE_SEARCH;
    l->sdp_sent = l->sdp_tries = 0;
    l->sdp_len = l->sdp_cont_len = 0;
    l->sdp_handle_count = l->sdp_handle_idx = 0;
}

static void sdp_log_descriptor(const link_t *l, const unsigned char *desc, int dlen)
{
    int i;

    for (i = 0; i < dlen; i += 16) {
        char line[16 * 3];
        int j, n = 0;
        int end = i + 16 < dlen ? i + 16 : dlen;

        for (j = i; j < end; j++)
            n += snprintf(line + n, sizeof line - (size_t)n, "%02x%s",
                          desc[j], j + 1 < end ? " " : "");
        log_line("pad %s: HID descriptor[%d..%d]: %s",
                 addr_str(l->addr), i, end - 1, line);
    }
}

/* A query is over: use what it brought, or what failing means. */
static void sdp_finish(host_t *h, link_t *l, int ok)
{
    int query = l->sdp_query;

    l->sdp_query = 0;
    if (query == SDP_Q_IDS) {
        uint16_t vid = 0, pid = 0;
        if (ok && sdp_find_ids(l->sdp_buf, l->sdp_len, &vid, &pid)) {
            db_rec *r = db_put(h, l->addr);
            r->vid = vid;
            r->pid = pid;
            db_save(h);
            log_line("pad %s: vendor %04x product %04x", addr_str(l->addr), vid, pid);
        } else {
            log_line("pad %s: no Device ID record, treating it as generic", addr_str(l->addr));
        }
        l->vid = vid;
        l->pid = pid;
        l->ids_known = 1;
    } else if (query == SDP_Q_DESC) {
        const unsigned char *desc = NULL;
        int dlen = 0;

        if (!ok) {
            log_line("pad %s: HID descriptor SDP query failed (%d attribute bytes)",
                     addr_str(l->addr), l->sdp_len);
            link_disconnect(h, l, "no usable HID descriptor");
        } else if (!sdp_find_descriptor(l->sdp_buf, l->sdp_len, &desc, &dlen)) {
            log_line("pad %s: SDP response has no HIDDescriptorList (%d attribute bytes)",
                     addr_str(l->addr), l->sdp_len);
            link_disconnect(h, l, "no usable HID descriptor");
        } else {
            log_line("pad %s: HID report descriptor received (%d bytes)",
                     addr_str(l->addr), dlen);
            if (generic_setup(l->pctx.bytes, desc, dlen, l->vid, l->pid, h->map_dir)) {
                l->prof = &generic_profile;
            } else {
                sdp_log_descriptor(l, desc, dlen);
                link_disconnect(h, l, "no usable HID descriptor");
            }
        }
    }
}

static void sdp_attr_complete(host_t *h, link_t *l)
{
    const unsigned char *desc = NULL;
    int dlen = 0;
    int descriptor_found = 0;

    if (l->sdp_query == SDP_Q_IDS) {
        uint16_t vid, pid;

        if (sdp_find_ids(l->sdp_buf, l->sdp_len, &vid, &pid)) {
            sdp_finish(h, l, 1);
            return;
        }
    } else if (l->sdp_query == SDP_Q_DESC) {
        descriptor_found = sdp_find_descriptor(l->sdp_buf, l->sdp_len, &desc, &dlen);
        if (descriptor_found) {
            log_line("pad %s: HID report descriptor received (%d bytes)",
                     addr_str(l->addr), dlen);
            if (generic_setup(l->pctx.bytes, desc, dlen, l->vid, l->pid, h->map_dir)) {
                l->prof = &generic_profile;
                l->sdp_query = 0;
                return;
            }
        }
    }

    if (l->sdp_handle_idx + 1 < l->sdp_handle_count) {
        l->sdp_handle_idx++;
        l->sdp_len = l->sdp_cont_len = 0;
        l->sdp_tries = 0;
        l->sdp_sent = 0;
        sdp_send_request(h, l);
        return;
    }
    if (descriptor_found) {
        l->sdp_query = 0;
        sdp_log_descriptor(l, desc, dlen);
        link_disconnect(h, l, "no usable HID descriptor");
        return;
    }
    sdp_finish(h, l, 1);
}

static void on_sdp(host_t *h, link_t *l, const unsigned char *d, int len)
{
    int bytes, cl;

    if (!l->sdp_query || !l->sdp_sent || len < 5) return;
    if (be16(d + 1) != l->sdp_tid) return;          /* an answer to an older try */
    if ((unsigned)be16(d + 3) + 5u > (unsigned)len) {
        sdp_finish(h, l, 0);
        return;
    }

    if (l->sdp_phase == SDP_PHASE_SEARCH) {
        int count, i, state;

        if (d[0] != 0x03 || len < 10) {
            sdp_finish(h, l, 0);
            return;
        }
        count = (int)be16(d + 7);
        state = 9 + count * 4;
        if (count > SDP_MAX_HANDLES || state >= len ||
            l->sdp_handle_count + count > SDP_MAX_HANDLES) {
            sdp_finish(h, l, 0);
            return;
        }
        for (i = 0; i < count; i++) {
            const unsigned char *p = d + 9 + i * 4;
            uint32_t handle = (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 |
                              (uint32_t)p[2] << 8 | p[3];
            l->sdp_handles[l->sdp_handle_count++] = handle;
        }
        cl = d[state];
        if (cl > (int)sizeof l->sdp_cont || state + 1 + cl > len) {
            sdp_finish(h, l, 0);
            return;
        }
        if (cl) {
            memcpy(l->sdp_cont, d + state + 1, (size_t)cl);
            l->sdp_cont_len = cl;
            l->sdp_tries = 0;
            sdp_send_request(h, l);
            return;
        }
        if (!l->sdp_handle_count) {
            sdp_finish(h, l, 0);
            return;
        }
        l->sdp_phase = SDP_PHASE_ATTR;
        l->sdp_handle_idx = 0;
        l->sdp_cont_len = l->sdp_len = 0;
        l->sdp_tries = 0;
        sdp_send_request(h, l);
        return;
    }

    if (d[0] != 0x05 || len < 8) {
        sdp_finish(h, l, 0);
        return;
    }
    bytes = (int)be16(d + 5);
    if (8 + bytes > len || l->sdp_len + bytes > (int)sizeof l->sdp_buf) {
        sdp_finish(h, l, 0);
        return;
    }
    memcpy(l->sdp_buf + l->sdp_len, d + 7, (size_t)bytes);
    l->sdp_len += bytes;
    cl = d[7 + bytes];
    if (cl > (int)sizeof l->sdp_cont || 8 + bytes + cl > len) {
        sdp_finish(h, l, 0);
        return;
    }
    if (cl) {
        memcpy(l->sdp_cont, d + 8 + bytes, (size_t)cl);
        l->sdp_cont_len = cl;
        l->sdp_tries = 0;
        sdp_send_request(h, l);
        return;
    }
    sdp_attr_complete(h, l);
}

/* Moves an SDP query along: channel, request, retries. */
static void sdp_step(host_t *h, link_t *l, long now)
{
    chan_tick(h, l, &l->sdp, now);
    if (l->sdp.st == CH_CLOSED) {
        chan_connect(h, l, &l->sdp);
    } else if (l->sdp.st == CH_OPEN && (!l->sdp_sent || now - l->t_sdp > T_SDP_RESEND)) {
        if (l->sdp_tries >= SDP_TRIES) {
            log_line("pad %s: SDP not answered", addr_str(l->addr));
            sdp_finish(h, l, 0);
        } else {
            sdp_send_request(h, l);
        }
    }
}

/* ---- HID ------------------------------------------------------------------ */

/* Sends an output report: buf[1..n] is the report, id first; buf[0] is
 * room for the Classic HID header. */
static int send_report(host_t *h, link_t *l, unsigned char *buf, int n)
{
    if (l->le) return le_send_report(h, l, buf + 1, n);
    buf[0] = HID_DATA_OUTPUT;
    return l2_send(h, l, l->intr.dcid, buf, n + 1);
}

static int send_output(host_t *h, link_t *l)
{
    unsigned char buf[128];
    int n;

    if (!l->ready || !l->prof->build_output) return 0;
    n = l->prof->build_output(l->pctx.bytes, &l->out, buf + 1, (int)sizeof buf - 1);
    if (n <= 0) return 0;
    l->t_out = now_ms();
    return send_report(h, l, buf, n);
}

static void on_hid(host_t *h, link_t *l, const chan *c, const unsigned char *d, int len)
{
    if (len < 1) return;
    if (c == &l->ctrl) {
        if (d[0] == HID_CONTROL_UNPLUG) link_disconnect(h, l, "virtual cable unplugged");
        return;                         /* handshakes and feature data unused */
    }
    if (d[0] != HID_DATA_INPUT || len < 2) return;
    link_input(h, l, d + 1, len - 1);
}

/* One input report, id first, from either transport. */
void link_input(host_t *h, link_t *l, const unsigned char *rep, int len)
{
    int r;

    if (!l->prof || len < 1) return;
    r = l->prof->parse(l->pctx.bytes, rep, len, &l->st);
    if (r == PARSE_FULL) l->full = 1;
    if (r != PARSE_NONE && l->ready && h->ev.on_state)
        h->ev.on_state(h->ev.ud, slot_of(h, l), &l->st);
}

void link_ready(host_t *h, link_t *l)
{
    pad_info info;

    l->ready = 1;
    memset(&info, 0, sizeof info);
    info.vid = l->vid;
    info.pid = l->pid;
    info.profile = l->prof->name;
    memcpy(info.addr, l->addr, 6);
    log_line("pad %s: ready as %s in slot %d", addr_str(l->addr), l->prof->name, slot_of(h, l));
    if (h->ev.on_connect) h->ev.on_connect(h->ev.ud, slot_of(h, l), &info);
}

/* Sends the pad's wake-up sequence one report at a time, and repeats it
 * while the pad has not switched to its full report. */
void init_tick(host_t *h, link_t *l, long now)
{
    unsigned char buf[128];
    int n;

    if (l->init_done || !l->prof->init_report) return;
    if (l->init_step < 0) {                     /* between rounds */
        if (l->full) l->init_done = 1;
        else if (now - l->t_init >= T_INIT_RETRY) l->init_step = 0;
        return;
    }
    if (now - l->t_init < T_INIT_STEP) return;

    n = l->prof->init_report(l->pctx.bytes, l->init_step, &l->out, buf + 1, (int)sizeof buf - 1);
    if (n > 0) {
        send_report(h, l, buf, n);
        l->init_step++;
        l->t_init = now;
        return;
    }
    l->init_rounds++;
    if (!l->prof->needs_full || l->full) {
        l->init_done = 1;
    } else if (l->init_rounds >= INIT_ROUNDS) {
        log_line("pad %s: never switched to its full report", addr_str(l->addr));
        l->init_done = 1;
    } else {
        l->init_step = -1;
        l->t_init = now;
    }
}

/* ---- inbound ACL ---------------------------------------------------------- */

static void on_frame(host_t *h, link_t *l, unsigned cid, const unsigned char *d, int len)
{
    chan *c;

    if (l->le) {
        le_on_frame(h, l, cid, d, len);
        return;
    }
    if (cid == CID_SIGNALING) {
        on_signaling(h, l, d, len);
        return;
    }
    c = chan_by_scid(l, cid);
    if (!c) return;
    if (c == &l->sdp) on_sdp(h, l, d, len);
    else on_hid(h, l, c, d, len);
}

static void on_acl(host_t *h, const unsigned char *pkt, int len)
{
    unsigned hdr;
    int pb, dlen;
    link_t *l;

    if (len < 4) return;
    hdr = le16(pkt);
    pb = (int)(hdr >> 12) & 3;
    dlen = (int)le16(pkt + 2);
    l = link_by_handle(h, hdr);
    if (!l || 4 + dlen > len) return;   /* not ours, or cut short */

    if (pb == 1) {                      /* continuation */
        if (!l->rx_need || l->rx_len + dlen > (int)sizeof l->rx) {
            l->rx_need = 0;
            return;
        }
        memcpy(l->rx + l->rx_len, pkt + 4, (size_t)dlen);
        l->rx_len += dlen;
    } else {                            /* start: a lost tail is abandoned */
        if (dlen < 4 || dlen > (int)sizeof l->rx) return;
        memcpy(l->rx, pkt + 4, (size_t)dlen);
        l->rx_len = dlen;
        l->rx_need = 4 + (int)le16(pkt + 4);
    }
    if (l->rx_need && l->rx_len >= l->rx_need) {
        int need = l->rx_need;
        l->rx_need = 0;
        on_frame(h, l, le16(l->rx + 2), l->rx + 4, need - 4);
    }
}

/* ---- HCI events ----------------------------------------------------------- */

static void pair_with(host_t *h, const unsigned char *rec)
{
    unsigned char p[13];
    link_t *l;

    if (link_by_addr(h, rec)) return;
    if (db_find(h, rec)) {
        log_line("pad %s: in pairing mode again, forgetting its old key", addr_str(rec));
        db_forget(h, rec);
    }
    l = link_new(h, rec);
    if (!l) {
        log_line("pairing: no free slot");
        return;
    }
    l->pairing = 1;
    if (h->inquiring) send_cmd(h, OP_INQUIRY_CANCEL, NULL, 0);
    h->inquiring = 0;

    memcpy(p, rec, 6);
    put16(p + 6, 0xCC18);               /* DM1..DH5 */
    p[8] = rec[6];                      /* page scan repetition mode */
    p[9] = 0;
    put16(p + 10, le16(rec + 11) | 0x8000);   /* clock offset, valid */
    p[12] = 1;                          /* allow role switch */
    send_cmd(h, OP_CREATE_CONNECTION, p, 13);
    log_line("pad %s: connecting to pair", addr_str(rec));
}

/* An inquiry record: addr 6, page scan mode 1, reserved 1, class 3,
 * clock offset 2, RSSI 1. */
static void on_found(host_t *h, const unsigned char *rec)
{
    uint32_t cod = (uint32_t)rec[8] | (uint32_t)rec[9] << 8 | (uint32_t)rec[10] << 16;

    if (now_ms() >= h->pair_until) return;
    if (!profile_class_is_gamepad(cod)) {
        log_line("inquiry: %s class %06x is not a gamepad, ignored", addr_str(rec), (unsigned)cod);
        return;
    }
    log_line("found gamepad %s (class %06x)", addr_str(rec), (unsigned)cod);
    pair_with(h, rec);
}

/* "Connection already exists" while paging a pad means this controller still
 * holds a link to it from an earlier run that ended without closing it (the
 * payload was started again over a running one, or crashed). Nothing else
 * uses this controller, and the pad would otherwise stay unreachable until
 * the chip is reset. Handles are small numbers: ask to close each; the
 * controller answers "unknown handle" to all but the real ones. Once a
 * minute at most. */
static void purge_stale_links(host_t *h)
{
    unsigned char p[3];
    unsigned handle;

    if (h->purge_at && now_ms() - h->purge_at < 60000) return;
    h->purge_at = now_ms();
    log_line("an earlier run left a link open on the controller; closing stale links");
    for (handle = 0; handle < 0x100; handle++) {
        if (link_by_handle(h, handle)) continue;        /* ours, in use */
        put16(p, handle);
        p[2] = 0x13;                    /* remote user terminated */
        send_cmd(h, OP_DISCONNECT, p, 3);
    }
}

static void on_event(host_t *h, const unsigned char *ev, int len)
{
    unsigned char p[32];
    link_t *l;
    int i;

    if (len < 2) return;
    switch (ev[0]) {
    case 0x01:  /* Inquiry Complete */
        log_line("inquiry: finished (status %#04x)", len > 2 ? ev[2] : 0xFF);
        h->inquiring = 0;
        break;

    case 0x02:  /* Inquiry Result: addr, scan mode, 2 reserved, class, clock */
        for (i = 0; i < ev[2] && 3 + (i + 1) * 14 <= len; i++) {
            const unsigned char *r = ev + 3 + i * 14;
            unsigned char rec[14];
            memcpy(rec, r, 7);          /* to the RSSI layout on_found reads */
            rec[7] = 0;
            memcpy(rec + 8, r + 9, 5);
            rec[13] = 0;
            on_found(h, rec);
        }
        break;

    case 0x22:  /* Inquiry Result with RSSI */
        for (i = 0; i < ev[2] && 3 + (i + 1) * 14 <= len; i++) on_found(h, ev + 3 + i * 14);
        break;

    case 0x2F:  /* Extended Inquiry Result */
        if (len >= 17) on_found(h, ev + 3);
        break;

    case 0x04:  /* Connection Request: addr, class, link type */
        if (len < 12 || ev[11] != 1) break;
        {
            db_rec *r = db_find(h, ev + 2);
            if (!r || r->le || link_by_addr(h, ev + 2)) break;      /* not ours */
        }
        l = link_new(h, ev + 2);
        memcpy(p, ev + 2, 6);
        if (!l) {
            p[6] = 0x0D;                /* limited resources */
            send_cmd(h, OP_REJECT_CONNECTION, p, 7);
            break;
        }
        p[6] = 0x00;                    /* become central, as a host does */
        send_cmd(h, OP_ACCEPT_CONNECTION, p, 7);
        log_line("pad %s: reconnecting", addr_str(ev + 2));
        break;

    case 0x03:  /* Connection Complete: status, handle, addr, type, enc */
        if (len < 13 || !(l = link_by_addr(h, ev + 5)) || l->connected) break;
        if (ev[2] != 0) {
            log_line("pad %s: connection failed (status %#04x)", addr_str(l->addr), ev[2]);
            link_free(h, l);
            break;
        }
        l->connected = 1;
        l->handle = le16(ev + 3) & 0x0FFF;
        l->t_conn = now_ms();
        log_line("pad %s: connected, handle %#05x", addr_str(l->addr), l->handle);
        break;

    case 0x05:  /* Disconnection Complete: status, handle, reason */
        if (len < 6 || !(l = link_by_handle(h, le16(ev + 3)))) break;
        log_line("pad %s: disconnected (reason %#04x)", addr_str(l->addr), ev[5]);
        link_free(h, l);
        break;

    case 0x06:  /* Authentication Complete: status, handle */
        if (len < 5 || !(l = link_by_handle(h, le16(ev + 3)))) break;
        if (ev[2] == 0) {
            l->auth_ok = 1;
            l->t_auth_ok = now_ms();
            break;
        }
        log_line("pad %s: authentication failed (status %#04x)", addr_str(l->addr), ev[2]);
        if (!l->pairing) db_forget(h, l->addr);     /* the pad lost its key */
        link_disconnect(h, l, "authentication failed");
        break;

    case 0x08:  /* Encryption Change: status, handle, enabled */
        if (len < 6 || !(l = link_by_handle(h, le16(ev + 3)))) break;
        if (l->le) {
            le_on_encryption(h, l, ev[2], ev[5]);
            break;
        }
        if (ev[2] == 0 && ev[5]) {
            l->enc_on = 1;
            l->auth_ok = 1;             /* encryption implies it */
        }
        break;

    case 0x30:  /* Encryption Key Refresh Complete: status, handle */
        if (len < 5 || !(l = link_by_handle(h, le16(ev + 3))) || !l->le) break;
        le_on_encryption(h, l, ev[2], ev[2] == 0);
        break;

    case 0x3E:  /* LE Meta */
        if (len >= 3) le_on_meta(h, ev + 2, len - 2);
        break;

    case 0x0E:  /* Command Complete: credits, opcode, return parameters */
        if (len < 6) break;
        h->cc_op = le16(ev + 3);
        h->cc_len = len < (int)sizeof h->cc ? len : (int)sizeof h->cc;
        memcpy(h->cc, ev, (size_t)h->cc_len);
        break;

    case 0x0F:  /* Command Status: status, credits, opcode */
        if (len >= 6 && le16(ev + 4) == OP_INQUIRY) {
            log_line("inquiry: controller answered status %#04x", ev[2]);
            if (ev[2] != 0) h->inquiring = 0;
        }
        if (len < 6 || ev[2] == 0) break;
        if (le16(ev + 4) == OP_CREATE_CONNECTION && ev[2] == 0x0B) purge_stale_links(h);
        if (le16(ev + 4) == OP_CREATE_CONNECTION || le16(ev + 4) == 0x200D) {
            int want_le = le16(ev + 4) == 0x200D;
            for (i = 0; i < HOST_MAX_PADS; i++) {
                l = &h->links[i];
                if (l->used && !l->connected && l->le == want_le &&
                    (l->pairing || want_le)) {
                    log_line("pad %s: connection refused (status %#04x)", addr_str(l->addr), ev[2]);
                    link_free(h, l);
                }
            }
        } else if (le16(ev + 4) == OP_AUTH_REQUESTED) {
            /* Usually a collision with the pad's own authentication:
             * wait and see, and ask again if nothing comes of it. */
            for (i = 0; i < HOST_MAX_PADS; i++) {
                l = &h->links[i];
                if (l->used && l->auth_sent && !l->auth_ok) {
                    l->auth_sent = 0;
                    l->t_conn = now_ms();
                }
            }
        }
        break;

    case 0x13:  /* Number Of Completed Packets: (handle, count) pairs */
        for (i = 0; i < ev[2] && 3 + i * 4 + 4 <= len; i++) {
            int done = (int)le16(ev + 3 + i * 4 + 2);
            l = link_by_handle(h, le16(ev + 3 + i * 4));
            if (l) pool_done(pool_of(h, l), done);
        }
        break;

    case 0x16:  /* PIN Code Request: legacy pairing */
        if (len < 8 || !link_by_addr(h, ev + 2)) break;
        memset(p, 0, sizeof p);
        memcpy(p, ev + 2, 6);
        p[6] = 4;
        memcpy(p + 7, "0000", 4);
        send_cmd(h, OP_PIN_CODE_REPLY, p, 23);
        break;

    case 0x17:  /* Link Key Request */
        if (len < 8 || !link_by_addr(h, ev + 2)) break;
        {
            db_rec *r = db_find(h, ev + 2);
            if (r) {
                memcpy(p, ev + 2, 6);
                memcpy(p + 6, r->key, 16);
                send_cmd(h, OP_LINK_KEY_REPLY, p, 22);
            } else {
                send_cmd(h, OP_LINK_KEY_NEG_REPLY, ev + 2, 6);
            }
        }
        break;

    case 0x18:  /* Link Key Notification: addr, key, type */
        if (len < 25 || !(l = link_by_addr(h, ev + 2))) break;
        {
            db_rec *r = db_put(h, ev + 2);
            memcpy(r->key, ev + 8, 16);
            r->type = ev[24];
            db_save(h);
        }
        log_line("pad %s: paired, key stored", addr_str(ev + 2));
        break;

    case 0x31:  /* IO Capability Request */
        if (len < 8 || !link_by_addr(h, ev + 2)) break;
        memcpy(p, ev + 2, 6);
        p[6] = 0x03;                    /* NoInputNoOutput */
        p[7] = 0x00;                    /* no OOB data */
        p[8] = 0x04;                    /* general bonding, no MITM */
        send_cmd(h, OP_IO_CAP_REPLY, p, 9);
        break;

    case 0x33:  /* User Confirmation Request */
        if (len < 8 || !link_by_addr(h, ev + 2)) break;
        send_cmd(h, OP_USER_CONFIRM_REPLY, ev + 2, 6);
        break;

    default:
        break;
    }
}

/* ---- per-link state machine ---------------------------------------------- */

/* Advances one link from whatever has happened so far. Each step waits
 * first for the pad to take it, then takes it itself, so it works for pads
 * that drive their own reconnection and for those that wait to be asked. */
static void link_step(host_t *h, link_t *l, long now)
{
    unsigned char p[3];

    if (l->le) {
        le_step(h, l, now);
        return;
    }
    if (!l->connected) {
        if (now - l->t_create > T_CONN_TIMEOUT) link_disconnect(h, l, "no connection");
        return;
    }
    if (l->disconnecting) return;
    if (!l->ready && now - l->t_conn > T_SETUP_TIMEOUT) {
        link_disconnect(h, l, "setup did not finish");
        return;
    }

    /* Authentication and encryption. */
    if (!l->auth_ok) {
        if (!l->auth_sent && now - l->t_conn >= (l->pairing ? 0 : T_SELF_AUTH)) {
            put16(p, l->handle);
            send_cmd(h, OP_AUTH_REQUESTED, p, 2);
            l->auth_sent = 1;
        }
        return;
    }
    if (!l->enc_on) {
        if (!l->enc_sent && now - l->t_auth_ok >= (l->pairing ? 0 : T_SELF_ENCRYPT)) {
            put16(p, l->handle);
            p[2] = 1;
            send_cmd(h, OP_SET_ENCRYPTION, p, 3);
            l->enc_sent = 1;
        }
        return;
    }
    if (!l->t_secure) {
        l->t_secure = now;
        log_line("pad %s: link encrypted", addr_str(l->addr));
    }

    /* Which pad is it: the store, the pad's Device ID record, and for pads
     * with no profile, the pad's own HID descriptor. */
    if (!l->ids_known && !l->sdp_query) {
        db_rec *r = db_find(h, l->addr);
        if (r && (r->vid || r->pid)) {
            l->vid = r->vid;
            l->pid = r->pid;
            l->ids_known = 1;
        } else {
            sdp_start(l, SDP_Q_IDS);
        }
    }
    if (l->ids_known && !l->prof && !l->sdp_query) {
        l->prof = profile_find(l->vid, l->pid);
        if (l->prof && l->prof->setup) l->prof->setup(l->pctx.bytes, l->vid, l->pid);
        if (!l->prof) {
            log_line("pad %s: %04x:%04x has no profile, reading its HID descriptor",
                     addr_str(l->addr), l->vid, l->pid);
            sdp_start(l, SDP_Q_DESC);
        }
    }
    if (l->sdp_query) {
        sdp_step(h, l, now);
        return;
    }
    if (l->disconnecting || !l->prof) return;
    if (l->sdp.st == CH_OPEN) chan_close(h, l, &l->sdp);

    /* HID channels: control, then interrupt. */
    chan_tick(h, l, &l->ctrl, now);
    chan_tick(h, l, &l->intr, now);
    if (l->ctrl.st == CH_CLOSED && now - l->t_secure >= T_SELF_CHANNELS)
        chan_connect(h, l, &l->ctrl);
    if (l->ctrl.st == CH_OPEN && l->intr.st == CH_CLOSED &&
        now - l->ctrl.t_state >= T_INTR_AFTER)
        chan_connect(h, l, &l->intr);

    if (!l->ready) {
        if (l->ctrl.st == CH_OPEN && l->intr.st == CH_OPEN) link_ready(h, l);
        return;
    }

    init_tick(h, l, now);
    if (l->prof->rumble_refresh_ms && (l->out.strong || l->out.weak) &&
        now - l->t_out >= l->prof->rumble_refresh_ms)
        send_output(h, l);
}

/* ---- pumping ---------------------------------------------------------------- */

static void pump(host_t *h, int timeout_ms)
{
    unsigned char buf[HCI_PKT_MAX];
    int n;

    if (h->hci.ops->pump(h->hci.ctx, timeout_ms) < 0) h->dead = 1;
    if (h->ev.idle) h->ev.idle(h->ev.ud);
    while ((n = h->hci.ops->next_event(h->hci.ctx, buf, (int)sizeof buf)) > 0) {
        h->last_rx = now_ms();
        on_event(h, buf, n);
    }
    while ((n = h->hci.ops->next_acl(h->hci.ctx, buf, (int)sizeof buf)) > 0) {
        h->last_rx = now_ms();
        on_acl(h, buf, n);
    }
}

static void start_inquiry(host_t *h)
{
    /* General inquiry, ~10 s, unlimited responses. */
    static const unsigned char inq[5] = { 0x33, 0x8B, 0x9E, 0x08, 0x00 };

    if (send_cmd(h, OP_INQUIRY, inq, 5)) {
        h->inquiring = 1;
        log_line("inquiry: started");
    } else {
        static int fails;
        if (fails++ < 5) log_line("inquiry: the command could not be sent");
    }
}

void host_poll(host_t *h, int timeout_ms)
{
    long now;
    int i, linking = 0;

    if (!h) return;                     /* no Bluetooth: nothing to do */

    pump(h, timeout_ms);
    now = now_ms();
    for (i = 0; i < HOST_MAX_PADS; i++) {
        if (!h->links[i].used) continue;
        link_step(h, &h->links[i], now);
        if (h->links[i].used && h->links[i].pairing && !h->links[i].ready &&
            !h->links[i].le) linking = 1;
    }
    le_poll(h, now);

    /* A controller that stopped answering (rest mode can leave the USB
     * device open but deaf) is probed, and given up if still silent. */
    if (now - h->last_rx > T_PROBE_IDLE) {
        if (h->probes >= PROBE_TRIES && now - h->probe_at > T_PROBE_WAIT) {
            if (!h->dead) log_line("controller silent after %d probes: transport lost", h->probes);
            h->dead = 1;
        } else if (h->probes < PROBE_TRIES && now - h->probe_at > T_PROBE_WAIT) {
            send_cmd(h, OP_READ_BUFFER_SIZE, NULL, 0);     /* any reply will do */
            h->probe_at = now;
            h->probes++;
        }
    } else {
        h->probes = 0;
    }

    /* Keep searching through the pairing window, one pad at a time. */
    if (now < h->pair_until && !h->inquiring && !linking) start_inquiry(h);
    if (now >= h->pair_until && h->inquiring) {
        send_cmd(h, OP_INQUIRY_CANCEL, NULL, 0);
        h->inquiring = 0;
    }
}

int host_pair(host_t *h, int seconds)
{
    h->pair_until = now_ms() + (long)seconds * 1000;
    log_line("pairing: searching for %d s", seconds);
    return 1;
}

int host_set_output(host_t *h, int slot, const pad_output *out)
{
    link_t *l;

    if (slot < 0 || slot >= HOST_MAX_PADS) return 0;
    l = &h->links[slot];
    if (!l->used || !l->ready) return 0;
    l->out = *out;
    return send_output(h, l);
}

int host_connected(const host_t *h, int slot)
{
    return slot >= 0 && slot < HOST_MAX_PADS && h->links[slot].used && h->links[slot].ready;
}

int host_paired_count(const host_t *h) { return h->ndb; }

int host_transport_lost(const host_t *h) { return h->dead; }

int host_pairing_left(const host_t *h)
{
    long left = h->pair_until - now_ms();
    return left > 0 ? (int)((left + 999) / 1000) : 0;
}

/* The address a link's pad is stored under: its identity, for LE pads
 * behind private addresses. */
static const unsigned char *link_identity(const link_t *l)
{
    static const unsigned char zero[6];
    if (l->le && memcmp(l->le_s.id_addr, zero, 6) != 0) return l->le_s.id_addr;
    return l->addr;
}

int host_paired_get(const host_t *h, int i, host_paired_info *out)
{
    const db_rec *d;
    int k;

    if (i < 0 || i >= h->ndb) return 0;
    d = &h->db[i];
    memset(out, 0, sizeof *out);
    memcpy(out->addr, d->addr, 6);
    out->vid = d->vid;
    out->pid = d->pid;
    out->le = d->le;
    for (k = 0; k < HOST_MAX_PADS; k++) {
        const link_t *l = &h->links[k];
        if (l->used && l->ready && memcmp(link_identity(l), d->addr, 6) == 0) out->connected = 1;
    }
    return 1;
}

int host_pad_get(const host_t *h, int slot, pad_info *info, pad_state *st)
{
    const link_t *l;

    if (slot < 0 || slot >= HOST_MAX_PADS) return 0;
    l = &h->links[slot];
    if (!l->used || !l->ready) return 0;
    memset(info, 0, sizeof *info);
    info->vid = l->vid;
    info->pid = l->pid;
    info->profile = l->prof ? l->prof->name : "";
    memcpy(info->addr, link_identity(l), 6);
    *st = l->st;
    return 1;
}

int host_forget(host_t *h, const unsigned char addr[6])
{
    int k;

    if (!db_find(h, addr)) return 0;
    for (k = 0; k < HOST_MAX_PADS; k++) {
        link_t *l = &h->links[k];
        if (l->used && memcmp(link_identity(l), addr, 6) == 0)
            link_disconnect(h, l, "forgotten from the web page");
    }
    db_forget(h, addr);
    log_line("pad %s: forgotten", addr_str(addr));
    return 1;
}

/* ---- controller setup ----------------------------------------------------- */

/* Sends a command and waits for its Command Complete. The reply can go to
 * the system's driver, so the command is sent again; those used here are
 * safe to repeat. */
int hci_sync(host_t *h, unsigned op, const void *p, int plen)
{
    int attempt;

    for (attempt = 1; attempt <= 3; attempt++) {
        long deadline = now_ms() + (attempt == 3 ? 3000 : 1000);

        h->cc_op = 0;
        if (!send_cmd(h, op, p, plen)) return 0;
        while (h->cc_op != op && now_ms() < deadline) pump(h, 20);
        if (h->cc_op == op) return h->cc_len > 5 && h->cc[5] == 0;
    }
    log_line("command %#06x: no reply", op);
    if (!h->diag_done && h->hci.ops->diag) {
        h->diag_done = 1;
        h->hci.ops->diag(h->hci.ctx);
    }
    return 0;
}

/* Prepares the controller, changing as little of it as it can. The
 * controller is shared with the system's driver, so:
 *   - no reset, no name, no class of device, no inquiry mode;
 *   - the event mask: needed, as Secure Simple Pairing events (IO
 *     capability, user confirmation) are off in the default mask. There is
 *     no command to read it back, so it is left as set;
 *   - Simple Pairing mode: only written if off. It cannot be turned off
 *     again once on, by specification;
 *   - page scan: needed for paired pads to reconnect by themselves. The
 *     value found is kept and put back on close, unless someone else has
 *     changed it meanwhile. */
static int setup(host_t *h)
{
    static const unsigned char mask[8] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x3F };
    static const unsigned char one[1] = { 1 };
    unsigned char scan[1];

    h->scan_orig = h->scan_set = -1;
    if (!hci_sync(h, OP_READ_BUFFER_SIZE, NULL, 0) || h->cc_len < 13) return 0;
    h->acl_mtu = le16(h->cc + 6);
    h->br.credits = h->br.max = (int)le16(h->cc + 9);
    log_line("controller: %d ACL buffers of %u bytes", h->br.max, h->acl_mtu);

    if (!hci_sync(h, OP_SET_EVENT_MASK, mask, 8)) return 0;
    if (!hci_sync(h, OP_READ_SSP_MODE, NULL, 0) || h->cc_len < 7 || h->cc[6] != 1) {
        if (!hci_sync(h, OP_WRITE_SSP_MODE, one, 1)) return 0;
        log_line("controller: Simple Pairing turned on");
    }
    if (!hci_sync(h, OP_READ_SCAN_ENABLE, NULL, 0) || h->cc_len < 7) return 0;
    h->scan_orig = h->cc[6];
    if (!(h->scan_orig & 0x02)) {
        scan[0] = (unsigned char)(h->scan_orig | 0x02);
        if (!hci_sync(h, OP_WRITE_SCAN_ENABLE, scan, 1)) return 0;
        h->scan_set = scan[0];
        log_line("controller: page scan turned on (was %#x)", h->scan_orig);
    }

    if (hci_sync(h, OP_READ_BD_ADDR, NULL, 0) && h->cc_len >= 12) memcpy(h->bdaddr, h->cc + 6, 6);
    log_line("controller address %s", addr_str(h->bdaddr));
    le_setup(h);
    return 1;
}

/* Puts back what setup() changed and nobody has changed since. */
static void restore(host_t *h)
{
    unsigned char scan[1];

    le_shutdown(h);
    if (h->scan_set < 0) return;
    if (hci_sync(h, OP_READ_SCAN_ENABLE, NULL, 0) && h->cc_len >= 7 && h->cc[6] == h->scan_set) {
        scan[0] = (unsigned char)h->scan_orig;
        hci_sync(h, OP_WRITE_SCAN_ENABLE, scan, 1);
        log_line("controller: page scan put back to %#x", h->scan_orig);
    } else {
        log_line("controller: page scan changed by someone else, left as is");
    }
}

host_t *host_open(hci_t hci, const char *db_path, const host_events *ev)
{
    host_t *h = calloc(1, sizeof *h);

    if (!h) return NULL;
    h->hci = hci;
    if (ev) h->ev = *ev;
    snprintf(h->db_path, sizeof h->db_path, "%s", db_path);
    {
        const char *slash = strrchr(db_path, '/');
        int dir = slash ? (int)(slash - db_path) : 1;
        snprintf(h->map_dir, sizeof h->map_dir, "%.*s/maps", dir, slash ? db_path : ".");
    }
    db_load(h);
    h->last_rx = now_ms();
    if (!setup(h)) {
        free(h);
        return NULL;
    }
    return h;
}

void host_close(host_t *h)
{
    int i;
    long deadline;

    if (!h) return;
    for (i = 0; i < HOST_MAX_PADS; i++) {
        if (!h->links[i].used) continue;
        if (h->dead) link_free(h, &h->links[i]);    /* nothing to tell the pad */
        else link_disconnect(h, &h->links[i], "closing");
    }
    deadline = now_ms() + 2000;
    for (;;) {
        int any = 0;
        for (i = 0; i < HOST_MAX_PADS; i++) any |= h->links[i].used;
        if (!any || now_ms() >= deadline) break;
        pump(h, 50);
    }
    if (!h->dead) restore(h);
    if (h->hci.ops->close) h->hci.ops->close(h->hci.ctx);
    free(h);
}
