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
 */

#include "network-coding-udp-application.h"
#include "network-coding-packet.h"
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
    m_peer (),
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
    m_retransmissionCount (0),
    m_galoisField (CreateObject<GaloisField>())
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
  m_galoisField = nullptr;
  Simulator::Cancel (m_sendEvent);
  Simulator::Cancel (m_retransmissionTimer);
  Application::DoDispose ();
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
  
  // Initialize REAL encoder and decoder
  NS_LOG_INFO ("Initializing REAL encoder/decoder with generation size " << m_generationSize 
               << " and packet size " << m_packetSize);
  
  m_encoder = CreateObject<NetworkCodingEncoder> (m_generationSize, m_packetSize);
  m_decoder = CreateObject<NetworkCodingDecoder> (m_generationSize, m_packetSize);
  
  if (!m_encoder || !m_decoder) {
    NS_FATAL_ERROR ("Failed to create REAL encoder or decoder");
  }
  
  NS_LOG_INFO ("REAL Network Coding encoder and decoder initialized successfully");

  if (!m_socket) {
    m_socket = Socket::CreateSocket (GetNode (), UdpSocketFactory::GetTypeId ());
    
    bool isReceiver = (m_numPackets == 0);
    
    if (isReceiver) {
      NS_LOG_INFO ("Setting up as REAL NETWORK CODING RECEIVER");
      if (InetSocketAddress::IsMatchingType (m_peer)) {
        InetSocketAddress local = InetSocketAddress::ConvertFrom (m_peer);
        m_socket->Bind (InetSocketAddress (Ipv4Address::GetAny (), local.GetPort ()));
      } else if (Inet6SocketAddress::IsMatchingType (m_peer)) {
        Inet6SocketAddress local = Inet6SocketAddress::ConvertFrom (m_peer);
        m_socket->Bind (Inet6SocketAddress (Ipv6Address::GetAny (), local.GetPort ()));
      }
    } else {
      NS_LOG_INFO ("Setting up as REAL NETWORK CODING SENDER");
      m_socket->Bind ();
      
      if (m_numPackets > 0) {
        // FIXED: Generate packets per generation instead of all at once
        GenerateOriginalPackets();
        ScheduleNext ();
      }
    }
  }
  
  m_socket->SetRecvCallback (MakeCallback (&NetworkCodingUdpApplication::HandleRead, this));
}

// FIXED: Generate original packets per generation
void
NetworkCodingUdpApplication::GenerateOriginalPackets (void)
{
  NS_LOG_FUNCTION (this);
  
  if (!m_encoder) {
    NS_LOG_ERROR ("No encoder available for generating original packets");
    return;
  }
  
  NS_LOG_INFO ("Setting up encoder for generation-based packet creation...");
  
  // DON'T add all packets at once - add them per generation
  // Just add the first generation initially
  AddPacketsToCurrentGeneration(0);  // Start with generation 0
  
  NS_LOG_INFO ("Encoder prepared for generation-based coding");
}

// FIXED: Add packets for specific generation to encoder
void
NetworkCodingUdpApplication::AddPacketsToCurrentGeneration (uint32_t generationId)
{
  NS_LOG_FUNCTION (this << generationId);
  
  if (!m_encoder) {
    NS_LOG_ERROR ("No encoder available");
    return;
  }
  
  uint32_t startPacket = generationId * m_generationSize;
  uint32_t endPacket = std::min(startPacket + m_generationSize, m_numPackets);
  
  NS_LOG_INFO ("Adding packets " << startPacket << "-" << (endPacket-1) 
               << " to encoder for generation " << generationId);
  
  for (uint32_t i = startPacket; i < endPacket; i++) {
    // Create REAL packet data with identifiable pattern
    std::vector<uint8_t> data(m_packetSize);
    for (uint32_t j = 0; j < m_packetSize; j++) {
      data[j] = (uint8_t)((i * 123 + j * 7) % 256);  // Same pattern as before
    }
    
    Ptr<Packet> originalPacket = Create<Packet>(data.data(), m_packetSize);
    
    // Add to encoder with LOCAL sequence number (0, 1, 2, ...)
    uint32_t localSeq = i - startPacket;
    bool added = m_encoder->AddPacket(originalPacket, localSeq);
    
    if (added) {
      NS_LOG_INFO ("Added packet " << i << " (local_seq=" << localSeq 
                   << ") to encoder for generation " << generationId);
    } else {
      NS_LOG_ERROR ("Failed to add packet " << i << " to encoder");
    }
  }
}

