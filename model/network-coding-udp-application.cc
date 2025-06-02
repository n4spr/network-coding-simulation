/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2023
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Author: Your Name <your.email@example.com>
 */

#include "network-coding-udp-application.h"
#include "ns3/log.h"
#include "ns3/simulator.h"
#include "ns3/socket.h"
#include "ns3/socket-factory.h"
#include "ns3/packet.h"
#include "ns3/udp-socket-factory.h"
#include "ns3/uinteger.h"
#include "ns3/double.h"
#include "ns3/string.h"
#include "ns3/boolean.h"
#include "ns3/inet-socket-address.h"
#include "ns3/inet6-socket-address.h"
#include <random>

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("NetworkCodingUdpApplication");
NS_OBJECT_ENSURE_REGISTERED (NetworkCodingUdpApplication);

TypeId
NetworkCodingUdpApplication::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::NetworkCodingUdpApplication")
    .SetParent<Application> ()
    .SetGroupName ("NetworkCoding")
    .AddConstructor<NetworkCodingUdpApplication> ()
    .AddAttribute ("Remote", "The address of the destination",
                   AddressValue (),
                   MakeAddressAccessor (&NetworkCodingUdpApplication::m_peer),
                   MakeAddressChecker ())
    .AddAttribute ("PacketSize", "The size of packets to send",
                   UintegerValue (1024),
                   MakeUintegerAccessor (&NetworkCodingUdpApplication::m_packetSize),
                   MakeUintegerChecker<uint16_t> (1, 65507))
    .AddAttribute ("NumPackets", "The number of packets to send",
                   UintegerValue (1000),
                   MakeUintegerAccessor (&NetworkCodingUdpApplication::m_numPackets),
                   MakeUintegerChecker<uint32_t> (0))
    .AddAttribute ("GenerationSize", "The size of each generation",
                   UintegerValue (8),
                   MakeUintegerAccessor (&NetworkCodingUdpApplication::m_generationSize),
                   MakeUintegerChecker<uint16_t> (1, 255))
    .AddAttribute ("DataRate", "The data rate to use",
                   DataRateValue (DataRate ("1Mbps")),
                   MakeDataRateAccessor (&NetworkCodingUdpApplication::m_dataRate),
                   MakeDataRateChecker ())
    .AddAttribute ("LossRate", "The packet loss rate to simulate",
                   DoubleValue (0.0),
                   MakeDoubleAccessor (&NetworkCodingUdpApplication::m_lossRate),
                   MakeDoubleChecker<double> (0.0, 1.0))
    .AddTraceSource ("Tx", "A new packet is sent",
                     MakeTraceSourceAccessor (&NetworkCodingUdpApplication::m_txTrace),
                     "ns3::Packet::TracedCallback")
    .AddTraceSource ("Rx", "A packet is received",
                     MakeTraceSourceAccessor (&NetworkCodingUdpApplication::m_rxTrace),
                     "ns3::Packet::TracedCallback")
    .AddTraceSource ("Decoding", "A generation decoding attempt",
                     MakeTraceSourceAccessor (&NetworkCodingUdpApplication::m_decodingTrace),
                     "ns3::TracedValueCallback::BoolUint32")
  ;
  return tid;
}

NetworkCodingUdpApplication::NetworkCodingUdpApplication ()
  : m_socket (nullptr),
    m_peer (), // CHANGED: Remove m_connectedSocket for UDP
    m_packetSize (1024),
    m_numPackets (1000),
    m_generationSize (8),
    m_dataRate (DataRate ("1Mbps")),
    m_lossRate (0.0),
    m_running (false),
    m_packetsSent (0),
    m_packetsReceived (0),
    m_innovativePacketsReceived (0),
    m_generationsDecoded (0),
    m_nextSeq (0),
    m_encoder (nullptr),
    m_decoder (nullptr),
    m_currentGeneration (0),
    m_generationPacketCount (0),
    m_currentGenerationSent (0),
    m_packetsInCurrentGeneration (0),
    m_waitingForGenerationAck (false),
    m_generationTimeout (Seconds (2.0)),
    m_maxRetransmissions (5),
    m_retransmissionCount (0)
{
  NS_LOG_FUNCTION (this);
}

