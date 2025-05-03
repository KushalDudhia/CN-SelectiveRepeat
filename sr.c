/**
 * sr.c - Selective Repeat protocol implementation with single timer support
 * Final implementation with detailed inline comments for academic submission
 */

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include "emulator.h"
#include "sr.h"

#define RTT 16.0
#define WINDOWSIZE 6
#define SEQSPACE 12  // For SR, typically SEQSPACE >= 2 * WINDOWSIZE
#define NOTINUSE (-1)

/********************** Utility Functions **********************/
int ComputeChecksum(struct pkt packet) {
  int checksum = packet.seqnum + packet.acknum;
  for (int i = 0; i < 20; i++) checksum += (int)(packet.payload[i]);
  return checksum;
}

bool IsCorrupted(struct pkt packet) {
  return packet.checksum != ComputeChecksum(packet);
}

/********************** Sender (A) Globals **********************/
static struct pkt snd_buffer[SEQSPACE];     // Store sent but unACKed packets
static bool acked[SEQSPACE];                // Track ACK status of packets
static int base = 0;                        // Lower edge of sender window
static int nextseqnum = 0;                  // Next sequence number to use

/********************** Receiver (B) Globals **********************/
static struct pkt rcv_buffer[SEQSPACE];     // Buffer for out-of-order packets
static bool received[SEQSPACE];             // Track which packets have been received
static int expectedseqnum = 0;              // Next expected sequence number

/********************** Sender Functions **********************/
void A_output(struct msg message) {
  if (((nextseqnum - base + SEQSPACE) % SEQSPACE) >= WINDOWSIZE) {
    if (TRACE > 0) printf("A_output: Window is full, message dropped\n");
    window_full++;
    return;
  }

  struct pkt packet;
  packet.seqnum = nextseqnum;
  packet.acknum = NOTINUSE;
  memcpy(packet.payload, message.data, 20);
  packet.checksum = ComputeChecksum(packet);

  snd_buffer[nextseqnum] = packet;
  acked[nextseqnum] = false;

  if (TRACE > 1) printf("A_output: Sending packet %d\n", packet.seqnum);
  tolayer3(A, packet);

  if (base == nextseqnum)
    starttimer(A, RTT);  // Start timer only when base is equal to nextseqnum

  nextseqnum = (nextseqnum + 1) % SEQSPACE;
}

void A_input(struct pkt packet) {
  if (IsCorrupted(packet)) {
    if (TRACE > 0) printf("A_input: Received corrupted ACK %d\n", packet.acknum);
    return;
  }

  int seq = packet.acknum;
  if (!acked[seq]) {
    acked[seq] = true;
    new_ACKs++;
    total_ACKs_received++;

    if (TRACE > 0) printf("A_input: ACK %d received\n", seq);

    // Slide base if possible
    while (acked[base]) {
      acked[base] = false;
      base = (base + 1) % SEQSPACE;
    }

    // Stop/start timer based on pending unACKed packets
    stoptimer(A);
    if (base != nextseqnum)
      starttimer(A, RTT);
  } else {
    if (TRACE > 0) printf("A_input: Duplicate ACK %d ignored\n", seq);
  }
}

void A_timerinterrupt(void) {
  if (TRACE > 0) printf("A_timerinterrupt: Timer expired, retransmitting unACKed packets\n");
  for (int i = 0; i < WINDOWSIZE; i++) {
    int seq = (base + i) % SEQSPACE;
    if (!acked[seq] && ((nextseqnum - base + SEQSPACE) % SEQSPACE) > i) {
      tolayer3(A, snd_buffer[seq]);
      packets_resent++;
      if (TRACE > 1) printf("A_timerinterrupt: Resent packet %d\n", seq);
    }
  }
  starttimer(A, RTT);
}

void A_init(void) {
  base = 0;
  nextseqnum = 0;
  for (int i = 0; i < SEQSPACE; i++) {
    acked[i] = false;
  }
}

/********************** Receiver Functions **********************/
void B_input(struct pkt packet) {
  if (IsCorrupted(packet)) {
    if (TRACE > 0) printf("B_input: Corrupted packet %d ignored\n", packet.seqnum);
    return;
  }

  int seq = packet.seqnum;

  if (((seq - expectedseqnum + SEQSPACE) % SEQSPACE) < WINDOWSIZE) {
    // Accept if within receive window
    if (!received[seq]) {
      rcv_buffer[seq] = packet;
      received[seq] = true;
      packets_received++;
      if (TRACE > 0) printf("B_input: Stored packet %d\n", seq);

      // Deliver in order
      while (received[expectedseqnum]) {
        tolayer5(B, rcv_buffer[expectedseqnum].payload);
        received[expectedseqnum] = false;
        expectedseqnum = (expectedseqnum + 1) % SEQSPACE;
      }
    }
  } else {
    if (TRACE > 0) printf("B_input: Packet %d out of window ignored\n", seq);
  }

  // Send ACK
  struct pkt ackpkt;
  ackpkt.seqnum = 0;  // Not used by receiver
  ackpkt.acknum = packet.seqnum;
  memset(ackpkt.payload, 0, 20);
  ackpkt.checksum = ComputeChecksum(ackpkt);

  tolayer3(B, ackpkt);
  if (TRACE > 0) printf("B_input: Sent ACK %d\n", ackpkt.acknum);
}

void B_init(void) {
  expectedseqnum = 0;
  for (int i = 0; i < SEQSPACE; i++)
    received[i] = false;
}

/********************** Stub for B_output and B_timerinterrupt **********************/
void B_output(struct msg message) {
  
}
void B_timerinterrupt(void) {

}
