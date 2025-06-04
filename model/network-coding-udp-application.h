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

#ifndef NETWORK_CODING_UDP_APPLICATION_H
#define NETWORK_CODING_UDP_APPLICATION_H

#include "ns3/application.h"
#include "ns3/socket.h"
#include "ns3/data-rate.h"
#include "ns3/traced-callback.h"
#include "network-coding-encoder.h"
#include "network-coding-decoder.h"
#include "galois-field.h"

namespace ns3 {

/**
 * \brief A UDP application that implements Random Linear Network Coding
 */
class NetworkCodingUdpApplication : public Application
{
public:
  /**
   * \brief Get the type ID.
   * \return the object TypeId
   */
  static TypeId GetTypeId (void);

  /**
   * \brief Constructor
   */
  NetworkCodingUdpApplication ();

  /**
   * \brief Destructor
   */
  virtual ~NetworkCodingUdpApplication ();

  // Getter methods
  uint32_t GetPacketsSent (void) const;
  uint32_t GetPacketsReceived (void) const;
  uint32_t GetInnovativePacketsReceived (void) const;
  uint32_t GetGenerationsDecoded (void) const;
  Ptr<NetworkCodingEncoder> GetEncoder (void) const;
  Ptr<NetworkCodingDecoder> GetDecoder (void) const;

protected:
  virtual void DoDispose (void);

private:
  virtual void StartApplication (void);
  virtual void StopApplication (void);
  
  // FIXED: Core methods with generation management
  void GenerateOriginalPackets (void);
  void AddPacketsToCurrentGeneration (uint32_t generationId);
  void SendRealCodedPacket (uint32_t generationId);
  bool ProcessRealCodedPacket (Ptr<Packet> packet);
  void VerifyDecodedPackets (const std::vector<Ptr<Packet>>& decodedPackets,
                           uint32_t generationId);
  
  // Network communication
  void HandleRead (Ptr<Socket> socket);
  void ScheduleNext (void);
  void SendPacket (void);
  
  // Generation management
  void MoveToNextGeneration (void);
  bool IsCurrentGenerationComplete (void) const;
  
  // ACK handling
  bool IsAckPacket (Ptr<Packet> packet);
  void HandleAck (Ptr<Packet> packet);
  void SendAck (uint32_t generationId, Address senderAddress);
  
  // Reliability
  void HandleGenerationTimeout (void);

  // Socket and addressing
  Ptr<Socket> m_socket;
  Address m_peer;

  // Configuration
  uint16_t m_packetSize;
  uint32_t m_numPackets;
  uint16_t m_generationSize;
  DataRate m_dataRate;
  double m_lossRate;

  // State
  bool m_running;
  uint32_t m_packetsSent;
  uint32_t m_packetsReceived;
  uint32_t m_innovativePacketsReceived;
  uint32_t m_generationsDecoded;
  uint32_t m_nextSeq;

  // REAL Network Coding components
  Ptr<NetworkCodingEncoder> m_encoder;
  Ptr<NetworkCodingDecoder> m_decoder;

  // Generation tracking
  uint32_t m_currentGeneration;
  uint32_t m_generationPacketCount;
  uint32_t m_currentGenerationSent;
  uint32_t m_packetsInCurrentGeneration;

  // Reliability
  bool m_waitingForGenerationAck;
  Time m_generationTimeout;
  uint32_t m_maxRetransmissions;
  uint32_t m_retransmissionCount;
  Ptr<GaloisField> m_galoisField;

  // Events
  EventId m_sendEvent;
  EventId m_retransmissionTimer;

  // Tracing
  TracedCallback<Ptr<const Packet>> m_txTrace;
  TracedCallback<Ptr<const Packet>> m_rxTrace;
  TracedCallback<bool, uint32_t> m_decodingTrace;
};

} // namespace ns3

#endif /* NETWORK_CODING_UDP_APPLICATION_H */