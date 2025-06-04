#include "network-coding-encoder.h"
#include "ns3/log.h"
#include "galois-field.h"
#include <random>

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("NetworkCodingEncoder");
NS_OBJECT_ENSURE_REGISTERED (NetworkCodingEncoder);

TypeId
NetworkCodingEncoder::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::NetworkCodingEncoder")
    .SetParent<Object> ()
    .SetGroupName ("NetworkCoding")
    .AddConstructor<NetworkCodingEncoder> ()
  ;
  return tid;
}

NetworkCodingEncoder::NetworkCodingEncoder ()
  : m_generationSize (8),
    m_packetSize (1024),
    m_currentGeneration (0),
    m_galoisField (CreateObject<GaloisField>())
{
  NS_LOG_FUNCTION (this);
}

NetworkCodingEncoder::NetworkCodingEncoder (uint16_t generationSize, uint16_t packetSize)
  : m_generationSize (generationSize),
    m_packetSize (packetSize),
    m_currentGeneration (0)
{
  NS_LOG_FUNCTION (this << generationSize << packetSize);
  m_galoisField = CreateObject<GaloisField>();
}

NetworkCodingEncoder::~NetworkCodingEncoder ()
{
  NS_LOG_FUNCTION (this);
  // Clean up by removing all references
  m_generationPackets.clear();
}

uint16_t
NetworkCodingEncoder::GetGenerationSize (void) const
{
  return m_generationSize;
}

void
NetworkCodingEncoder::SetGenerationSize (uint16_t generationSize)
{
  NS_LOG_FUNCTION (this << generationSize);
  m_generationSize = generationSize;
}

uint16_t
NetworkCodingEncoder::GetPacketSize (void) const
{
  return m_packetSize;
}

void
NetworkCodingEncoder::SetPacketSize (uint16_t packetSize)
{
  NS_LOG_FUNCTION (this << packetSize);
  m_packetSize = packetSize;
}

bool
NetworkCodingEncoder::AddPacket (Ptr<const Packet> packet, uint32_t seqNum)
{
  NS_LOG_FUNCTION (this << packet << seqNum);
  
  // Basic validation
  if (!packet)
    {
      NS_LOG_ERROR ("Cannot add null packet");
      return false;
    }
  
  if (m_generationPackets.size() >= m_generationSize)
    {
      NS_LOG_WARN ("Cannot add packet: generation is full");
      return false;
    }
  
  if (m_generationPackets.find(seqNum) != m_generationPackets.end())
    {
      NS_LOG_WARN ("Packet with sequence number " << seqNum << " already exists");
      return false;
    }
  
  // Create a new packet with exact size (simplifies memory management)
  Ptr<Packet> newPacket;
  
  if (packet->GetSize() == m_packetSize)
    {
      // Same size, just copy
      newPacket = packet->Copy();
    }
  else
    {
      // Create a buffer for data
      uint8_t *buffer = new uint8_t[m_packetSize];
      memset(buffer, 0, m_packetSize); // Zero-fill
      
      // Copy available data (handles truncation or padding)
      uint32_t copySize = std::min(packet->GetSize(), (uint32_t)m_packetSize);
      packet->CopyData(buffer, copySize);
      
      // Create packet from buffer
      newPacket = Create<Packet>(buffer, m_packetSize);
      
      // IMPORTANT: Delete buffer after creating packet
      delete[] buffer;
    }
  
  // Store the packet
  m_generationPackets[seqNum] = newPacket;
  
  NS_LOG_INFO ("Added packet with sequence number " << seqNum << " to generation " 
               << m_currentGeneration);
  
  return true;
}

