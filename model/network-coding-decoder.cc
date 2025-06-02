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

#include "network-coding-decoder.h"
#include "ns3/log.h"
#include "ns3/simulator.h"
#include "ns3/assert.h"

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("NetworkCodingDecoder");
NS_OBJECT_ENSURE_REGISTERED (NetworkCodingDecoder);

TypeId
NetworkCodingDecoder::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::NetworkCodingDecoder")
    .SetParent<Object> ()
    .SetGroupName ("NetworkCoding")
    .AddConstructor<NetworkCodingDecoder> ()
  ;
  return tid;
}

NetworkCodingDecoder::NetworkCodingDecoder ()
  : m_generationSize (8),
    m_packetSize (1024),
    m_currentGeneration (0),
    m_decoded (false)
{
  NS_LOG_FUNCTION (this);
  m_gf = CreateObject<GaloisField> ();
  NS_ASSERT_MSG (m_generationSize > 0 && m_packetSize > 0, "Invalid generation or packet size");
  // Initialize the coefficient matrix and coded payload storage
  m_coefficients.resize (m_generationSize, std::vector<uint8_t> (m_generationSize, 0));
  m_codedPayloads.resize (m_generationSize, std::vector<uint8_t> (m_packetSize, 0));
  NS_LOG_INFO ("Decoder created");
}

NetworkCodingDecoder::NetworkCodingDecoder (uint16_t generationSize, uint16_t packetSize)
  : m_generationSize (generationSize),
    m_packetSize (packetSize),
    m_currentGeneration (0),
    m_decoded (false)
{
  NS_LOG_FUNCTION (this << generationSize << packetSize);
  
  // Create GaloisField first
  m_gf = CreateObject<GaloisField> ();
  NS_ASSERT_MSG(m_gf, "Failed to create GaloisField");
  
  // Validate parameters
  NS_ASSERT_MSG (m_generationSize > 0 && m_generationSize <= 255, "Invalid generation size");
  NS_ASSERT_MSG (m_packetSize > 0, "Invalid packet size");
  
  // Initialize the coefficient matrix and coded payload storage
  try {
    m_coefficients.resize (m_generationSize, std::vector<uint8_t> (m_generationSize, 0));
    m_codedPayloads.resize (m_generationSize, std::vector<uint8_t> (m_packetSize, 0));
  } catch (const std::exception& e) {
    NS_FATAL_ERROR("Failed to allocate decoder matrices: " << e.what());
  }
  
  NS_LOG_INFO ("Decoder created with generation size " << m_generationSize 
               << " and packet size " << m_packetSize);
}

NetworkCodingDecoder::~NetworkCodingDecoder ()
{
  NS_LOG_FUNCTION (this);
}

void
NetworkCodingDecoder::SetGenerationSize (uint16_t generationSize)
{
  NS_LOG_FUNCTION (this << generationSize);
  
  if (generationSize != m_generationSize)
    {
      m_generationSize = generationSize;
      
      // Resize the coefficient matrix and coded payload storage
      m_coefficients.resize (m_generationSize, std::vector<uint8_t> (m_generationSize, 0));
      m_codedPayloads.resize (m_generationSize, std::vector<uint8_t> (m_packetSize, 0));
      
      // Reset decoder state
      m_decoded = false;
      m_decodedPackets.clear ();
      m_receivedSequences.clear ();
    }
}

uint16_t
NetworkCodingDecoder::GetGenerationSize (void) const
{
  return m_generationSize;
}

void
NetworkCodingDecoder::SetPacketSize (uint16_t packetSize)
{
  NS_LOG_FUNCTION (this << packetSize);
  
  if (packetSize != m_packetSize)
    {
      m_packetSize = packetSize;
      
      // Resize the coded payload storage
      for (auto& payload : m_codedPayloads)
        {
          payload.resize (m_packetSize, 0);
        }
      
      // Reset decoder state
      m_decoded = false;
      m_decodedPackets.clear ();
    }
}

uint16_t
NetworkCodingDecoder::GetPacketSize (void) const
{
  return m_packetSize;
}