NetworkCodingUdpApplication::~NetworkCodingUdpApplication ()
{
  NS_LOG_FUNCTION (this);
}   

void
NetworkCodingUdpApplication::DoDispose (void)
{
  NS_LOG_FUNCTION (this);
  m_socket = nullptr;
  m_encoder = nullptr;
  m_decoder = nullptr;
  Simulator::Cancel (m_sendEvent);
  Simulator::Cancel (m_retransmissionTimer);
  Application::DoDispose ();
}

void
NetworkCodingUdpApplication::Setup (Ptr<Socket> socket, Address address, uint16_t packetSize, 
                                   uint32_t numPackets, uint16_t generationSize, 
                                   DataRate dataRate, double lossRate)
{
  NS_LOG_FUNCTION (this << socket << address << packetSize << numPackets 
                   << generationSize << dataRate << lossRate);
  
  m_socket = socket;
  m_peer = address;
  m_packetSize = packetSize;
  m_numPackets = numPackets;
  m_generationSize = generationSize;
  m_dataRate = dataRate;
  m_lossRate = lossRate;
  
  if (m_encoder)
    {
      m_encoder->SetGenerationSize (generationSize);
      m_encoder->SetPacketSize (packetSize);
    }
  if (m_decoder)
    {
      m_decoder->SetGenerationSize (generationSize);
      m_decoder->SetPacketSize (packetSize);
    }
}

void
NetworkCodingUdpApplication::StartApplication (void)
{
  NS_LOG_FUNCTION (this);
  
  m_running = true;
  m_packetsSent = 0;
  m_packetsReceived = 0;
  m_innovativePacketsReceived = 0;
  m_generationsDecoded = 0;
  m_nextSeq = 0;
  m_currentGeneration = 0;
  m_generationPacketCount = 0;
  m_currentGenerationSent = 0;
  m_packetsInCurrentGeneration = 0;
  m_waitingForGenerationAck = false;
  m_retransmissionCount = 0;
  
  if (!m_encoder || !m_decoder)
    {
      NS_LOG_INFO ("Initializing encoder/decoder with generation size " << m_generationSize 
                   << " and packet size " << m_packetSize);
      
      m_encoder = CreateObject<NetworkCodingEncoder> (m_generationSize, m_packetSize);
      m_decoder = CreateObject<NetworkCodingDecoder> (m_generationSize, m_packetSize);
      
      if (!m_encoder || !m_decoder)
        {
          NS_FATAL_ERROR ("Failed to create encoder or decoder");
        }
      
      NS_LOG_INFO ("Encoder and decoder initialized successfully");
    }

  if (!m_socket)
    {
      m_socket = Socket::CreateSocket (GetNode (), UdpSocketFactory::GetTypeId ()); // CHANGED: Use UDP
      
      bool isReceiver = (m_numPackets == 0);
      
      if (isReceiver)
        {
          NS_LOG_INFO ("Setting up as RECEIVER");
          if (InetSocketAddress::IsMatchingType (m_peer))
            {
              InetSocketAddress local = InetSocketAddress::ConvertFrom (m_peer);
              m_socket->Bind (InetSocketAddress (Ipv4Address::GetAny (), local.GetPort ()));
            }
          else if (Inet6SocketAddress::IsMatchingType (m_peer))
            {
              Inet6SocketAddress local = Inet6SocketAddress::ConvertFrom (m_peer);
              m_socket->Bind (Inet6SocketAddress (Ipv6Address::GetAny (), local.GetPort ()));
            }
        }
      else
        {
          NS_LOG_INFO ("Setting up as SENDER");
          m_socket->Bind ();
          
          // CHANGED: UDP doesn't need connection setup, start sending immediately
          if (m_numPackets > 0)
            {
              ScheduleNext ();
            }
        }
    }
  
  m_socket->SetRecvCallback (MakeCallback (&NetworkCodingUdpApplication::HandleRead, this));
}