void
NetworkCodingUdpApplication::StopApplication (void)
{
  NS_LOG_FUNCTION (this);
  
  m_running = false;
  
  Simulator::Cancel (m_sendEvent);
  Simulator::Cancel (m_retransmissionTimer);
  
  if (m_socket) {
    m_socket->Close ();
    m_socket->SetRecvCallback (MakeNullCallback<void, Ptr<Socket>> ());
  }
  
  NS_LOG_INFO ("REAL Network Coding Application stopped. Packets sent: " << m_packetsSent 
               << ", Packets received: " << m_packetsReceived
               << ", Innovative packets: " << m_innovativePacketsReceived
               << ", Generations decoded: " << m_generationsDecoded);
}

void
NetworkCodingUdpApplication::HandleRead (Ptr<Socket> socket)
{
  NS_LOG_FUNCTION (this << socket);
  
  Ptr<Packet> packet;
  Address from;
  
  while ((packet = socket->RecvFrom (from))) {
    if (packet->GetSize () == 0) {
      break;
    }
    
    m_packetsReceived++;
    m_rxTrace (packet);
    
    NS_LOG_INFO ("Received packet of size " << packet->GetSize () 
                 << " from " << from << ". Total received: " << m_packetsReceived);
    
    // Check if this is an ACK packet
    if (IsAckPacket (packet)) {
      HandleAck (packet);
      continue;
    }
    
    // Process as REAL coded packet
    bool isInnovative = ProcessRealCodedPacket (packet);
    
    if (isInnovative) {
      m_innovativePacketsReceived++;
      m_generationPacketCount++;
      NS_LOG_INFO ("Received INNOVATIVE coded packet. Total innovative: " 
                   << m_innovativePacketsReceived 
                   << ", Generation packets: " << m_generationPacketCount);
      
      // Check if we can decode the current generation
      if (m_decoder && m_decoder->CanDecode()) {
        m_generationsDecoded++;
        m_decodingTrace (true, m_generationsDecoded);
        
        // REAL DECODING HAPPENED!
        auto decodedPackets = m_decoder->GetDecodedPackets();
        
        NS_LOG_INFO ("*** GENERATION " << m_currentGeneration 
                     << " SUCCESSFULLY DECODED! ***");
        NS_LOG_INFO ("Recovered " << decodedPackets.size() << " original packets");
        
        // Verify decoded packets
        VerifyDecodedPackets(decodedPackets, m_currentGeneration);
        
        // Send ACK to sender
        SendAck (m_currentGeneration, from);
        
        // FIXED: Don't advance generation here - let ProcessRealCodedPacket handle it
        // Reset packet count for current generation
        m_generationPacketCount = 0;
        
        // FIXED: Don't create new decoder here - let ProcessRealCodedPacket manage it
      }
    } else {
      NS_LOG_INFO ("Received NON-INNOVATIVE coded packet (redundant)");
    }
  }
}

// FIXED: Process REAL coded packets using decoder
bool
NetworkCodingUdpApplication::ProcessRealCodedPacket (Ptr<Packet> packet)
{
  NS_LOG_FUNCTION (this << packet);
  
  if (!m_decoder) {
    NS_LOG_ERROR ("No decoder available");
    return false;
  }
  
  // Make a copy to extract header
  Ptr<Packet> packetCopy = packet->Copy();
  
  // Extract network coding header
  NetworkCodingHeader header;
  try {
    packetCopy->RemoveHeader(header);
  } catch (...) {
    NS_LOG_ERROR ("Failed to extract network coding header from received packet");
    return false;
  }
  
  uint32_t generationId = header.GetGenerationId();
  const std::vector<uint8_t>& coefficients = header.GetCoefficients();
  
  NS_LOG_INFO ("Processing REAL coded packet for generation " << generationId 
               << " with coefficients [");
  for (size_t i = 0; i < coefficients.size(); i++) {
    std::cout << (int)coefficients[i];
    if (i < coefficients.size() - 1) std::cout << ",";
  }
  std::cout << "]" << std::endl;
  
  // CRITICAL FIX: Handle generation transitions properly
  if (generationId != m_currentGeneration) {
    if (generationId > m_currentGeneration) {
      // Advance to the new generation
      NS_LOG_INFO ("Advancing receiver from generation " << m_currentGeneration 
                   << " to generation " << generationId);
      
      // Move to the correct generation
      while (m_currentGeneration < generationId) {
        m_currentGeneration++;
        m_generationPacketCount = 0;
        
        // Create fresh decoder for the new generation
        m_decoder = CreateObject<NetworkCodingDecoder> (m_generationSize, m_packetSize);
        
        // Advance decoder's internal generation counter
        for (uint32_t i = 0; i < m_currentGeneration; i++) {
          m_decoder->NextGeneration();
        }
      }
      
      NS_LOG_INFO ("Receiver now at generation " << m_currentGeneration);
    } else {
      // Old generation packet - ignore
      NS_LOG_INFO ("Received packet from old generation " << generationId 
                   << " (current " << m_currentGeneration << "), ignoring");
      return false;
    }
  }
  
  // Use REAL decoder to process coded packet
  bool innovative = m_decoder->ProcessCodedPacket(packet);
  
  NS_LOG_INFO ("Decoder processed packet: innovative = " << innovative 
               << ", can decode = " << m_decoder->CanDecode());
  
  return innovative;
}