bool
NetworkCodingDecoder::ProcessCodedPacket (Ptr<const Packet> packet)
{
  NS_LOG_FUNCTION (this << packet);
  
  if (!packet) {
    NS_LOG_ERROR("Null packet received");
    return false;
  }
  
  // Already decoded, no need to process more packets
  if (m_decoded)
    {
      NS_LOG_INFO ("Generation already decoded, ignoring packet");
      return false;
    }
  
  // Make a copy of the packet
  Ptr<Packet> packetCopy = packet->Copy ();
  
  // Extract the network coding header
  NetworkCodingHeader header;
  try {
    uint32_t headerSize = packetCopy->RemoveHeader (header);
    if (headerSize == 0) {
      NS_LOG_ERROR("Failed to remove network coding header");
      return false;
    }
  } catch (...) {
    NS_LOG_ERROR("Exception while removing header");
    return false;
  }
  
  // Check if the packet belongs to the current generation
  if (header.GetGenerationId () != m_currentGeneration)
    {
      NS_LOG_WARN ("Packet belongs to generation " << header.GetGenerationId () 
                   << " but current generation is " << m_currentGeneration);
      return false;
    }
  
  // Get the coding coefficients
  const std::vector<uint8_t>& coefficients = header.GetCoefficients ();
  if (coefficients.empty()) {
    NS_LOG_ERROR("Empty coefficients vector");
    return false;
  }
  
  // Pad coefficients to generation size if needed
  std::vector<uint8_t> paddedCoeffs(m_generationSize, 0);
  for (size_t i = 0; i < std::min(coefficients.size(), (size_t)m_generationSize); i++) {
    paddedCoeffs[i] = coefficients[i];
  }
  
  // Check packet payload size
  if (packetCopy->GetSize () != m_packetSize)
    {
      NS_LOG_WARN ("Packet size mismatch: expected " << m_packetSize 
                    << " but got " << packetCopy->GetSize ());
      // Handle size mismatch gracefully
    }
  
  // Extract the payload
  std::vector<uint8_t> payload(m_packetSize, 0);
  uint32_t actualSize = std::min(packetCopy->GetSize(), (uint32_t)m_packetSize);
  if (actualSize > 0) {
    packetCopy->CopyData(payload.data(), actualSize);
  }
  
  // Find an empty row to store this packet
  bool stored = false;
  for (size_t i = 0; i < m_generationSize; i++)
    {
      // Check if the row is empty (all zeros)
      bool rowEmpty = true;
      for (size_t j = 0; j < m_generationSize; j++)
        {
          if (m_coefficients[i][j] != 0)
            {
              rowEmpty = false;
              break;
            }
        }
      
      if (rowEmpty)
        {
          // Store the coefficients and payload
          m_coefficients[i] = paddedCoeffs;
          m_codedPayloads[i] = payload;
          stored = true;
          
          NS_LOG_INFO ("Stored coded packet in row " << i);
          break;
        }
    }
  
  if (!stored) {
    NS_LOG_WARN("No empty row available for packet storage");
    return false;
  }
  
  // Check if we can decode now
  if (CanDecode ())
    {
      NS_LOG_INFO ("Matrix has full rank, decoding generation " << m_currentGeneration);
      DecodeGeneration ();
    }
  
  return true;
}

bool
NetworkCodingDecoder::CanDecode (void) const
{
  // Can decode if the coefficient matrix has full rank
  return GetRank () == m_generationSize;
}

uint16_t
NetworkCodingDecoder::GetRank (void) const
{
  return CalculateRank ();
}

uint16_t
NetworkCodingDecoder::CalculateRank (void) const
{
  // Copy the coefficient matrix to avoid modifying it
  std::vector<std::vector<uint8_t>> matrix = m_coefficients;
  
  // Use Gaussian elimination to calculate the rank
  uint16_t rank = 0;
  
  // For each column (potential pivot)
  for (size_t j = 0; j < m_generationSize; j++)
    {
      // Find a row with a non-zero entry in this column
      size_t pivotRow = rank;
      while (pivotRow < m_generationSize && matrix[pivotRow][j] == 0)
        {
          pivotRow++;
        }
      
      if (pivotRow < m_generationSize)
        {
          // Found a pivot, swap rows if necessary
          if (pivotRow != rank)
            {
              std::swap (matrix[rank], matrix[pivotRow]);
            }
          
          // Zero out entries below the pivot
          for (size_t i = rank + 1; i < m_generationSize; i++)
            {
              if (matrix[i][j] != 0)
                {
                  // Calculate the factor to zero out this entry
                  uint8_t factor = m_gf->Divide (matrix[i][j], matrix[rank][j]);
                  
                  // Zero out the entry and all others in this row
                  for (size_t k = j; k < m_generationSize; k++)
                    {
                      uint8_t product = m_gf->Multiply (factor, matrix[rank][k]);
                      matrix[i][k] = m_gf->Subtract (matrix[i][k], product);
                    }
                }
            }
          
          rank++;
        }
    }
  
  return rank;
}

