#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include "emulator.h"
#include "gbn.h"

// ------------------------------------
//           CONSTANTS
// ------------------------------------

#define RTT 16.0              // Round Trip Time, must be 16.0
#define WINDOWSIZE 6          // Sender window size (fixed)
#define SEQSPACE 10           // Sequence number space (must be ≥ 2×WINDOWSIZE for SR)
#define NOTINUSE (-1)         // Placeholder for unused ACK number

// ------------------------------------
//        SENDER STATE VARIABLES
// ------------------------------------

static struct pkt buffer[SEQSPACE];    // Buffer for all sent but not yet ACKed packets
static bool acked[SEQSPACE];           // Boolean array to track which packets are ACKed
static int window_base;                // Base of the sender's current window
static int next_seqnum;                // Sequence number for the next outgoing packet

// ------------------------------------
//        RECEIVER STATE VARIABLES
// ------------------------------------

static struct pkt recv_buffer[SEQSPACE];   // Buffer to store out-of-order packets
static bool received[SEQSPACE];            // Boolean array to track received packets
static int expected_seqnum_B;              // Next expected in-order packet at receiver

// ------------------------------------
//       CHECKSUM UTILITIES
// ------------------------------------

// Compute checksum by adding sequence number, ack number, and payload bytes
int ComputeChecksum(struct pkt packet) {
    int checksum = packet.seqnum + packet.acknum;
    for (int i = 0; i < 20; i++)
        checksum += (int)(packet.payload[i]);
    return checksum;
}

// Returns true if packet has been corrupted (based on checksum mismatch)
bool IsCorrupted(struct pkt packet) {
    return packet.checksum != ComputeChecksum(packet);
}

// ------------------------------------
//       A_output (Sender)
// ------------------------------------

/*
 * Called by layer 5 to send a message.
 * If the sender window is not full, it sends the packet immediately and starts a timer
 * if it is the base packet. Otherwise, it drops the message.
 */
void A_output(struct msg message) {
    // Check if sender window has space
    if ((next_seqnum + SEQSPACE - window_base) % SEQSPACE < WINDOWSIZE) {
        struct pkt packet;
        packet.seqnum = next_seqnum;
        packet.acknum = NOTINUSE;

        // Copy message data into packet payload
        for (int i = 0; i < 20; i++)
            packet.payload[i] = message.data[i];

        packet.checksum = ComputeChecksum(packet);

        buffer[next_seqnum] = packet;
        acked[next_seqnum] = false;

        tolayer3(A, packet);
        if (TRACE > 0)
            printf("A_output: Sent packet %d\n", packet.seqnum);

        // Start timer if this is the base packet in window
        if (window_base == next_seqnum)
            starttimer(A, RTT);

        next_seqnum = (next_seqnum + 1) % SEQSPACE;
    } else {
        // Window full; drop packet
        if (TRACE > 0)
            printf("A_output: Window full, message dropped\n");
        window_full++;
    }
}

// ------------------------------------
//       A_input (Sender)
// ------------------------------------

/*
 * Called when an ACK packet arrives at the sender.
 * Marks the corresponding packet as ACKed and slides the window forward.
 * Restarts timer if there are still unACKed packets.
 */
void A_input(struct pkt packet) {
    if (!IsCorrupted(packet)) {
        if (TRACE > 0)
            printf("A_input: Received valid ACK %d\n", packet.acknum);

        total_ACKs_received++;

        if (!acked[packet.acknum]) {
            acked[packet.acknum] = true;
            new_ACKs++;
        }

        // Slide window forward for all consecutively ACKed packets
        while (acked[window_base]) {
            acked[window_base] = false;
            window_base = (window_base + 1) % SEQSPACE;
        }

        stoptimer(A);
        if (window_base != next_seqnum)
            starttimer(A, RTT);
    } else {
        if (TRACE > 0)
            printf("A_input: Corrupted ACK received\n");
    }
}

// ------------------------------------
//   A_timerinterrupt (Sender Timeout)
// ------------------------------------

/*
 * Called when the timer expires.
 * Retransmits all unACKed packets in the window.
 */
void A_timerinterrupt(void) {
    if (TRACE > 0)
        printf("A_timerinterrupt: Timeout. Resending unACKed packets\n");

    for (int i = 0; i < WINDOWSIZE; i++) {
        int seq = (window_base + i) % SEQSPACE;
        if (seq == next_seqnum)
            break;

        if (!acked[seq]) {
            tolayer3(A, buffer[seq]);
            packets_resent++;
            if (TRACE > 0)
                printf("A_timerinterrupt: Resent packet %d\n", seq);
        }
    }

    starttimer(A, RTT);
}

// ------------------------------------
//        A_init (Sender Init)
// ------------------------------------

/*
 * Called once before any A-side routines are called.
 * Initializes all sender-side variables.
 */
void A_init(void) {
    window_base = 0;
    next_seqnum = 0;
    for (int i = 0; i < SEQSPACE; i++)
        acked[i] = false;

    if (TRACE > 0)
        printf("A_init: SR sender initialized\n");
}

// ------------------------------------
//       B_input (Receiver)
// ------------------------------------

/*
 * Called when a packet arrives at receiver B.
 * Sends ACK for every valid packet, even if out-of-order.
 * Buffers out-of-order packets and delivers in-order ones to layer 5.
 */
void B_input(struct pkt packet) {
    struct pkt ack_pkt;

    if (!IsCorrupted(packet)) {
        if (TRACE > 0)
            printf("B_input: Received packet %d\n", packet.seqnum);

        packets_received++;

        // Always send an ACK
        ack_pkt.seqnum = 0;
        ack_pkt.acknum = packet.seqnum;
        for (int i = 0; i < 20; i++)
            ack_pkt.payload[i] = '0';
        ack_pkt.checksum = ComputeChecksum(ack_pkt);
        tolayer3(B, ack_pkt);

        // Buffer out-of-order packets
        if (!received[packet.seqnum]) {
            recv_buffer[packet.seqnum] = packet;
            received[packet.seqnum] = true;
        }

        // Deliver buffered in-order packets to application
        while (received[expected_seqnum_B]) {
            tolayer5(B, recv_buffer[expected_seqnum_B].payload);
            received[expected_seqnum_B] = false;
            expected_seqnum_B = (expected_seqnum_B + 1) % SEQSPACE;
        }
    } else {
        if (TRACE > 0)
            printf("B_input: Corrupted packet received\n");
    }
}

// ------------------------------------
//        B_init (Receiver Init)
// ------------------------------------

/*
 * Called once before any B-side routines are called.
 * Initializes all receiver-side variables.
 */
void B_init(void) {
    expected_seqnum_B = 0;
    for (int i = 0; i < SEQSPACE; i++)
        received[i] = false;
}

// ------------------------------------
//        Unused Bi-directional
// ------------------------------------

void B_output(struct msg message) {}
void B_timerinterrupt(void) {}