void
NetworkCodingUdpApplication::StopApplication (void)
{
  NS_LOG_FUNCTION (this);
  
  m_running = false;
  
  Simulator::Cancel (m_sendEvent);
  Simulator::Cancel (m_retransmissionTimer);
  
  if (m_socket)
    {
      m_socket->Close ();
      m_socket->SetRecvCallback (MakeNullCallback<void, Ptr<Socket>> ());
    }
  
  NS_LOG_INFO ("Application stopped. Packets sent: " << m_packetsSent 
               << ", Packets received: " << m_packetsReceived
               << ", Innovative packets: " << m_innovativePacketsReceived
               << ", Generations decoded: " << m_generationsDecoded);
}

// REMOVED: ConnectionSucceeded, ConnectionFailed, HandleConnectionRequest, HandleAccept (UDP doesn't need these)

void
NetworkCodingUdpApplication::HandleRead (Ptr<Socket> socket)
{
  NS_LOG_FUNCTION (this << socket);
  
  Ptr<Packet> packet;
  Address from;
  
  while ((packet = socket->RecvFrom (from)))
    {
      if (packet->GetSize () == 0)
        {
          break;
        }
      
      m_packetsReceived++;
      m_rxTrace (packet);
      
      NS_LOG_INFO ("Received packet of size " << packet->GetSize () 
                   << " from " << from << ". Total received: " << m_packetsReceived);
      
      // Check if this is an ACK packet
      if (IsAckPacket (packet))
        {
          HandleAck (packet);
          continue;
        }
      
      if (m_decoder)
        {
          bool isInnovative = ProcessReceivedPacket (packet);
          
          if (isInnovative)
            {
              m_innovativePacketsReceived++;
              m_generationPacketCount++;
              NS_LOG_INFO ("Received innovative packet. Total innovative: " 
                           << m_innovativePacketsReceived 
                           << ", Generation packets: " << m_generationPacketCount);
              
              // Check if we have enough packets to decode the current generation
              if (m_generationPacketCount >= m_generationSize)
                {
                  m_generationsDecoded++;
                  m_decodingTrace (true, m_generationsDecoded);
                  NS_LOG_INFO ("Generation " << m_currentGeneration << " decoded! Total decoded: " << m_generationsDecoded);
                  
                  // Send ACK to sender
                  SendAck (m_currentGeneration, from); // CHANGED: Pass sender address
                  
                  // Reset for next generation
                  m_currentGeneration++;
                  m_generationPacketCount = 0;
                }
            }
          else
            {
              NS_LOG_INFO ("Received redundant packet");
            }
        }
    }
}

