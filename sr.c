/*==================================
  Computer Networks & Applications
  Student: Kushal Dudhia
  Student ID: a1904158
  Assignment: 2
===================================*/

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include "emulator.h"
#include "gbn.h"

/* Define constants */
#define RTT 16.0           /* Round trip time for timer (must be 16.0 for assignment) */
#define WINDOWSIZE 6       /* Sender window size (must be 6 for assignment) */
#define SEQSPACE 7         /* Sequence number space (minimum: WINDOWSIZE + 1) */
#define NOTINUSE -1        /* Placeholder for unused acknum values */

/* Compute checksum of packet */
int ComputeChecksum(struct pkt packet)
{
  int checksum = 0;
  int i;
  for (i = 0; i < 20; i++) {
    checksum += packet.payload[i];
  }
  checksum += packet.acknum;
  checksum += packet.seqnum;
  checksum = ~checksum;
  return checksum;
}

/* Check if packet is corrupted */
bool IsCorrupted(struct pkt packet)
{
  return (packet.checksum != ComputeChecksum(packet));
}

/* Sender-side variables */
static struct pkt buffer[WINDOWSIZE];
static int windowfirst, windowlast;
static int windowcount;
static int A_nextseqnum;

/* Called by layer 5 to send message */
void A_output(struct msg message)
{
  struct pkt sendpkt;
  int i;

  if (windowcount < WINDOWSIZE) {
    if (TRACE > 1)
      printf("----A: New message arrives, send window is not full, send new messge to layer3!\n");

    /* Create and prepare packet */
    sendpkt.seqnum = A_nextseqnum;
    sendpkt.acknum = NOTINUSE;
    for (i = 0; i < 20; i++)
      sendpkt.payload[i] = message.data[i];
    sendpkt.checksum = ComputeChecksum(sendpkt);

    /* Store packet in buffer */
    windowlast = (windowlast + 1) % WINDOWSIZE;
    buffer[windowlast] = sendpkt;
    for (i = 0; i < 20; i++)
      buffer[windowlast].payload[i] = sendpkt.payload[i];
    windowcount++;

    /* Send packet */
    if (TRACE > 0)
      printf("Sending packet %d to layer 3\n", sendpkt.seqnum);
    tolayer3(A, sendpkt);

    if (windowcount == 1) {
      starttimer(A, RTT);
    }

    A_nextseqnum = (A_nextseqnum + 1) % (WINDOWSIZE + 1);
  } else {
    if (TRACE > 0)
      printf("----A: New message arrives, send window is full\n");
    window_full++;
  }
}

/* Called when ACK is received at A */
void A_input(struct pkt packet)
{
  size_t ACKs = 0;
  bool dup = true;
  int i;

  if (!IsCorrupted(packet)) {
    if (TRACE > 0)
      printf("----A: uncorrupted ACK %d is received\n", packet.acknum);
    total_ACKs_received++;

    for (i = windowfirst; i < windowfirst + windowcount; i++) {
      if (packet.acknum == buffer[i % WINDOWSIZE].seqnum) {
        dup = false;
      }
    }

    if (!dup) {
      if (TRACE > 0)
        printf("----A: ACK %d is not a duplicate\n", packet.acknum);
      new_ACKs++;

      for (i = windowfirst; (i % WINDOWSIZE) != windowlast && packet.acknum != buffer[i % WINDOWSIZE].seqnum; i++) {
        ACKs++;
      }
      ACKs++;

      windowfirst = (windowfirst + ACKs) % WINDOWSIZE;
      windowcount -= ACKs;

      stoptimer(A);
      if (windowcount > 0) {
        starttimer(A, RTT);
      }
    } else if (TRACE > 0) {
      printf("----A: duplicate ACK received, do nothing!\n");
    }
  } else if (TRACE > 0) {
    printf("----A: corrupted ACK is received, do nothing!\n");
  }
}

/* Timer interrupt at sender A */
void A_timerinterrupt(void)
{
  int i = 0;
  if (TRACE > 0)
    printf("----A: time out,resend packets!\n");

  for (i = windowfirst; (i % WINDOWSIZE) != windowlast; i++) {
    printf("---A: resending packet %d\n", buffer[i].seqnum);
    tolayer3(A, buffer[i]);
    if (i == windowfirst) {
      starttimer(A, RTT);
    }
    packets_resent++;
  }

  printf("---A: resending packet %d\n", buffer[windowlast].seqnum);
  tolayer3(A, buffer[windowlast]);

  if (windowfirst == windowlast) {
    starttimer(A, RTT);
  }
  packets_resent++;
}

/* Initialize sender A */
void A_init(void)
{
  A_nextseqnum = 0;
  windowfirst = 0;
  windowlast = -1;
  windowcount = 0;
}

/* Receiver-side variables */
static int expectedseqnum;
static int B_nextseqnum;

/* Called when packet arrives at receiver B */
void B_input(struct pkt packet)
{
  struct pkt sendpkt;
  int i;

  if (!IsCorrupted(packet) && packet.seqnum == expectedseqnum) {
    if (TRACE > 0)
      printf("----B: packet %d is correctly received, send ACK!\n", packet.seqnum);
    packets_received++;

    tolayer5(B, packet.payload);
    sendpkt.acknum = expectedseqnum;
    expectedseqnum = (expectedseqnum + 1) % (WINDOWSIZE + 1);
  } else {
    if (TRACE > 0)
      printf("----B: packet corrupted or not expected sequence number, resend ACK!\n");
    if (expectedseqnum != 0) {
      sendpkt.acknum = expectedseqnum - 1;
    } else {
      sendpkt.acknum = WINDOWSIZE;
    }
  }

  sendpkt.seqnum = B_nextseqnum;
  B_nextseqnum = (B_nextseqnum + 1) % (WINDOWSIZE + 1);

  for (i = 0; i < 20; i++)
    sendpkt.payload[i] = '0';

  sendpkt.checksum = ComputeChecksum(sendpkt);
  tolayer3(B, sendpkt);
}

/* Initialize receiver B */
void B_init(void)
{
  expectedseqnum = 0;
  B_nextseqnum = 1;
}

/* B_output and B_timerinterrupt not used in this assignment */
void B_output(struct msg message) {}
void B_timerinterrupt(void) {}
