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

#include "network-coding-packet.h"
#include "ns3/log.h"

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("NetworkCodingPacket");
NS_OBJECT_ENSURE_REGISTERED (NetworkCodingHeader);
NS_OBJECT_ENSURE_REGISTERED (NetworkCodingControlHeader);

//----------------------------------------------------------------------------
// NetworkCodingHeader Implementation
//----------------------------------------------------------------------------

TypeId 
NetworkCodingHeader::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::NetworkCodingHeader")
    .SetParent<Header> ()
    .SetGroupName ("NetworkCoding")
    .AddConstructor<NetworkCodingHeader> ()
  ;
  return tid;
}

TypeId 
NetworkCodingHeader::GetInstanceTypeId (void) const
{
  return GetTypeId ();
}

NetworkCodingHeader::NetworkCodingHeader ()
  : m_generationId (0),
    m_generationSize (0)
{
}

NetworkCodingHeader::~NetworkCodingHeader ()
{
}

void 
NetworkCodingHeader::SetGenerationId (uint32_t genId)
{
  m_generationId = genId;
}

uint32_t 
NetworkCodingHeader::GetGenerationId (void) const
{
  return m_generationId;
}

void 
NetworkCodingHeader::SetGenerationSize (uint16_t genSize)
{
  m_generationSize = genSize;
}

uint16_t 
NetworkCodingHeader::GetGenerationSize (void) const
{
  return m_generationSize;
}

void 
NetworkCodingHeader::SetCoefficients (const std::vector<uint8_t>& coeffs)
{
  m_coefficients = coeffs;
}

const std::vector<uint8_t>& 
NetworkCodingHeader::GetCoefficients (void) const
{
  return m_coefficients;
}

void 
NetworkCodingHeader::Serialize (Buffer::Iterator start) const
{
  NS_LOG_FUNCTION (this);
  
  // Write generation ID (4 bytes)
  start.WriteHtonU32 (m_generationId);
  
  // Write generation size (2 bytes)  
  start.WriteHtonU16 (m_generationSize);
  
  // Write number of coefficients (2 bytes)
  start.WriteHtonU16 (static_cast<uint16_t>(m_coefficients.size()));
  
  // Write coefficients (pad to generation size)
  for (size_t i = 0; i < m_generationSize; i++)
    {
      if (i < m_coefficients.size())
        {
          start.WriteU8 (m_coefficients[i]);
        }
      else
        {
          start.WriteU8 (0);  // Pad with zeros
        }
    }
}

uint32_t 
NetworkCodingHeader::Deserialize (Buffer::Iterator start)
{
  NS_LOG_FUNCTION (this);
  
  // Read generation ID
  m_generationId = start.ReadNtohU32 ();
  
  // Read generation size
  m_generationSize = start.ReadNtohU16 ();
  
  // Read number of coefficients
  uint16_t numCoeffs = start.ReadNtohU16 ();
  
  // Validate
  if (numCoeffs != m_generationSize)
    {
      NS_LOG_ERROR ("Coefficient count mismatch: expected " << m_generationSize << " but got " << numCoeffs);
      return 0;
    }
  
  if (m_generationSize == 0 || m_generationSize > 255) 
    {
      NS_LOG_ERROR ("Invalid generation size: " << m_generationSize);
      return 0;
    }
  
  // Read coefficients
  m_coefficients.clear ();
  m_coefficients.reserve (m_generationSize);
  
  for (uint16_t i = 0; i < m_generationSize; i++)
    {
      if (start.GetRemainingSize () > 0)
        {
          m_coefficients.push_back (start.ReadU8 ());
        }
      else
        {
          NS_LOG_ERROR ("Buffer underrun while reading coefficients");
          return 0;
        }
    }
  
  return GetSerializedSize ();
}

uint32_t 
NetworkCodingHeader::GetSerializedSize (void) const
{
  // 4 bytes (generation ID) + 2 bytes (generation size) + 2 bytes (coeff count) + coefficients
  return 8 + m_generationSize;  // Fixed size calculation
}

void 
NetworkCodingHeader::Print (std::ostream &os) const
{
  os << "Generation ID: " << m_generationId
     << " Generation Size: " << m_generationSize
     << " Coefficients: [";
  
  for (size_t i = 0; i < m_coefficients.size (); i++)
    {
      os << static_cast<uint32_t> (m_coefficients[i]);
      if (i < m_coefficients.size () - 1)
        {
          os << ", ";
        }
    }
  
  os << "]";
}