void
NetworkCodingDecoder::DecodeGeneration (void)
{
  NS_LOG_FUNCTION (this);
  
  if (m_decoded) {
    NS_LOG_INFO("Generation already decoded");
    return;
  }
  
  if (!CanDecode()) {
    NS_LOG_WARN("Cannot decode: insufficient rank");
    return;
  }
  
  // Make copies of the coefficient matrix and coded payloads for Gaussian elimination
  std::vector<std::vector<uint8_t>> coeffMatrix = m_coefficients;
  std::vector<std::vector<uint8_t>> payloadMatrix = m_codedPayloads;
  
  try {
    // Step 1: Forward elimination to get reduced row echelon form
    for (size_t i = 0; i < m_generationSize; i++)
      {
        // Find pivot
        size_t pivotRow = i;
        for (size_t j = i + 1; j < m_generationSize; j++) {
          if (coeffMatrix[j][i] != 0) {
            pivotRow = j;
            break;
          }
        }
        
        // Check if pivot is non-zero
        if (coeffMatrix[pivotRow][i] == 0) {
          NS_LOG_ERROR("Matrix is singular at row " << i);
          return;
        }
        
        // Swap rows if needed
        if (pivotRow != i) {
          std::swap(coeffMatrix[i], coeffMatrix[pivotRow]);
          std::swap(payloadMatrix[i], payloadMatrix[pivotRow]);
        }
        
        // Normalize pivot row
        uint8_t pivot = coeffMatrix[i][i];
        if (pivot == 0) {
          NS_LOG_ERROR("Zero pivot encountered");
          return;
        }
        
        uint8_t pivotInv = m_gf->Inverse(pivot);
        
        // Normalize coefficient row
        for (size_t j = 0; j < m_generationSize; j++) {
          coeffMatrix[i][j] = m_gf->Multiply(coeffMatrix[i][j], pivotInv);
        }
        
        // Normalize payload row
        for (size_t j = 0; j < m_packetSize; j++) {
          payloadMatrix[i][j] = m_gf->Multiply(payloadMatrix[i][j], pivotInv);
        }
        
        // Eliminate column in other rows
        for (size_t j = 0; j < m_generationSize; j++) {
          if (j != i && coeffMatrix[j][i] != 0) {
            uint8_t factor = coeffMatrix[j][i];
            
            // Eliminate coefficients
            for (size_t k = 0; k < m_generationSize; k++) {
              uint8_t product = m_gf->Multiply(factor, coeffMatrix[i][k]);
              coeffMatrix[j][k] = m_gf->Subtract(coeffMatrix[j][k], product);
            }
            
            // Eliminate payload
            for (size_t k = 0; k < m_packetSize; k++) {
              uint8_t product = m_gf->Multiply(factor, payloadMatrix[i][k]);
              payloadMatrix[j][k] = m_gf->Subtract(payloadMatrix[j][k], product);
            }
          }
        }
      }
    
    // Create packets from the decoded data
    m_decodedPackets.clear();
    m_decodedPackets.reserve(m_generationSize);
    
    for (size_t i = 0; i < m_generationSize; i++) {
      Ptr<Packet> packet = Create<Packet>(payloadMatrix[i].data(), m_packetSize);
      m_decodedPackets.push_back(packet);
    }
    
    m_decoded = true;
    NS_LOG_INFO ("Successfully decoded generation " << m_currentGeneration);
    
  } catch (const std::exception& e) {
    NS_LOG_ERROR("Exception during decoding: " << e.what());
    m_decoded = false;
  }
}

std::vector<Ptr<Packet>>
NetworkCodingDecoder::GetDecodedPackets (void)
{
  NS_LOG_FUNCTION (this);
  
  if (!m_decoded && CanDecode ())
    {
      DecodeGeneration ();
    }
  
  return m_decodedPackets;
}

std::set<uint32_t>
NetworkCodingDecoder::GetMissingPackets (void) const
{
  std::set<uint32_t> missingPackets;
  
  // In a real implementation, we would track which packets we've received
  // For this example, we estimate based on the rank of the matrix
  // uint16_t rank = GetRank ();
  
  for (uint32_t i = 0; i < m_generationSize; i++)
    {
      // Consider each packet in the current generation
      uint32_t seqNum = m_currentGeneration * m_generationSize + i;
      
      if (!m_receivedSequences.count (seqNum))
        {
          missingPackets.insert (seqNum);
        }
    }
  
  return missingPackets;
}

void
NetworkCodingDecoder::NextGeneration (void)
{
  NS_LOG_FUNCTION (this);
  
  m_currentGeneration++;
  
  // Reset the coefficient matrix and coded payloads
  for (auto& row : m_coefficients)
    {
      std::fill (row.begin (), row.end (), 0);
    }
  
  for (auto& payload : m_codedPayloads)
    {
      std::fill (payload.begin (), payload.end (), 0);
    }
  
  // Reset decoder state
  m_decoded = false;
  m_decodedPackets.clear ();
  m_receivedSequences.clear ();
  
  NS_LOG_INFO ("Moving to generation " << m_currentGeneration);
}

uint32_t
NetworkCodingDecoder::GetCurrentGenerationId (void) const
{
  return m_currentGeneration;
}

} // namespace ns3