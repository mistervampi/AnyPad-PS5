/* The HCI transport: commands out, events and ACL packets in.
 *
 * The host stack only sees these operations, so the same code runs on the
 * console (hci_usb.c, the PS5's Bluetooth chip) and in the tests (a
 * simulated controller and pad).
 */
#ifndef PH_HCI_H
#define PH_HCI_H

#define HCI_PKT_MAX 1100

typedef struct hci_ops {
    /* Sends an HCI command. Returns 0 on a transport error. */
    int  (*cmd)(void *ctx, unsigned opcode, const void *params, int plen);
    /* Sends one ACL packet, 4-byte header included. Returns 0 on error. */
    int  (*acl_send)(void *ctx, const unsigned char *pkt, int len);
    /* Waits up to timeout_ms for incoming packets. Returns 1 if any queued,
     * 0 if none, -1 if the transport is gone (the device went away, as it
     * does across rest mode). */
    int  (*pump)(void *ctx, int timeout_ms);
    /* Pops one queued packet; returns its length or 0 if none. Events come
     * as code, length, parameters; ACL packets with their 4-byte header. */
    int  (*next_event)(void *ctx, unsigned char *out, int max);
    int  (*next_acl)(void *ctx, unsigned char *out, int max);
    void (*close)(void *ctx);
    /* Optional: writes what the transport knows about itself to the log
     * (device, traffic counts, samples). Called once when the controller
     * does not answer, to find out why. NULL: nothing to say. */
    void (*diag)(void *ctx);
} hci_ops;

typedef struct {
    const hci_ops *ops;
    void *ctx;
} hci_t;

#endif