//----------------------------------------------------------------------------
// NetworkCodingControlHeader Implementation
//----------------------------------------------------------------------------

TypeId 
NetworkCodingControlHeader::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::NetworkCodingControlHeader")
    .SetParent<Header> ()
    .SetGroupName ("NetworkCoding")
    .AddConstructor<NetworkCodingControlHeader> ()
  ;
  return tid;
}

TypeId 
NetworkCodingControlHeader::GetInstanceTypeId (void) const
{
  return GetTypeId ();
}

NetworkCodingControlHeader::NetworkCodingControlHeader ()
  : m_controlType (REQUEST_UNCODED),
    m_generationId (0)
{
}

NetworkCodingControlHeader::NetworkCodingControlHeader (ControlType type, uint32_t genId)
  : m_controlType (type),
    m_generationId (genId)
{
}

NetworkCodingControlHeader::~NetworkCodingControlHeader ()
{
}

void 
NetworkCodingControlHeader::SetControlType (ControlType type)
{
  m_controlType = type;
}

NetworkCodingControlHeader::ControlType 
NetworkCodingControlHeader::GetControlType (void) const
{
  return m_controlType;
}

void 
NetworkCodingControlHeader::SetGenerationId (uint32_t genId)
{
  m_generationId = genId;
}

uint32_t 
NetworkCodingControlHeader::GetGenerationId (void) const
{
  return m_generationId;
}

void 
NetworkCodingControlHeader::SetSequenceNumbers (const std::vector<uint32_t>& seqNums)
{
  m_sequenceNumbers = seqNums;
}

const std::vector<uint32_t>& 
NetworkCodingControlHeader::GetSequenceNumbers (void) const
{
  return m_sequenceNumbers;
}

uint32_t 
NetworkCodingControlHeader::GetSerializedSize (void) const
{
  // Control Type + Generation ID + Number of sequence numbers + Sequence numbers
  return sizeof (uint8_t) + sizeof (uint32_t) + sizeof (uint16_t) + m_sequenceNumbers.size () * sizeof (uint32_t);
}

void 
NetworkCodingControlHeader::Serialize (Buffer::Iterator start) const
{
  // Write control type
  start.WriteU8 (static_cast<uint8_t> (m_controlType));
  
  // Write generation ID
  start.WriteHtonU32 (m_generationId);
  
  // Write number of sequence numbers
  start.WriteHtonU16 (static_cast<uint16_t> (m_sequenceNumbers.size ()));
  
  // Write sequence numbers
  for (uint32_t seqNum : m_sequenceNumbers)
    {
      start.WriteHtonU32 (seqNum);
    }
}

uint32_t 
NetworkCodingControlHeader::Deserialize (Buffer::Iterator start)
{
  // Read control type
  m_controlType = static_cast<ControlType> (start.ReadU8 ());
  
  // Read generation ID
  m_generationId = start.ReadNtohU32 ();
  
  // Read number of sequence numbers
  uint16_t numSeqNums = start.ReadNtohU16 ();
  
  // Read sequence numbers
  m_sequenceNumbers.clear ();
  for (uint16_t i = 0; i < numSeqNums; i++)
    {
      if (start.GetRemainingSize () >= sizeof (uint32_t))
        {
          m_sequenceNumbers.push_back (start.ReadNtohU32 ());
        }
      else
        {
          // Not enough data in buffer
          return 0;
        }
    }
  
  // Return number of bytes consumed
  return GetSerializedSize ();
}

void 
NetworkCodingControlHeader::Print (std::ostream &os) const
{
  std::string typeStr;
  switch (m_controlType) {
    case REQUEST_UNCODED:
      typeStr = "REQUEST_UNCODED";
      break;
    case ACKNOWLEDGE:
      typeStr = "ACKNOWLEDGE";
      break;
    case INNOVATIVE_ACK:
      typeStr = "INNOVATIVE_ACK";
      break;
    default:
      typeStr = "UNKNOWN";
      break;
  }

  os << "Control Type: " << typeStr
     << " Generation ID: " << m_generationId
     << " Sequence Numbers: [";
  
  for (size_t i = 0; i < m_sequenceNumbers.size (); i++)
    {
      os << m_sequenceNumbers[i];
      if (i < m_sequenceNumbers.size () - 1)
        {
          os << ", ";
        }
    }
  
  os << "]";
}

} // namespace ns3