// FIXED: Use proper Galois field arithmetic in GenerateCodedPacket
Ptr<Packet>
NetworkCodingEncoder::GenerateCodedPacket (void)
{
  NS_LOG_FUNCTION (this);
  
  // Validation
  if (m_generationPackets.empty())
    {
      NS_LOG_WARN ("Cannot generate coded packet: no packets in generation");
      return nullptr;
    }
  
  // Generate random coefficients
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<uint8_t> dis(1, 255); // Avoid zero coefficients
  
  std::vector<uint8_t> coefficients(m_generationSize, 0);
  
  // Set coefficients for packets we actually have
  size_t packetIndex = 0;
  for (auto& pair : m_generationPackets)
    {
      if (packetIndex < m_generationSize)
        {
          coefficients[packetIndex] = dis(gen);
          packetIndex++;
        }
    }
  
  // FIXED: Create coded payload using PROPER Galois field arithmetic
  std::vector<uint8_t> codedPayload(m_packetSize, 0);
  
  packetIndex = 0;
  for (auto& pair : m_generationPackets)
    {
      if (packetIndex < m_generationSize && coefficients[packetIndex] != 0)
        {
          // Extract packet data
          std::vector<uint8_t> packetData(m_packetSize);
          pair.second->CopyData(packetData.data(), m_packetSize);
          
          // FIXED: Use proper Galois field operations
          uint8_t coeff = coefficients[packetIndex];
          for (size_t i = 0; i < m_packetSize; i++)
            {
              // Use Galois field multiplication and addition
              uint8_t product = m_galoisField->Multiply(coeff, packetData[i]);
              codedPayload[i] = m_galoisField->Add(codedPayload[i], product);
            }
        }
      packetIndex++;
    }
  
  // Create the coded packet
  Ptr<Packet> codedPacket = Create<Packet>(codedPayload.data(), m_packetSize);
  
  // Create and add header
  NetworkCodingHeader header;
  header.SetGenerationId(m_currentGeneration);
  header.SetGenerationSize(m_generationSize);
  header.SetCoefficients(coefficients);
  
  codedPacket->AddHeader(header);
  
  NS_LOG_INFO ("Generated coded packet for generation " << m_currentGeneration 
               << " with " << m_generationPackets.size() << " source packets");
  
  return codedPacket;
}

bool
NetworkCodingEncoder::IsGenerationComplete (void) const
{
  return m_generationPackets.size() >= m_generationSize;
}

uint32_t
NetworkCodingEncoder::GetPacketCount (void) const
{
  return m_generationPackets.size();
}

void
NetworkCodingEncoder::NextGeneration (void)
{
  NS_LOG_FUNCTION (this);
  
  m_currentGeneration++;
  m_generationPackets.clear();
  
  NS_LOG_INFO ("Moving to generation " << m_currentGeneration);
}

uint32_t
NetworkCodingEncoder::GetCurrentGenerationId (void) const
{
  return m_currentGeneration;
}

std::set<uint32_t>
NetworkCodingEncoder::GetSequenceNumbers (void) const
{
  std::set<uint32_t> seqNums;
  
  for (const auto& pair : m_generationPackets)
    {
      seqNums.insert(pair.first);
    }
  
  return seqNums;
}

Ptr<Packet>
NetworkCodingEncoder::GenerateUncodedPacket (uint32_t seqNum)
{
  NS_LOG_FUNCTION (this << seqNum);
  
  auto it = m_generationPackets.find(seqNum);
  if (it == m_generationPackets.end())
    {
      NS_LOG_WARN ("Cannot generate uncoded packet: sequence number " << seqNum << " not found");
      return nullptr;
    }
  
  // Create a copy of the packet
  Ptr<Packet> packet = it->second->Copy();
  
  // Create header
  NetworkCodingHeader header;
  header.SetGenerationId(m_currentGeneration);
  header.SetGenerationSize(m_generationSize);
  
  // Set up identity coefficients
  std::vector<uint8_t> coefficients(m_generationSize, 0);
  
  // Find position of this packet in the generation
  size_t position = 0;
  for (auto it2 = m_generationPackets.begin(); it2 != m_generationPackets.end(); ++it2)
    {
      if (it2->first == seqNum)
        {
          break;
        }
      position++;
    }
  
  // Set coefficient for this packet to 1
  if (position < m_generationSize)
    {
      coefficients[position] = 1;
    }
  
  header.SetCoefficients(coefficients);
  packet->AddHeader(header);
  
  NS_LOG_INFO ("Generated uncoded packet with sequence number " << seqNum);
  
  return packet;
}

} // namespace ns3