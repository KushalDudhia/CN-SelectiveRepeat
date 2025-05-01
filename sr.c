#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include "emulator.h"
#include "gbn.h"

/* ******************************************************************
   Go Back N protocol.  Adapted from J.F.Kurose
   ALTERNATING BIT AND GO-BACK-N NETWORK EMULATOR: VERSION 1.2

   Network properties:
   - one way network delay averages five time units (longer if there
   are other messages in the channel for GBN), but can be larger
   - packets can be corrupted (either the header or the data portion)
   or lost, according to user-defined probabilities
   - packets will be delivered in the order in which they were sent
   (although some can be lost).

   Modifications:
   - removed bidirectional GBN code and other code not used by prac.
   - fixed C style to adhere to current programming style
   - added GBN implementation
**********************************************************************/

#define RTT  16.0       /* round trip time.  MUST BE SET TO 16.0 when submitting assignment */
#define WINDOWSIZE 6    /* the maximum number of buffered unacked packet
                          MUST BE SET TO 6 when submitting assignment */
#define SEQSPACE 10     /* the min sequence space for GBN must be at least windowsize + 1  (Must be at least 2 × WINDOWSIZE for Selective Repeat)*/
#define NOTINUSE (-1)   /* used to fill header fields that are not being used */

/* generic procedure to compute the checksum of a packet.  Used by both sender and receiver
   the simulator will overwrite part of your packet with 'z's.  It will not overwrite your
   original checksum.  This procedure must generate a different checksum to the original if
   the packet is corrupted.
*/
static struct pkt buffer[SEQSPACE];     // Stores sent packets
static bool acked[SEQSPACE];            // Tracks which packets are ACKed
static int window_base;                 // First unACKed packet in window
static int next_seqnum;                 // Next sequence number to use


int ComputeChecksum(struct pkt packet)
{
  int checksum = 0;
  int i;

  checksum = packet.seqnum;
  checksum += packet.acknum;
  for ( i=0; i<20; i++ )
    checksum += (int)(packet.payload[i]);

  return checksum;
}

bool IsCorrupted(struct pkt packet)
{
  if (packet.checksum == ComputeChecksum(packet))
    return (false);
  else
    return (true);
}


/********* Sender (A) variables and functions ************/

static struct pkt buffer[SEQSPACE];
static bool acked[SEQSPACE];
static int window_base;
static int next_seqnum;


/* called from layer 5 (application layer), passed the message to be sent to other side */

void A_output(struct msg message)
{
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
        starttimer(A, RTT);

        if (TRACE > 0)
            printf("A_output: Sent packet %d\n", packet.seqnum);

        next_seqnum = (next_seqnum + 1) % SEQSPACE;
    } else {
        if (TRACE > 0)
            printf("A_output: Window full, message dropped\n");
        window_full++;
    }
}


/* called from layer 3, when a packet arrives for layer 4
   In this practical this will always be an ACK as B never sends data.
*/
void A_input(struct pkt packet)
{
    if (!IsCorrupted(packet)) {
        if (TRACE > 0)
            printf("A_input: Received valid ACK %d\n", packet.acknum);

        total_ACKs_received++;

        // If this is a new ACK
        if (!acked[packet.acknum]) {
            acked[packet.acknum] = true;
            new_ACKs++;
        }

        // Slide the window forward
        while (acked[window_base]) {
            acked[window_base] = false;  // clear slot
            window_base = (window_base + 1) % SEQSPACE;
        }

        // Manage timer
        stoptimer(A);
        if (window_base != next_seqnum) {
            starttimer(A, RTT);
        }
    } else {
        if (TRACE > 0)
            printf("A_input: Corrupted ACK received\n");
    }
}

/* called when A's timer goes off */
void A_timerinterrupt(void)
{
    if (TRACE > 0)
        printf("A_timerinterrupt: Timeout occurred. Resending unACKed packets.\n");

    for (int i = 0; i < WINDOWSIZE; i++) {
        int seq = (window_base + i) % SEQSPACE;
        if (seq == next_seqnum) break;

        if (!acked[seq]) {
            tolayer3(A, buffer[seq]);
            packets_resent++;

            if (TRACE > 0)
                printf("A_timerinterrupt: Resent packet %d\n", seq);
        }
    }

    starttimer(A, RTT);  // Restart timer
}



/* the following routine will be called once (only) before any other */
/* entity A routines are called. You can use it to do any initialization */
void A_init(void)
{ /*Updated sr.c with Selective Repeat constants, globals, and A_init()*/
   
    window_base = 0;
    next_seqnum = 0;
    for (int i = 0; i < SEQSPACE; i++) {
        acked[i] = false;
    }

    if (TRACE > 0)
        printf("A_init: SR sender initialized (window_base = %d, next_seqnum = %d)\n", window_base, next_seqnum);
}



/********* Receiver (B)  variables and procedures ************/

static int expectedseqnum; /* the sequence number expected next by the receiver */
static int B_nextseqnum;   /* the sequence number for the next packets sent by B */


/* called from layer 3, when a packet arrives for layer 4 at B*/
void B_input(struct pkt packet)
{
  struct pkt sendpkt;
  int i;

  /* if not corrupted and received packet is in order */
  if  ( (!IsCorrupted(packet))  && (packet.seqnum == expectedseqnum) ) {
    if (TRACE > 0)
      printf("----B: packet %d is correctly received, send ACK!\n",packet.seqnum);
    packets_received++;

    /* deliver to receiving application */
    tolayer5(B, packet.payload);

    /* send an ACK for the received packet */
    sendpkt.acknum = expectedseqnum;

    /* update state variables */
    expectedseqnum = (expectedseqnum + 1) % SEQSPACE;
  }
  else {
    /* packet is corrupted or out of order resend last ACK */
    if (TRACE > 0)
      printf("----B: packet corrupted or not expected sequence number, resend ACK!\n");
    if (expectedseqnum == 0)
      sendpkt.acknum = SEQSPACE - 1;
    else
      sendpkt.acknum = expectedseqnum - 1;
  }

  /* create packet */
  sendpkt.seqnum = B_nextseqnum;
  B_nextseqnum = (B_nextseqnum + 1) % 2;

  /* we don't have any data to send.  fill payload with 0's */
  for ( i=0; i<20 ; i++ )
    sendpkt.payload[i] = '0';

  /* computer checksum */
  sendpkt.checksum = ComputeChecksum(sendpkt);

  /* send out packet */
  tolayer3 (B, sendpkt);
}

/* the following routine will be called once (only) before any other */
/* entity B routines are called. You can use it to do any initialization */
void B_init(void)
{
  expectedseqnum = 0;
  B_nextseqnum = 1;
}

/******************************************************************************
 * The following functions need be completed only for bi-directional messages *
 *****************************************************************************/

/* Note that with simplex transfer from a-to-B, there is no B_output() */
void B_output(struct msg message)
{
}

/* called when B's timer goes off */
void B_timerinterrupt(void)
{
}
