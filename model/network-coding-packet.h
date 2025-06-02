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
#include "ns3/buffer.h"
#include <vector>

namespace ns3 {

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
   * \brief Get the instance TypeId
   * \return the instance TypeId
   */
  virtual TypeId GetInstanceTypeId (void) const;

  /**
   * \brief Default constructor
   */
  NetworkCodingHeader ();
  
  /**
   * \brief Destructor
   */
  virtual ~NetworkCodingHeader ();

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
  
  /**
   * \brief Get the coding coefficients
   * \return the coding coefficients
   */
  const std::vector<uint8_t>& GetCoefficients (void) const;

  /**
   * \brief Get the size of this header
   * \return the size in bytes
   */
  virtual uint32_t GetSerializedSize (void) const;
  
  /**
   * \brief Serialize the header to a buffer
   * \param start the buffer to serialize to
   */
  virtual void Serialize (Buffer::Iterator start) const;
  
  /**
   * \brief Deserialize the header from a buffer
   * \param start the buffer to deserialize from
   * \return the number of bytes consumed
   */
  virtual uint32_t Deserialize (Buffer::Iterator start);
  
  /**
   * \brief Print the header contents
   * \param os the output stream to print to
   */
  virtual void Print (std::ostream &os) const;

private:
  uint32_t m_generationId;                 //!< Generation ID
  uint16_t m_generationSize;               //!< Generation size
  std::vector<uint8_t> m_coefficients;     //!< Coding coefficients
};

/**
 * \ingroup network-coding
 * \brief Header for network coding control packets (rerequest missing packets)
 */
class NetworkCodingControlHeader : public Header
{
public:
  /**
   * \brief Control packet types
   */
  enum ControlType
  {
    REQUEST_UNCODED = 1,   //!< Request uncoded packets
    ACKNOWLEDGE = 2        //!< Acknowledge packets
  };

  /**
   * \brief Get the TypeId
   * \return the TypeId for this class
   */
  static TypeId GetTypeId (void);
  
  /**
   * \brief Get the instance TypeId
   * \return the instance TypeId
   */
  virtual TypeId GetInstanceTypeId (void) const;

  /**
   * \brief Default constructor
   */
  NetworkCodingControlHeader ();
  
  /**
   * \brief Destructor
   */
  virtual ~NetworkCodingControlHeader ();

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
  
  /**
   * \brief Get the packet sequence numbers
   * \return the packet sequence numbers
   */
  const std::vector<uint32_t>& GetSequenceNumbers (void) const;

  /**
   * \brief Get the size of this header
   * \return the size in bytes
   */
  virtual uint32_t GetSerializedSize (void) const;
  
  /**
   * \brief Serialize the header to a buffer
   * \param start the buffer to serialize to
   */
  virtual void Serialize (Buffer::Iterator start) const;
  
  /**
   * \brief Deserialize the header from a buffer
   * \param start the buffer to deserialize from
   * \return the number of bytes consumed
   */
  virtual uint32_t Deserialize (Buffer::Iterator start);
  
  /**
   * \brief Print the header contents
   * \param os the output stream to print to
   */
  virtual void Print (std::ostream &os) const;

private:
  ControlType m_controlType;               //!< Control packet type
  uint32_t m_generationId;                 //!< Generation ID
  std::vector<uint32_t> m_sequenceNumbers; //!< Packet sequence numbers
};

} // namespace ns3

#endif /* NETWORK_CODING_PACKET_H */