// FIXED: Verify that decoded packets match original patterns
void
NetworkCodingUdpApplication::VerifyDecodedPackets (const std::vector<Ptr<Packet>>& decodedPackets,
                                                  uint32_t generationId)
{
  NS_LOG_FUNCTION (this << decodedPackets.size() << generationId);
  
  for (size_t i = 0; i < decodedPackets.size(); i++) {
    uint32_t originalSeq = generationId * m_generationSize + i;
    
    // Extract decoded data
    std::vector<uint8_t> decodedData(m_packetSize);
    decodedPackets[i]->CopyData(decodedData.data(), m_packetSize);
    
    // Generate expected original pattern
    std::vector<uint8_t> expectedData(m_packetSize);
    for (uint32_t j = 0; j < m_packetSize; j++) {
      expectedData[j] = (uint8_t)((originalSeq * 123 + j * 7) % 256);
    }
    
    // Verify match
    bool matches = (decodedData == expectedData);
    
    NS_LOG_INFO ("Decoded packet " << i << " (seq=" << originalSeq 
                 << "): " << (matches ? "CORRECT" : "INCORRECT"));
    
    if (!matches) {
      NS_LOG_ERROR ("VERIFICATION FAILED for packet " << originalSeq);
      // Print first few bytes for debugging
      std::cout << "Expected: [";
      for (size_t k = 0; k < std::min(size_t(8), expectedData.size()); k++) {
        std::cout << (int)expectedData[k];
        if (k < 7 && k < expectedData.size() - 1) std::cout << ",";
      }
      std::cout << "]" << std::endl;
      
      std::cout << "Decoded:  [";
      for (size_t k = 0; k < std::min(size_t(8), decodedData.size()); k++) {
        std::cout << (int)decodedData[k];
        if (k < 7 && k < decodedData.size() - 1) std::cout << ",";
      }
      std::cout << "]" << std::endl;
    }
  }
}

void
NetworkCodingUdpApplication::ScheduleNext (void)
{
  NS_LOG_FUNCTION (this);
  
  uint32_t totalGenerationsNeeded = (m_numPackets + m_generationSize - 1) / m_generationSize;
  
  bool hasMoreGenerations = (m_currentGenerationSent < totalGenerationsNeeded);
  bool hasMorePacketsInCurrentGeneration = false;
  
  if (hasMoreGenerations) {
    uint32_t packetsNeededInCurrentGeneration = m_generationSize;
    if (m_currentGenerationSent == totalGenerationsNeeded - 1) {
      uint32_t remainingPackets = m_numPackets - (m_currentGenerationSent * m_generationSize);
      packetsNeededInCurrentGeneration = remainingPackets;
    }
    hasMorePacketsInCurrentGeneration = (m_packetsInCurrentGeneration < packetsNeededInCurrentGeneration);
  }
  
  NS_LOG_INFO ("ScheduleNext: Generation " << m_currentGenerationSent 
               << "/" << totalGenerationsNeeded 
               << ", packets in current gen: " << m_packetsInCurrentGeneration
               << ", waiting for ACK: " << m_waitingForGenerationAck);
  
  if (m_running && hasMoreGenerations && hasMorePacketsInCurrentGeneration && !m_waitingForGenerationAck) {
    Time tNext = Seconds (m_packetSize * 8 / static_cast<double> (m_dataRate.GetBitRate ()));
    m_sendEvent = Simulator::Schedule (tNext, &NetworkCodingUdpApplication::SendPacket, this);
    NS_LOG_DEBUG ("Scheduled next REAL coded packet in " << tNext.GetSeconds () << " seconds");
  } else if (!hasMoreGenerations) {
    NS_LOG_INFO ("All " << totalGenerationsNeeded << " generations completed");
  }
}

