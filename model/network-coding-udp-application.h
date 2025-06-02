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

#ifndef NETWORK_CODING_UDP_APPLICATION_H
#define NETWORK_CODING_UDP_APPLICATION_H

#include "ns3/application.h"
#include "ns3/event-id.h"
#include "ns3/ptr.h"
#include "ns3/data-rate.h"
#include "ns3/traced-callback.h"
#include "network-coding-encoder.h"
#include "network-coding-decoder.h"
#include <set>
#include <vector>

namespace ns3 {

class Socket;
class Packet;

/**
 * \ingroup network-coding
 * \brief A udp application using network coding
 *
 * This application can act as both sender and receiver of network-coded data.
 * When configured as a sender (numPackets > 0), it encodes packets into generations
 * and sends them. When configured as a receiver (numPackets = 0), it receives
 * and decodes the packets.
 */
class NetworkCodingUdpApplication : public Application
{
public:
  /**
   * \brief Get the type ID.
   * \return the object TypeId
   */
  static TypeId GetTypeId (void);

  NetworkCodingUdpApplication ();
  virtual ~NetworkCodingUdpApplication ();
  
  void Setup (Ptr<Socket> socket, Address address, uint16_t packetSize, 
              uint32_t numPackets, uint16_t generationSize, 
              DataRate dataRate, double lossRate);
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
  void SendPacket (void);
  void ScheduleNext (void);
  void HandleRead (Ptr<Socket> socket);
  void HandleAccept (Ptr<Socket> socket, const Address& from);
  void ConnectionSucceeded (Ptr<Socket> socket);
  void ConnectionFailed (Ptr<Socket> socket);
  void RequestMissingPackets (void);
  bool HandleConnectionRequest (Ptr<Socket> socket, const Address& from);
  bool ProcessReceivedPacket (Ptr<Packet> packet);
  
  // ADD THESE NEW METHOD DECLARATIONS:
  void SendCodedPacket (uint32_t generationId);
  bool IsCurrentGenerationComplete (void) const;
  void MoveToNextGeneration (void);
  void HandleGenerationTimeout (void);
  void SendAck (uint32_t generationId, Address senderAddress); // CHANGED: Added senderAddress parameter
  bool IsAckPacket (Ptr<Packet> packet);
  void HandleAck (Ptr<Packet> packet);
  
  // Network parameters  
  Ptr<Socket> m_socket;           //!< Associated socket
  Address m_peer;
  uint16_t m_packetSize;
  uint32_t m_numPackets;
  uint16_t m_generationSize;
  DataRate m_dataRate;
  double m_lossRate;
  bool m_running;
  EventId m_sendEvent;
  
  // Statistics
  uint32_t m_packetsSent;
  uint32_t m_packetsReceived;
  uint32_t m_innovativePacketsReceived;
  uint32_t m_generationsDecoded;
  uint32_t m_nextSeq;
  std::set<uint32_t> m_receivedPackets;
  std::vector<uint8_t> m_receiveBuffer;
  
  // Network coding components
  Ptr<NetworkCodingEncoder> m_encoder;
  Ptr<NetworkCodingDecoder> m_decoder;
  
  // Generation tracking
  uint32_t m_currentGeneration;
  uint32_t m_generationPacketCount;
  uint32_t m_currentGenerationSent;
  uint32_t m_packetsInCurrentGeneration;
  bool m_waitingForGenerationAck;
  EventId m_retransmissionTimer;
  Time m_generationTimeout;
  uint32_t m_maxRetransmissions;
  uint32_t m_retransmissionCount;
  
  // Trace sources
  TracedCallback<Ptr<const Packet>> m_txTrace;
  TracedCallback<Ptr<const Packet>> m_rxTrace;
  TracedCallback<bool, uint32_t> m_decodingTrace;
  
  // Additional methods for network coding support
  bool ProcessReceiveBuffer(void);
  bool ProcessNetworkCodedPacket(Ptr<Packet> packet);
};

} // namespace ns3

#endif /* NETWORK_CODING_UDP_APPLICATION_H */