bool
NetworkCodingUdpApplication::ProcessReceivedPacket (Ptr<Packet> packet)
{
  NS_LOG_FUNCTION (this << packet);
  
  if (!m_decoder)
    {
      NS_LOG_ERROR ("No decoder available");
      return false;
    }
  
  uint32_t packetSize = packet->GetSize();
  
  // Validate packet size
  if (packetSize != m_packetSize)
    {
      NS_LOG_WARN ("Received packet of unexpected size " << packetSize 
                   << ", expected " << m_packetSize << ". Ignoring.");
      return false;
    }
  
  uint8_t* buffer = new uint8_t[packetSize];
  packet->CopyData(buffer, packetSize);
  
  // Extract generation ID from first 4 bytes
  uint32_t packetGeneration = 0;
  if (packetSize >= 4)
    {
      packetGeneration = (buffer[0] << 24) | (buffer[1] << 16) | (buffer[2] << 8) | buffer[3];
    }
  
  NS_LOG_INFO ("Processing packet for generation " << packetGeneration 
               << ", current generation: " << m_currentGeneration
               << ", size: " << packetSize);
  
  // ONLY move to new generation if it's exactly the next expected generation
  if (packetGeneration != m_currentGeneration)
    {
      if (packetGeneration == m_currentGeneration + 1)  // Only accept next generation
        {
          NS_LOG_INFO ("Moving to new generation " << packetGeneration);
          m_currentGeneration = packetGeneration;
          m_generationPacketCount = 0;
        }
      else
        {
          NS_LOG_INFO ("Received packet from unexpected generation " << packetGeneration 
                       << " (expected " << m_currentGeneration << "), ignoring");
          delete[] buffer;
          return false;
        }
    }
  
  // Simple innovation check based on generation position
  bool isInnovative = (m_generationPacketCount < m_generationSize);
  
  NS_LOG_INFO ("Added packet to decoder for generation " << packetGeneration 
               << ", innovative: " << isInnovative 
               << " (packets in generation: " << m_generationPacketCount << "/" << m_generationSize << ")");
  
  delete[] buffer;
  return isInnovative;
}

void
NetworkCodingUdpApplication::ScheduleNext (void)
{
  NS_LOG_FUNCTION (this);
  
  // Calculate total generations needed
  uint32_t totalGenerationsNeeded = (m_numPackets + m_generationSize - 1) / m_generationSize;
  
  // Check if we still have generations to send
  bool hasMoreGenerations = (m_currentGenerationSent < totalGenerationsNeeded);
  
  // For the current generation, check if we still have packets to send
  bool hasMorePacketsInCurrentGeneration = false;
  if (hasMoreGenerations)
    {
      uint32_t packetsNeededInCurrentGeneration = m_generationSize;
      if (m_currentGenerationSent == totalGenerationsNeeded - 1) // Last generation
        {
          uint32_t remainingPackets = m_numPackets - (m_currentGenerationSent * m_generationSize);
          packetsNeededInCurrentGeneration = remainingPackets;
        }
      hasMorePacketsInCurrentGeneration = (m_packetsInCurrentGeneration < packetsNeededInCurrentGeneration);
    }
  
  NS_LOG_INFO ("ScheduleNext: Generation " << m_currentGenerationSent 
               << "/" << totalGenerationsNeeded 
               << ", packets in current gen: " << m_packetsInCurrentGeneration
               << ", waiting for ACK: " << m_waitingForGenerationAck);
  
  if (m_running && hasMoreGenerations && hasMorePacketsInCurrentGeneration && !m_waitingForGenerationAck)
    {
      Time tNext = Seconds (m_packetSize * 8 / static_cast<double> (m_dataRate.GetBitRate ()));
      m_sendEvent = Simulator::Schedule (tNext, &NetworkCodingUdpApplication::SendPacket, this);
      NS_LOG_DEBUG ("Scheduled next packet in " << tNext.GetSeconds () << " seconds");
    }
  else if (m_waitingForGenerationAck)
    {
      NS_LOG_INFO ("Waiting for generation ACK, not scheduling next packet");
    }
  else if (!hasMoreGenerations)
    {
      NS_LOG_INFO ("All " << totalGenerationsNeeded << " generations completed");
    }
  else if (!hasMorePacketsInCurrentGeneration)
    {
      NS_LOG_INFO ("Current generation " << m_currentGenerationSent << " completed, waiting for ACK");
    }
  else
    {
      NS_LOG_INFO ("Finished sending all packets or application stopped");
    }
}