void
NetworkCodingUdpApplication::SendPacket (void)
{
  NS_LOG_FUNCTION (this);
  
  uint32_t totalGenerationsNeeded = (m_numPackets + m_generationSize - 1) / m_generationSize;
  
  if (!m_running || !m_socket || m_currentGenerationSent >= totalGenerationsNeeded) {
    return;
  }
  
  if (m_waitingForGenerationAck && IsCurrentGenerationComplete ()) {
    NS_LOG_INFO ("Waiting for ACK for generation " << m_currentGenerationSent);
    return;
  }
  
  SendRealCodedPacket (m_currentGenerationSent);
  
  if (IsCurrentGenerationComplete ()) {
    m_waitingForGenerationAck = true;
    
    m_retransmissionTimer = Simulator::Schedule (m_generationTimeout, 
                                                 &NetworkCodingUdpApplication::HandleGenerationTimeout, 
                                                 this);
    
    NS_LOG_INFO ("Completed generation " << m_currentGenerationSent << ", waiting for ACK");
  } else {
    ScheduleNext ();
  }
}

// FIXED: Send REAL coded packets using encoder with correct generation
void
NetworkCodingUdpApplication::SendRealCodedPacket (uint32_t generationId)
{
  NS_LOG_FUNCTION (this << generationId);
  
  if (!m_encoder) {
    NS_LOG_ERROR ("No encoder available for sending coded packets");
    return;
  }
  
  uint32_t totalGenerationsNeeded = (m_numPackets + m_generationSize - 1) / m_generationSize;
  if (generationId >= totalGenerationsNeeded) {
    NS_LOG_INFO ("All generations completed, not sending more packets");
    return;
  }
  
  uint32_t packetsInThisGeneration = m_generationSize;
  if (generationId == totalGenerationsNeeded - 1) {
    uint32_t remainingPackets = m_numPackets - (generationId * m_generationSize);
    packetsInThisGeneration = remainingPackets;
  }
  
  // FIXED: Allow retransmissions even if initial packets were sent
  // Remove this blocking condition:
  // if (m_packetsInCurrentGeneration >= packetsInThisGeneration) {
  //   NS_LOG_INFO ("Generation " << generationId << " already complete");
  //   return;
  // }
  
  // Generate REAL coded packet using encoder
  Ptr<Packet> codedPacket = m_encoder->GenerateCodedPacket();
  
  if (!codedPacket) {
    NS_LOG_ERROR ("Failed to generate coded packet from encoder");
    return;
  }
  
  // Update generation ID in packet header
  Ptr<Packet> packetCopy = codedPacket->Copy();
  NetworkCodingHeader header;
  packetCopy->RemoveHeader(header);
  header.SetGenerationId(generationId);
  packetCopy->AddHeader(header);
  
  NS_LOG_INFO ("Sending REAL coded packet for generation " << generationId
               << " (retransmission allowed)");
  
  // Send the packet
  int actual = m_socket->SendTo (packetCopy, 0, m_peer);
  if (actual > 0) {
    m_packetsSent++;
    // FIXED: Only count initial packets, not retransmissions
    if (!m_waitingForGenerationAck) {
      m_packetsInCurrentGeneration++;
    }
    m_txTrace (packetCopy);
  }
}

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
  
  if (m_socket && m_socket->SendTo (ackPacket, 0, senderAddress) > 0) {
    NS_LOG_INFO ("Sent ACK for DECODED generation " << generationId << " to " << senderAddress);
  } else {
    NS_LOG_WARN ("Failed to send ACK for generation " << generationId);
  }
}

void
NetworkCodingUdpApplication::HandleGenerationTimeout (void)
{
  NS_LOG_FUNCTION (this);
  
  if (!m_waitingForGenerationAck) {
    NS_LOG_INFO ("Generation " << m_currentGenerationSent << " already complete");
    return;
  }
  
  m_retransmissionCount++;
  NS_LOG_INFO ("Generation " << m_currentGenerationSent << " timeout (attempt " 
               << m_retransmissionCount << "/" << m_maxRetransmissions << ")");
  
  if (m_retransmissionCount < m_maxRetransmissions) {
    // FIXED: Send additional REAL coded packets for retransmission
    NS_LOG_INFO ("Retransmitting packets for generation " << m_currentGenerationSent);
    
    // Send more coded packets to overcome packet loss
    uint32_t packetsToSend = m_generationSize; // Send at least generation size packets
    for (uint32_t i = 0; i < packetsToSend; i++) {
      SendRealCodedPacket(m_currentGenerationSent);
    }
    
    // Schedule next timeout
    m_retransmissionTimer = Simulator::Schedule (m_generationTimeout, 
                                                 &NetworkCodingUdpApplication::HandleGenerationTimeout, 
                                                 this);
  } else {
    NS_LOG_WARN ("Maximum retransmissions reached for generation " << m_currentGenerationSent);
    
    m_waitingForGenerationAck = false;
    m_retransmissionCount = 0;
    
    uint32_t totalGenerationsNeeded = (m_numPackets + m_generationSize - 1) / m_generationSize;
    if (m_currentGenerationSent + 1 < totalGenerationsNeeded) {
      MoveToNextGeneration ();
      ScheduleNext ();
    } else {
      NS_LOG_INFO ("All generations attempted");
    }
  }
}

