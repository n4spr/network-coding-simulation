#ifndef NETWORK_CODING_ENCODER_H
#define NETWORK_CODING_ENCODER_H

#include "ns3/object.h"
#include "ns3/packet.h"
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
};

} // namespace ns3

#endif /* NETWORK_CODING_ENCODER_H */