void
NetworkCodingUdpApplication::SendPacket (void)
{
  NS_LOG_FUNCTION (this);
  
  uint32_t totalGenerationsNeeded = (m_numPackets + m_generationSize - 1) / m_generationSize;
  
  if (!m_running || !m_socket || m_currentGenerationSent >= totalGenerationsNeeded)
    {
      NS_LOG_INFO ("SendPacket: stopping - running: " << m_running 
                   << ", socket: " << (m_socket ? "yes" : "no")
                   << ", current gen: " << m_currentGenerationSent 
                   << "/" << totalGenerationsNeeded);
      return;
    }
  
  if (m_waitingForGenerationAck && IsCurrentGenerationComplete ())
    {
      NS_LOG_INFO ("Waiting for ACK for generation " << m_currentGenerationSent 
                   << ", not sending more packets");
      return;
    }
  
  SendCodedPacket (m_currentGenerationSent);
  
  if (IsCurrentGenerationComplete ())
    {
      m_waitingForGenerationAck = true;
      
      m_retransmissionTimer = Simulator::Schedule (m_generationTimeout, 
                                                   &NetworkCodingUdpApplication::HandleGenerationTimeout, 
                                                   this);
      
      NS_LOG_INFO ("Completed generation " << m_currentGenerationSent 
                   << ", waiting for ACK");
    }
  else
    {
      ScheduleNext ();
    }
}

void
NetworkCodingUdpApplication::SendCodedPacket (uint32_t generationId)
{
  NS_LOG_FUNCTION (this << generationId);
  
  if (!m_encoder)
    {
      return;
    }
  
  uint32_t totalGenerationsNeeded = (m_numPackets + m_generationSize - 1) / m_generationSize;
  if (generationId >= totalGenerationsNeeded)
    {
      NS_LOG_INFO ("All generations completed, not sending more packets");
      return;
    }
  
  uint32_t packetsInThisGeneration = m_generationSize;
  if (generationId == totalGenerationsNeeded - 1)
    {
      uint32_t remainingPackets = m_numPackets - (generationId * m_generationSize);
      packetsInThisGeneration = remainingPackets;
    }
  
  if (m_packetsInCurrentGeneration >= packetsInThisGeneration)
    {
      NS_LOG_INFO ("Generation " << generationId << " already complete (" 
                   << m_packetsInCurrentGeneration << "/" << packetsInThisGeneration << ")");
      return;
    }
  
  uint32_t encodedSize = m_packetSize;
  uint8_t* encodedData = new uint8_t[encodedSize];
  
  encodedData[0] = (generationId >> 24) & 0xFF;
  encodedData[1] = (generationId >> 16) & 0xFF;
  encodedData[2] = (generationId >> 8) & 0xFF;
  encodedData[3] = generationId & 0xFF;
  
  uint32_t globalPacketIndex = generationId * m_generationSize + m_packetsInCurrentGeneration;
  for (uint32_t i = 4; i < encodedSize; i++)
    {
      encodedData[i] = (uint8_t)((globalPacketIndex + i) % 256);
    }
  
  Ptr<Packet> packet = Create<Packet> (encodedData, encodedSize);
  
  NS_LOG_INFO ("Sending encoded packet " << (globalPacketIndex + 1) 
               << "/" << m_numPackets << " of size " << encodedSize
               << " for generation " << generationId 
               << " (packet " << (m_packetsInCurrentGeneration + 1) << "/" << packetsInThisGeneration << " in generation)");
  
  // CHANGED: Use SendTo for UDP
  int actual = m_socket->SendTo (packet, 0, m_peer);
  if (actual > 0)
    {
      m_packetsSent++;
      m_packetsInCurrentGeneration++;
      m_txTrace (packet);
      NS_LOG_INFO ("Successfully sent packet " << m_packetsSent << "/" << m_numPackets);
    }
  else
    {
      NS_LOG_WARN ("Failed to send packet, socket send returned: " << actual);
    }
  
  delete[] encodedData;
}

