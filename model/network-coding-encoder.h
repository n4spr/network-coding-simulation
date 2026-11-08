#ifndef NETWORK_CODING_ENCODER_H
#define NETWORK_CODING_ENCODER_H

#include "ns3/object.h"
#include "ns3/packet.h"
#include "ns3/random-variable-stream.h"
#include "network-coding-packet.h" 
#include "galois-field.h"
#include <map>
#include <set>

namespace ns3 {

class NetworkCodingEncoder : public Object
{
public:
  static TypeId GetTypeId (void);

  NetworkCodingEncoder ();
  NetworkCodingEncoder (uint16_t generationSize, uint16_t packetSize);
  virtual ~NetworkCodingEncoder ();

  uint16_t GetGenerationSize (void) const;
  void SetGenerationSize (uint16_t generationSize);

  uint16_t GetPacketSize (void) const;
  void SetPacketSize (uint16_t packetSize);

  bool AddPacket (Ptr<const Packet> packet, uint32_t seqNum);
  Ptr<Packet> GenerateCodedPacket (void);
  // Explicit coefficients support reproducible coding schedules and validation.
  Ptr<Packet> GenerateCodedPacket (const std::vector<uint8_t>& coefficients);
  int64_t AssignStreams (int64_t stream);
  Ptr<Packet> GenerateUncodedPacket (uint32_t seqNum);

  bool IsGenerationComplete (void) const;
  uint32_t GetPacketCount (void) const;
  void NextGeneration (void);
  uint32_t GetCurrentGenerationId (void) const;
  std::set<uint32_t> GetSequenceNumbers (void) const;

private:
  uint16_t m_generationSize;
  uint16_t m_packetSize;
  uint32_t m_currentGeneration;
  
  std::map<uint32_t, Ptr<Packet>> m_generationPackets;
  Ptr<GaloisField> m_galoisField;
  Ptr<UniformRandomVariable> m_coefficientsRng;
};

} // namespace ns3

#endif /* NETWORK_CODING_ENCODER_H */