// FIXED: Move to next generation and set up encoder
void
NetworkCodingUdpApplication::MoveToNextGeneration (void)
{
  NS_LOG_FUNCTION (this);
  
  m_currentGenerationSent++;
  m_packetsInCurrentGeneration = 0;
  
  // CRITICAL FIX: Advance encoder to next generation!
  if (m_encoder) {
    m_encoder->NextGeneration();
    
    // Add packets for the new generation
    uint32_t totalGenerationsNeeded = (m_numPackets + m_generationSize - 1) / m_generationSize;
    if (m_currentGenerationSent < totalGenerationsNeeded) {
      AddPacketsToCurrentGeneration(m_currentGenerationSent);
    }
  }
  
  NS_LOG_INFO ("Moved to sending generation " << m_currentGenerationSent);
}

bool
NetworkCodingUdpApplication::IsAckPacket (Ptr<Packet> packet)
{
  if (packet->GetSize () != 8) {
    return false;
  }
  
  uint8_t buffer[8];
  packet->CopyData (buffer, 8);
  
  return (buffer[0] == 0xFF && buffer[1] == 0xFF && 
          buffer[2] == 0xFF && buffer[3] == 0xFF);
}

void
NetworkCodingUdpApplication::HandleAck (Ptr<Packet> packet)
{
  NS_LOG_FUNCTION (this << packet);
  
  uint8_t buffer[8];
  packet->CopyData (buffer, 8);
  
  uint32_t ackedGeneration = (buffer[4] << 24) | (buffer[5] << 16) | 
                            (buffer[6] << 8) | buffer[7];
  
  NS_LOG_INFO ("Received ACK for DECODED generation " << ackedGeneration 
               << ", current sending generation: " << m_currentGenerationSent);
  
  if (ackedGeneration == m_currentGenerationSent) {
    m_waitingForGenerationAck = false;
    m_retransmissionCount = 0;
    
    Simulator::Cancel (m_retransmissionTimer);
    
    uint32_t totalGenerationsNeeded = (m_numPackets + m_generationSize - 1) / m_generationSize;
    
    NS_LOG_INFO ("*** GENERATION " << ackedGeneration << " SUCCESSFULLY ACKNOWLEDGED ***");
    
    if (m_currentGenerationSent + 1 < totalGenerationsNeeded) {
      MoveToNextGeneration ();
      ScheduleNext ();
    } else {
      NS_LOG_INFO ("*** ALL " << totalGenerationsNeeded 
                   << " GENERATIONS SENT AND ACKNOWLEDGED! ***");
    }
  }
}

bool
NetworkCodingUdpApplication::IsCurrentGenerationComplete (void) const
{
  NS_LOG_FUNCTION (this);
  
  uint32_t totalGenerationsNeeded = (m_numPackets + m_generationSize - 1) / m_generationSize;
  
  if (m_currentGenerationSent >= totalGenerationsNeeded) {
    return true;
  }
  
  uint32_t packetsNeededInCurrentGeneration = m_generationSize;
  if (m_currentGenerationSent == totalGenerationsNeeded - 1) {
    uint32_t remainingPackets = m_numPackets - (m_currentGenerationSent * m_generationSize);
    packetsNeededInCurrentGeneration = remainingPackets;
  }
  
  return m_packetsInCurrentGeneration >= packetsNeededInCurrentGeneration;
}

// Getter methods
uint32_t NetworkCodingUdpApplication::GetPacketsSent (void) const { return m_packetsSent; }
uint32_t NetworkCodingUdpApplication::GetPacketsReceived (void) const { return m_packetsReceived; }
uint32_t NetworkCodingUdpApplication::GetInnovativePacketsReceived (void) const { return m_innovativePacketsReceived; }
uint32_t NetworkCodingUdpApplication::GetGenerationsDecoded (void) const { return m_generationsDecoded; }
Ptr<NetworkCodingEncoder> NetworkCodingUdpApplication::GetEncoder (void) const { return m_encoder; }
Ptr<NetworkCodingDecoder> NetworkCodingUdpApplication::GetDecoder (void) const { return m_decoder; }

} // namespace ns3