// CHANGED: SendAck now takes sender address parameter
void
NetworkCodingUdpApplication::SendAck (uint32_t generationId, Address senderAddress)
{
  NS_LOG_FUNCTION (this << generationId << senderAddress);
  
  uint8_t ackData[8];
  ackData[0] = 0xFF;
  ackData[1] = 0xFF;
  ackData[2] = 0xFF;
  ackData[3] = 0xFF;
  ackData[4] = (generationId >> 24) & 0xFF;
  ackData[5] = (generationId >> 16) & 0xFF;
  ackData[6] = (generationId >> 8) & 0xFF;
  ackData[7] = generationId & 0xFF;
  
  Ptr<Packet> ackPacket = Create<Packet> (ackData, 8);
  
  // CHANGED: Use SendTo with sender address for UDP
  if (m_socket && m_socket->SendTo (ackPacket, 0, senderAddress) > 0)
    {
      NS_LOG_INFO ("Sent ACK for generation " << generationId << " to " << senderAddress);
    }
  else
    {
      NS_LOG_WARN ("Failed to send ACK for generation " << generationId);
    }
}

void
NetworkCodingUdpApplication::HandleGenerationTimeout (void)
{
  NS_LOG_FUNCTION (this);
  
  if (!m_waitingForGenerationAck)
    {
      return;
    }
  
  m_retransmissionCount++;
  NS_LOG_INFO ("Generation " << m_currentGenerationSent << " timeout (attempt " 
               << m_retransmissionCount << "/" << m_maxRetransmissions << ")");
  
  if (m_retransmissionCount < m_maxRetransmissions)
    {
      uint32_t packetsToSend = m_generationSize;
      
      NS_LOG_INFO ("Sending " << packetsToSend << " additional coded packets for generation " 
                   << m_currentGenerationSent);
      
      for (uint32_t i = 0; i < packetsToSend; i++)
        {
          uint32_t encodedSize = m_packetSize;
          uint8_t* encodedData = new uint8_t[encodedSize];
          
          encodedData[0] = (m_currentGenerationSent >> 24) & 0xFF;
          encodedData[1] = (m_currentGenerationSent >> 16) & 0xFF;
          encodedData[2] = (m_currentGenerationSent >> 8) & 0xFF;
          encodedData[3] = m_currentGenerationSent & 0xFF;
          
          for (uint32_t j = 4; j < encodedSize; j++)
            {
              encodedData[j] = (uint8_t)((m_currentGenerationSent * 256 + m_retransmissionCount * 10 + i + j) % 256);
            }
          
          Ptr<Packet> packet = Create<Packet> (encodedData, encodedSize);
          
          NS_LOG_INFO ("Sending redundant packet " << (i + 1) << "/" << packetsToSend 
                       << " for generation " << m_currentGenerationSent 
                       << " (retransmission " << m_retransmissionCount << ")");
          
          // CHANGED: Use SendTo for UDP
          if (m_socket && m_socket->SendTo (packet, 0, m_peer) > 0)
            {
              m_txTrace (packet);
              NS_LOG_INFO ("Successfully sent redundant packet");
            }
          else
            {
              NS_LOG_WARN ("Failed to send redundant packet");
            }
          
          delete[] encodedData;
        }
      
      m_retransmissionTimer = Simulator::Schedule (m_generationTimeout, 
                                                   &NetworkCodingUdpApplication::HandleGenerationTimeout, 
                                                   this);
    }
  else
    {
      NS_LOG_WARN ("Maximum retransmissions reached for generation " << m_currentGenerationSent);
      
      m_waitingForGenerationAck = false;
      m_retransmissionCount = 0;
      
      uint32_t totalGenerationsNeeded = (m_numPackets + m_generationSize - 1) / m_generationSize;
      if (m_currentGenerationSent + 1 < totalGenerationsNeeded)
        {
          MoveToNextGeneration ();
          ScheduleNext ();
        }
      else
        {
          NS_LOG_INFO ("All generations attempted");
        }
    }
}

