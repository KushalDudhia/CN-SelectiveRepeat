#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include "project2.h"

/* ----------------------------- CONSTANTS ----------------------------- */
#define RTT 16.0              /* Round Trip Time, must be 16.0 */
#define WINDOWSIZE 6          /* Sender window size (fixed) */
#define SEQSPACE 10           /* Sequence number space (must be >= 2*WINDOWSIZE for SR) */
#define NOTINUSE (-1)         /* Placeholder for unused ACK number */

/* ------------------------ GLOBAL VARIABLES --------------------------- */
/* Sender-side variables */
static struct pkt buffer[SEQSPACE];        /* Buffer for all sent but not yet ACKed packets */
static bool acked[SEQSPACE];               /* Tracks which packets are acknowledged */
static int window_base = 0;
static int next_seqnum = 0;

/* Receiver-side variables */
static int expected_seqnum_B = 0;
static struct pkt recv_buffer[SEQSPACE];   /* Buffer to store out-of-order packets */
static bool received[SEQSPACE];            /* Tracks which packets are received */

/* ------------------------- HELPER FUNCTIONS -------------------------- */

/* Compute checksum for a packet */
int ComputeChecksum(struct pkt *packet) {
    int checksum = 0;
    int i;
    checksum += packet->seqnum;
    checksum += packet->acknum;
    for (i = 0; i < 20; i++)
        checksum += (int)packet->payload[i];
    return checksum;
}

/* --------------------------- A: SENDER ------------------------------- */

void A_output(struct msg message) {
    int i;
    if ((next_seqnum + SEQSPACE - window_base) % SEQSPACE < WINDOWSIZE) {
        struct pkt packet;
        packet.seqnum = next_seqnum;
        packet.acknum = NOTINUSE;
        strncpy(packet.payload, message.data, 20);
        packet.payload[19] = '\0';  /* ensure null termination */
        packet.checksum = ComputeChecksum(&packet);

        buffer[next_seqnum] = packet;
        acked[next_seqnum] = false;

        tolayer3(A, packet);
        printf("A_output: Sent packet %d\n", packet.seqnum);

        if (window_base == next_seqnum) {
            starttimer(A, RTT);
        }

        next_seqnum = (next_seqnum + 1) % SEQSPACE;
    } else {
        printf("A_output: Window full, message dropped\n");
    }
}

void A_input(struct pkt packet) {
    if (packet.checksum != ComputeChecksum(&packet)) {
        printf("A_input: Corrupted ACK received\n");
        return;
    }

    if (packet.acknum >= 0 && !acked[packet.acknum]) {
        acked[packet.acknum] = true;
        printf("A_input: Received valid ACK %d\n", packet.acknum);

        while (acked[window_base]) {
            acked[window_base] = false;
            window_base = (window_base + 1) % SEQSPACE;
        }

        stoptimer(A);
        if (window_base != next_seqnum)
            starttimer(A, RTT);
    }
}

void A_timerinterrupt(void) {
    printf("A_timerinterrupt: Timeout. Resending unACKed packets\n");
    int i;
    for (i = 0; i < WINDOWSIZE; i++) {
        int seq = (window_base + i) % SEQSPACE;
        if (!acked[seq]) {
            tolayer3(A, buffer[seq]);
            printf("A_timerinterrupt: Resent packet %d\n", seq);
        }
    }
    starttimer(A, RTT);
}

void A_init(void) {
    int i;
    window_base = 0;
    next_seqnum = 0;
    for (i = 0; i < SEQSPACE; i++) {
        acked[i] = false;
    }
    printf("A_init: SR sender initialized\n");
}

/* --------------------------- B: RECEIVER ----------------------------- */

void B_input(struct pkt packet) {
    if (packet.checksum != ComputeChecksum(&packet)) {
        printf("B_input: Corrupted packet received\n");
        return;
    }

    printf("B_input: Received packet %d\n", packet.seqnum);

    struct pkt ack_packet;
    ack_packet.seqnum = 0;
    ack_packet.acknum = packet.seqnum;
    memset(ack_packet.payload, 0, 20);
    ack_packet.checksum = ComputeChecksum(&ack_packet);
    tolayer3(B, ack_packet);

    if (!received[packet.seqnum]) {
        recv_buffer[packet.seqnum] = packet;
        received[packet.seqnum] = true;

        while (received[expected_seqnum_B]) {
            tolayer5(B, recv_buffer[expected_seqnum_B].payload);
            received[expected_seqnum_B] = false;
            expected_seqnum_B = (expected_seqnum_B + 1) % SEQSPACE;
        }
    }
}

void B_init(void) {
    int i;
    expected_seqnum_B = 0;
    for (i = 0; i < SEQSPACE; i++) {
        received[i] = false;
    }
    printf("B_init: SR receiver initialized\n");
}
