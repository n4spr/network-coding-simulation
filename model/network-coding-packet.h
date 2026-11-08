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

#ifndef NETWORK_CODING_PACKET_H
#define NETWORK_CODING_PACKET_H

#include "ns3/header.h"
#include "ns3/ipv4-address.h"
#include <vector>
#include <set>
#include <cstdint>

namespace ns3 {

struct ButterflyHeaderInfo
{
  uint32_t butterflyId = 0;
  uint32_t sourceNode = 0;
  uint32_t r1 = 0;
  uint32_t r2 = 0;
  uint32_t r3 = 0;
  uint32_t r4 = 0;
  uint32_t d1 = 0;
  uint32_t d2 = 0;
  uint32_t targetExitNode = 0;
};

enum ButterflyHopRole : uint8_t
{
  BUTTERFLY_HOP_NONE = 0,
  BUTTERFLY_HOP_SOURCE_TO_R1 = 1,
  BUTTERFLY_HOP_SOURCE_TO_R2 = 2,
  BUTTERFLY_HOP_R1_TO_R3 = 3,
  BUTTERFLY_HOP_R2_TO_R3 = 4,
  BUTTERFLY_HOP_R1_TO_D1 = 5,
  BUTTERFLY_HOP_R2_TO_D2 = 6,
  BUTTERFLY_HOP_R3_TO_R4 = 7,
  BUTTERFLY_HOP_R4_TO_D1 = 8,
  BUTTERFLY_HOP_R4_TO_D2 = 9
};

/**
 * \ingroup network-coding
 * \brief Header for network coded packets
 *
 * This header includes the information needed to process network coded packets:
 * - Generation ID: to identify which generation the packet belongs to
 * - Generation Size: number of packets in the generation
 * - Coding Coefficients: the coefficients used to encode the packet
 */
class NetworkCodingHeader : public Header
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
  NetworkCodingHeader ();
  
  /**
   * \brief Destructor
   */
  virtual ~NetworkCodingHeader ();

  /**
   * \brief Get the instance TypeId
   * \return the instance TypeId
   */
  virtual TypeId GetInstanceTypeId (void) const override;

  /**
   * \brief Get the serialized size
   * \return the size in bytes
   */
  virtual uint32_t GetSerializedSize (void) const override;

  /**
   * \brief Serialize the header
   * \param start the buffer iterator to start serialization
   */
  virtual void Serialize (Buffer::Iterator start) const override;

  /**
   * \brief Deserialize the header
   * \param start the buffer iterator to start deserialization
   * \return the number of bytes deserialized
   */
  virtual uint32_t Deserialize (Buffer::Iterator start) override;
  
  /**
   * \brief Print the header contents
   * \param os the output stream to print to
   */
  virtual void Print (std::ostream &os) const override;

  /**
   * \brief Set the generation ID
   * \param genId the generation ID to set
   */
  void SetGenerationId (uint32_t genId);
  
  /**
   * \brief Get the generation ID
   * \return the generation ID
   */
  uint32_t GetGenerationId (void) const;

  /**
   * \brief Set the generation size
   * \param genSize the generation size to set
   */
  void SetGenerationSize (uint16_t genSize);
  
  /**
   * \brief Get the generation size
   * \return the generation size
   */
  uint16_t GetGenerationSize (void) const;

  /**
   * \brief Set the coding coefficients
   * \param coeffs the coding coefficients to set
   */
  void SetCoefficients (const std::vector<uint8_t>& coeffs);
  const std::vector<uint8_t>& GetCoefficients (void) const;

  void SetHopSequence(uint64_t seq);
  uint64_t GetHopSequence() const;

  void SetFinalDestination(const Ipv4Address& addr);
  Ipv4Address GetFinalDestination() const;
  bool HasFinalDestination() const;

  void SetButterflyInfo(const ButterflyHeaderInfo& info);
  const ButterflyHeaderInfo& GetButterflyInfo() const;
  bool HasButterflyInfo() const;

  void SetButterflyHopRole(uint8_t role);
  uint8_t GetButterflyHopRole() const;

private:
  uint32_t m_generationId;
  uint16_t m_generationSize;
  std::vector<uint8_t> m_coefficients;
  uint64_t m_hopSequence; // Unique ID for hop-by-hop retransmissions
  Ipv4Address m_finalDestination;
  bool m_hasFinalDestination;
  ButterflyHeaderInfo m_butterflyInfo;
  bool m_hasButterflyInfo = false;
  uint8_t m_butterflyHopRole = BUTTERFLY_HOP_NONE;
};

/**
 * \ingroup network-coding
 * \brief Header for network coding control packets (rerequest missing packets)
 */
class NetworkCodingControlHeader : public Header
{
public:
  enum ControlType {
    REQUEST_UNCODED,
    ACKNOWLEDGE,
    INNOVATIVE_ACK,
    HOP_ACK // New type for hop-by-hop acknowledgment
  };

  NetworkCodingControlHeader (ControlType type, uint32_t genId);
  /**
   * \brief Get the TypeId
   * \return the TypeId for this class
   */
  static TypeId GetTypeId (void);
  
  /**
   * \brief Default constructor
   */
  NetworkCodingControlHeader ();
  
  /**
   * \brief Destructor
   */
  virtual ~NetworkCodingControlHeader ();

  /**
   * \brief Get the instance TypeId
   * \return the instance TypeId
   */
  virtual TypeId GetInstanceTypeId (void) const override;

  /**
   * \brief Get the serialized size
   * \return the size in bytes
   */
  virtual uint32_t GetSerializedSize (void) const override;

  /**
   * \brief Serialize the header
   * \param start the buffer iterator to start serialization
   */
  virtual void Serialize (Buffer::Iterator start) const override;

  /**
   * \brief Deserialize the header
   * \param start the buffer iterator to start deserialization
   * \return the number of bytes deserialized
   */
  virtual uint32_t Deserialize (Buffer::Iterator start) override;

  /**
   * \brief Print the header contents
   * \param os the output stream to print to
   */
  virtual void Print (std::ostream &os) const override;

  /**
   * \brief Set the control packet type
   * \param type the control packet type to set
   */
  void SetControlType (ControlType type);
  
  /**
   * \brief Get the control packet type
   * \return the control packet type
   */
  ControlType GetControlType (void) const;

  /**
   * \brief Set the generation ID
   * \param genId the generation ID to set
   */
  void SetGenerationId (uint32_t genId);
  
  /**
   * \brief Get the generation ID
   * \return the generation ID
   */
  uint32_t GetGenerationId (void) const;

  /**
   * \brief Set the packet sequence numbers
   * \param seqNums the packet sequence numbers to set
   */
  void SetSequenceNumbers (const std::vector<uint32_t>& seqNums);
  const std::vector<uint32_t>& GetSequenceNumbers (void) const;
  
  void SetHopAckSequence(uint64_t seq);
  uint64_t GetHopAckSequence() const;

private:
  ControlType m_controlType;
  uint32_t m_generationId;
  std::vector<uint32_t> m_sequenceNumbers;
  uint64_t m_hopAckSequence; // Sequence number for HOP_ACK
};

} // namespace ns3

#endif /* NETWORK_CODING_PACKET_H */