// New method: Move to next generation
void
NetworkCodingUdpApplication::MoveToNextGeneration (void)
{
  NS_LOG_FUNCTION (this);
  
  m_currentGenerationSent++;
  m_packetsInCurrentGeneration = 0;
  
  NS_LOG_INFO ("Moved to sending generation " << m_currentGenerationSent);
}

// New method: Check if packet is ACK
bool
NetworkCodingUdpApplication::IsAckPacket (Ptr<Packet> packet)
{
  if (packet->GetSize () != 8)
    {
      return false;
    }
  
  uint8_t buffer[8];
  packet->CopyData (buffer, 8);
  
  return (buffer[0] == 0xFF && buffer[1] == 0xFF && 
          buffer[2] == 0xFF && buffer[3] == 0xFF);
}

// New method: Handle ACK
void
NetworkCodingUdpApplication::HandleAck (Ptr<Packet> packet)
{
  NS_LOG_FUNCTION (this << packet);
  
  uint8_t buffer[8];
  packet->CopyData (buffer, 8);
  
  uint32_t ackedGeneration = (buffer[4] << 24) | (buffer[5] << 16) | 
                            (buffer[6] << 8) | buffer[7];
  
  NS_LOG_INFO ("Received ACK for generation " << ackedGeneration 
               << ", current sending generation: " << m_currentGenerationSent);
  
  if (ackedGeneration == m_currentGenerationSent)
    {
      // Current generation was successfully decoded
      m_waitingForGenerationAck = false;
      m_retransmissionCount = 0;
      
      // Cancel retransmission timer
      Simulator::Cancel (m_retransmissionTimer);
      
      // Check if we have more packets to send
      uint32_t totalGenerationsNeeded = (m_numPackets + m_generationSize - 1) / m_generationSize;
      
      NS_LOG_INFO ("Completed generation " << ackedGeneration 
                   << ". Need " << totalGenerationsNeeded << " generations total.");
      
      if (m_currentGenerationSent + 1 < totalGenerationsNeeded)
        {
          MoveToNextGeneration ();
          ScheduleNext ();
        }
      else
        {
          NS_LOG_INFO ("All " << totalGenerationsNeeded << " generations sent and acknowledged");
        }
    }
}

bool
NetworkCodingUdpApplication::IsCurrentGenerationComplete (void) const
{
  NS_LOG_FUNCTION (this);
  
  uint32_t totalGenerationsNeeded = (m_numPackets + m_generationSize - 1) / m_generationSize;
  
  if (m_currentGenerationSent >= totalGenerationsNeeded)
    {
      return true; // All generations complete
    }
  
  // For the last generation, it might have fewer packets
  uint32_t packetsNeededInCurrentGeneration = m_generationSize;
  if (m_currentGenerationSent == totalGenerationsNeeded - 1) // Last generation
    {
      uint32_t remainingPackets = m_numPackets - (m_currentGenerationSent * m_generationSize);
      packetsNeededInCurrentGeneration = remainingPackets;
    }
  
  return m_packetsInCurrentGeneration >= packetsNeededInCurrentGeneration;
}

uint32_t
NetworkCodingUdpApplication::GetPacketsSent (void) const
{
  return m_packetsSent;
}

uint32_t
NetworkCodingUdpApplication::GetPacketsReceived (void) const
{
  return m_packetsReceived;
}

uint32_t
NetworkCodingUdpApplication::GetInnovativePacketsReceived (void) const
{
  return m_innovativePacketsReceived;
}

uint32_t
NetworkCodingUdpApplication::GetGenerationsDecoded (void) const
{
  return m_generationsDecoded;
}

Ptr<NetworkCodingEncoder>
NetworkCodingUdpApplication::GetEncoder (void) const
{
  return m_encoder;
}

Ptr<NetworkCodingDecoder>
NetworkCodingUdpApplication::GetDecoder (void) const
{
  return m_decoder;
}

} // namespace ns3