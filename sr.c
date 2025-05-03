#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include "emulator.h"
#include "gbn.h"

// ------------------ CONSTANTS ------------------

#define RTT 16.0
#define WINDOWSIZE 6
#define SEQSPACE 10
#define NOTINUSE (-1)

// ------------------ SENDER STATE VARIABLES ------------------

static struct pkt buffer[SEQSPACE];     // Stores sent packets for potential retransmission
static bool acked[SEQSPACE];            // Tracks whether each sequence number has been ACKed
static int window_base;                 // Oldest unACKed packet in the window (base of the window)
static int next_seqnum;                 // Sequence number for the next outgoing packet

// ------------------ RECEIVER STATE VARIABLES ------------------

static int B_nextseqnum;                // Required by emulator, not used in SR
static struct pkt recv_buffer[SEQSPACE];// Buffers out-of-order packets
static bool received[SEQSPACE];         // Tracks which sequence numbers have been received
static int expected_seqnum_B;           // Next expected in-order packet to deliver to layer 5

// ------------------ CHECKSUM UTILS ------------------

// Computes the checksum over a packet's fields to detect corruption
int ComputeChecksum(struct pkt packet) {
    int checksum = packet.seqnum + packet.acknum;
    for (int i = 0; i < 20; i++)
        checksum += (int)(packet.payload[i]);
    return checksum;
}

// Verifies if a packet is corrupted based on checksum
bool IsCorrupted(struct pkt packet) {
    return packet.checksum != ComputeChecksum(packet);
}

// ------------------ A_output ------------------
/*
 * Called when layer 5 on A has data to send.
 * If the window is not full, packet is created, buffered and sent.
 */
void A_output(struct msg message) {
    if ((next_seqnum + SEQSPACE - window_base) % SEQSPACE < WINDOWSIZE) {
        struct pkt packet;
        packet.seqnum = next_seqnum;
        packet.acknum = NOTINUSE;

        for (int i = 0; i < 20; i++)
            packet.payload[i] = message.data[i];

        packet.checksum = ComputeChecksum(packet);

        buffer[next_seqnum] = packet;
        acked[next_seqnum] = false;

        tolayer3(A, packet);

        if (window_base == next_seqnum)
            starttimer(A, RTT); // Timer is only for the base packet

        if (TRACE > 0)
            printf("A_output: Sent packet %d\n", packet.seqnum);

        next_seqnum = (next_seqnum + 1) % SEQSPACE;
    } else {
        if (TRACE > 0)
            printf("A_output: Window full, message dropped\n");
        window_full++;
    }
}

// ------------------ A_input ------------------
/*
 * Called when an ACK is received from B.
 * Marks packets as ACKed and slides the sender window forward.
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

        // Slide the window base forward for consecutive ACKed packets
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

// ------------------ A_timerinterrupt ------------------
/*
 * Called when the timer at A expires.
 * Resends all unACKed packets in the current window.
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

    starttimer(A, RTT); // Restart timer after retransmission
}

// ------------------ A_init ------------------
/*
 * Initializes sender-side data structures before simulation starts.
 */
void A_init(void) {
    window_base = 0;
    next_seqnum = 0;
    for (int i = 0; i < SEQSPACE; i++)
        acked[i] = false;

    if (TRACE > 0)
        printf("A_init: SR sender initialized\n");
}

// ------------------ B_input ------------------
/*
 * Called when a packet arrives at B from A.
 * Sends an individual ACK. Buffers out-of-order packets.
 * Delivers in-order packets to layer 5.
 */
void B_input(struct pkt packet) {
    struct pkt ack_pkt;

    if (!IsCorrupted(packet)) {
        if (TRACE > 0)
            printf("B_input: Received packet %d\n", packet.seqnum);

        packets_received++;

        // Prepare ACK
        ack_pkt.seqnum = 0;
        ack_pkt.acknum = packet.seqnum;
        for (int i = 0; i < 20; i++)
            ack_pkt.payload[i] = '0';
        ack_pkt.checksum = ComputeChecksum(ack_pkt);
        tolayer3(B, ack_pkt);

        // If not already buffered, buffer the packet
        if (!received[packet.seqnum]) {
            recv_buffer[packet.seqnum] = packet;
            received[packet.seqnum] = true;
        }

        // Deliver in-order packets
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

// ------------------ B_init ------------------
/*
 * Initializes receiver-side data structures before simulation starts.
 */
void B_init(void) {
    expected_seqnum_B = 0;
    for (int i = 0; i < SEQSPACE; i++)
        received[i] = false;

    B_nextseqnum = 1; // Required by emulator
}

// ------------------ Unused (simplex only) ------------------

void B_output(struct msg message) {}
void B_timerinterrupt(void) {}
