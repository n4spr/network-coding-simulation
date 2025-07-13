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

#ifndef NETWORK_CODING_DECODER_H
#define NETWORK_CODING_DECODER_H

#include "ns3/object.h"
#include "ns3/packet.h"
#include "network-coding-packet.h"
#include "galois-field.h"
#include <vector>
#include <set>

namespace ns3 {

/**
 * \ingroup network-coding
 * \brief Network coding decoder for linear coding in GF(2^8)
 *
 * This class decodes network-coded packets using Gaussian elimination
 * in GF(2^8). It collects coded packets until the decoding matrix has 
 * full rank, then recovers the original packets.
 */
class NetworkCodingDecoder : public Object
{
public:
  /**
   * \brief Get the TypeId
   * \return the TypeId for this class
   */
  static TypeId GetTypeId (void);

  /**
   * \brief Default constructor
   */
  NetworkCodingDecoder ();
  
  /**
   * \brief Constructor with parameters
   * \param generationSize Size of each generation (number of packets)
   * \param packetSize Size of packets to decode
   */
  NetworkCodingDecoder (uint16_t generationSize, uint16_t packetSize);
  
  /**
   * \brief Destructor
   */
  virtual ~NetworkCodingDecoder ();

  /**
   * \brief Set the generation size
   * \param generationSize Size of each generation (number of packets)
   */
  void SetGenerationSize (uint16_t generationSize);
  
  /**
   * \brief Get the generation size
   * \return the generation size
   */
  uint16_t GetGenerationSize (void) const;

  /**
   * \brief Set the packet size
   * \param packetSize Size of packets to decode
   */
  void SetPacketSize (uint16_t packetSize);
  
  /**
   * \brief Get the packet size
   * \return the packet size
   */
  uint16_t GetPacketSize (void) const;

  bool ProcessCodedPacket (Ptr<const Packet> packet);
  bool CanDecode (void) const;
  uint16_t GetRank (void) const;
  
  std::vector<Ptr<Packet>> GetDecodedPackets (void);
  std::set<uint32_t> GetMissingPackets (void) const;

  void NextGeneration (void);
  uint32_t GetCurrentGenerationId (void) const;

private:
  void DecodeGeneration (void);
  uint16_t CalculateRank (const std::vector<std::vector<uint8_t>>& matrix) const;
  bool IsInnovative(const std::vector<uint8_t>& newCoeffs) const;

  uint16_t m_generationSize;
  uint16_t m_packetSize;
  uint32_t m_currentGeneration;            //!< Current generation ID
  bool m_decoded;

  std::set<uint32_t> m_receivedSequences;
  
  Ptr<GaloisField> m_gf;
  /**
   * \brief Coefficient matrix (row-major order)
   */
  std::vector<std::vector<uint8_t>> m_coefficients;
  
  /**
   * \brief Coded payloads corresponding to coefficient rows
   */
  std::vector<std::vector<uint8_t>> m_codedPayloads;
  
  /**
   * \brief Vector of decoded packets
   */
  std::vector<Ptr<Packet>> m_decodedPackets;
};

} // namespace ns3

#endif /* NETWORK_CODING_DECODER_H */