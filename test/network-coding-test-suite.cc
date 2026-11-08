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

#include "network-coding-test-suite.h"
#include "ns3/log.h"
#include "ns3/packet.h"
#include "ns3/network-coding-packet.h"
#include "ns3/simulator.h"
#include "ns3/rng-seed-manager.h"

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("NetworkCodingTestSuite");

namespace {
// Independent polynomial arithmetic, without the implementation's log tables.
uint8_t ReferenceMultiply(uint16_t a, uint16_t b)
{
  uint16_t result = 0;
  for (unsigned bit = 0; bit < 8; ++bit)
    {
      if (b & 1) result ^= a;
      b >>= 1;
      a <<= 1;
      if (a & 0x100) a ^= 0x11d;
    }
  return static_cast<uint8_t>(result);
}
std::vector<uint8_t> Coefficients(uint16_t count, uint8_t point)
{
  std::vector<uint8_t> result(count, 1);
  for (size_t i = 1; i < count; ++i) result[i] = ReferenceMultiply(result[i-1], point);
  return result;
}
std::vector<uint8_t> Original(uint16_t index, uint32_t size)
{
  std::vector<uint8_t> result(size);
  for (uint32_t j = 0; j < size; ++j) result[j] = (17 + index * 31 + j * 7 + index * j) % 256;
  return result;
}
}

// Register the test suite
static NetworkCodingTestSuite g_networkCodingTestSuite;

//-----------------------------------------------------------------------------
// GaloisFieldTestCase implementation
//-----------------------------------------------------------------------------

GaloisFieldTestCase::GaloisFieldTestCase ()
  : TestCase ("GaloisField test case")
{
}

GaloisFieldTestCase::~GaloisFieldTestCase ()
{
}

void
GaloisFieldTestCase::DoRun (void)
{
  Ptr<GaloisField> gf = CreateObject<GaloisField> ();
  
  TestAddition (gf);
  TestMultiplication (gf);
  for (unsigned a = 0; a < 256; ++a)
    {
      for (unsigned b = 0; b < 256; ++b)
        {
          NS_TEST_ASSERT_MSG_EQ(unsigned(gf->Multiply(a,b)), unsigned(ReferenceMultiply(a,b)), "polynomial product");
        }
      if (a != 0) { NS_TEST_ASSERT_MSG_EQ(unsigned(ReferenceMultiply(a,gf->Inverse(a))), 1, "polynomial inverse"); }
    }
  TestDivision (gf);
  TestInverse (gf);
}

void
GaloisFieldTestCase::TestAddition (Ptr<GaloisField> gf)
{
  // Test addition (XOR)
  NS_TEST_ASSERT_MSG_EQ (gf->Add (0x53, 0xCA), 0x99, "GF(2^8) addition failed");
  NS_TEST_ASSERT_MSG_EQ (gf->Add (0, 0x01), 0x01, "GF(2^8) addition with zero failed");
  NS_TEST_ASSERT_MSG_EQ (gf->Add (0x01, 0x01), 0x00, "GF(2^8) addition of same value failed");
  NS_TEST_ASSERT_MSG_EQ (gf->Add (0xFF, 0xFF), 0x00, "GF(2^8) addition of 0xFF failed");
  
  // Test that addition is commutative
  for (uint16_t i = 0; i < 10; i++)
    {
      uint8_t a = rand () % 256;
      uint8_t b = rand () % 256;
      NS_TEST_ASSERT_MSG_EQ (gf->Add (a, b), gf->Add (b, a), "GF(2^8) addition is not commutative");
    }
  
  // Test that addition is the same as subtraction
  for (uint16_t i = 0; i < 10; i++)
    {
      uint8_t a = rand () % 256;
      uint8_t b = rand () % 256;
      NS_TEST_ASSERT_MSG_EQ (gf->Add (a, b), gf->Subtract (a, b), "GF(2^8) addition is not the same as subtraction");
    }
}

void
GaloisFieldTestCase::TestMultiplication (Ptr<GaloisField> gf)
{
  // This product uses the implementation's 0x11d polynomial.
  NS_TEST_ASSERT_MSG_EQ (gf->Multiply (0x53, 0xCA), 0x8f, "GF(2^8) multiplication failed");
  NS_TEST_ASSERT_MSG_EQ (gf->Multiply (0, 0x01), 0, "GF(2^8) multiplication with zero failed");
  NS_TEST_ASSERT_MSG_EQ (gf->Multiply (0x01, 0x01), 0x01, "GF(2^8) multiplication by 1 failed");
  
  // Test that multiplication is commutative
  for (uint16_t i = 0; i < 10; i++)
    {
      uint8_t a = rand () % 256;
      uint8_t b = rand () % 256;
      NS_TEST_ASSERT_MSG_EQ (gf->Multiply (a, b), gf->Multiply (b, a), "GF(2^8) multiplication is not commutative");
    }
  
  // Test distributive property: a*(b+c) = a*b + a*c
  for (uint16_t i = 0; i < 10; i++)
    {
      uint8_t a = rand () % 256;
      uint8_t b = rand () % 256;
      uint8_t c = rand () % 256;
      
      uint8_t result1 = gf->Multiply (a, gf->Add (b, c));
      uint8_t result2 = gf->Add (gf->Multiply (a, b), gf->Multiply (a, c));
      
      NS_TEST_ASSERT_MSG_EQ (result1, result2, "GF(2^8) distributive property failed");
    }
}

void
GaloisFieldTestCase::TestDivision (Ptr<GaloisField> gf)
{
  // Test division
  NS_TEST_ASSERT_MSG_EQ (gf->Divide (0x01, 0x01), 0x01, "GF(2^8) division by 1 failed");
  NS_TEST_ASSERT_MSG_EQ (gf->Divide (0, 0x01), 0, "GF(2^8) division of 0 failed");
  
  // Test that a/b * b = a
  for (uint16_t i = 0; i < 10; i++)
    {
      uint8_t a = rand () % 256;
      uint8_t b = rand () % 256;
      
      if (b == 0)
        {
          b = 1;  // Avoid division by zero
        }
      
      uint8_t result = gf->Multiply (gf->Divide (a, b), b);
      NS_TEST_ASSERT_MSG_EQ (result, a, "GF(2^8) division and multiplication are not inverse operations");
    }
}

void
GaloisFieldTestCase::TestInverse (Ptr<GaloisField> gf)
{
  // Test inverse
  NS_TEST_ASSERT_MSG_EQ (gf->Inverse (0x01), 0x01, "GF(2^8) inverse of 1 failed");
  
  // Test that a * aÃ¢ÂÂ»ÃÂ¹ = 1
  for (uint16_t i = 0; i < 10; i++)
    {
      uint8_t a = rand () % 255 + 1;  // Avoid 0
      
      uint8_t result = gf->Multiply (a, gf->Inverse (a));
      NS_TEST_ASSERT_MSG_EQ (result, 0x01, "GF(2^8) inverse and multiplication failed");
    }
  
  // Test that aÃ¢ÂÂ»ÃÂ¹ = 1/a
  for (uint16_t i = 0; i < 10; i++)
    {
      uint8_t a = rand () % 255 + 1;  // Avoid 0
      
      NS_TEST_ASSERT_MSG_EQ (gf->Inverse (a), gf->Divide (0x01, a), "GF(2^8) inverse is not equal to 1/a");
    }
}

//-----------------------------------------------------------------------------
// NetworkCodingTestCase implementation
//-----------------------------------------------------------------------------

NetworkCodingTestCase::NetworkCodingTestCase ()
  : TestCase ("NetworkCoding test case")
{
}

NetworkCodingTestCase::~NetworkCodingTestCase ()
{
}

void
NetworkCodingTestCase::DoRun (void)
{
  // Equal explicit ns-3 stream assignments must replay coefficient sequences.
  auto first = CreateObject<NetworkCodingEncoder>(2, 64);
  auto second = CreateObject<NetworkCodingEncoder>(2, 64);
  first->AssignStreams(731); second->AssignStreams(731);
  for (unsigned i = 0; i < 2; ++i)
    {
      auto payload = Original(i,64);
      auto packet = Create<Packet>(payload.data(),payload.size());
      first->AddPacket(packet,i); second->AddPacket(packet,i);
    }
  for (unsigned i = 0; i < 8; ++i)
    {
      auto a = first->GenerateCodedPacket(), b = second->GenerateCodedPacket();
      std::vector<uint8_t> left(a->GetSize()), right(b->GetSize());
      a->CopyData(left.data(),left.size()); b->CopyData(right.data(),right.size());
      NS_TEST_ASSERT_MSG_EQ((left == right), true, "assigned stream replay");
    }

  // Test with different packet sizes and generation sizes
  TestCoding (64, 4);
  TestCoding (1024, 8);
  TestCoding (1500, 16);
  
  TestDeliverySchedules (64, 2);
  TestDeliverySchedules (1024, 8);
  TestDeliverySchedules (1500, 16);
}

void
NetworkCodingTestCase::TestCoding (uint32_t packetSize, uint16_t generationSize)
{
  // Create encoder and decoder
  Ptr<NetworkCodingEncoder> encoder = CreateObject<NetworkCodingEncoder> (generationSize, packetSize);
  Ptr<NetworkCodingDecoder> decoder = CreateObject<NetworkCodingDecoder> (generationSize, packetSize);
  
  // Create original packets
  std::vector<Ptr<Packet>> originalPackets;
  
  for (uint16_t i = 0; i < generationSize; i++)
    {
      uint8_t* buffer = new uint8_t[packetSize];
      
      const auto expected = Original(i, packetSize);
      std::copy(expected.begin(), expected.end(), buffer);
      
      Ptr<Packet> packet = Create<Packet> (buffer, packetSize);
      originalPackets.push_back (packet);
      
      // Add packet to encoder
      bool added = encoder->AddPacket (packet, i);
      NS_TEST_ASSERT_MSG_EQ (added, true, "Failed to add packet to encoder");
      
      delete[] buffer;
    }
  
  // Verify that generation is complete
  NS_TEST_ASSERT_MSG_EQ (encoder->IsGenerationComplete (), true, "Generation should be complete");
  NS_TEST_ASSERT_MSG_EQ (encoder->GetPacketCount (), generationSize, "Packet count doesn't match generation size");
  
  // Generate and process coded packets
  for (uint16_t i = 0; i < generationSize; i++)
    {
      // Generate a coded packet
      Ptr<Packet> codedPacket = encoder->GenerateCodedPacket (Coefficients(generationSize, i + 1));
      NS_TEST_ASSERT_MSG_NE (codedPacket, nullptr, "Failed to generate coded packet");
      
      // Process the coded packet
      bool innovative = decoder->ProcessCodedPacket (codedPacket);
      NS_TEST_ASSERT_MSG_EQ (innovative, true, "Coded packet should be innovative");
    }
  
  // Verify that decoding is possible
  NS_TEST_ASSERT_MSG_EQ (decoder->CanDecode (), true, "Should be able to decode after receiving all packets");
  NS_TEST_ASSERT_MSG_EQ (decoder->GetRank (), generationSize, "Decoder rank should equal generation size");
  
  // Get decoded packets
  std::vector<Ptr<Packet>> decodedPackets = decoder->GetDecodedPackets ();
  NS_TEST_ASSERT_MSG_EQ (decodedPackets.size (), generationSize, "Number of decoded packets doesn't match generation size");
  
  if (decodedPackets.size() != generationSize) return;

  // Verify that decoded packets match original packets
  for (uint16_t i = 0; i < generationSize; i++)
    {
      uint8_t* originalBuffer = new uint8_t[packetSize];
      uint8_t* decodedBuffer = new uint8_t[packetSize];
      
      originalPackets[i]->CopyData (originalBuffer, packetSize);
      decodedPackets[i]->CopyData (decodedBuffer, packetSize);
      
      bool match = true;
      for (uint32_t j = 0; j < packetSize; j++)
        {
          if (originalBuffer[j] != decodedBuffer[j])
            {
              match = false;
              break;
            }
        }
      
      delete[] originalBuffer;
      delete[] decodedBuffer;
      
      NS_TEST_ASSERT_MSG_EQ (match, true, "Decoded packet doesn't match original");
    }
}

void
NetworkCodingTestCase::TestDeliverySchedules(uint32_t packetSize, uint16_t generationSize)
{
  auto encoder = CreateObject<NetworkCodingEncoder>(generationSize, packetSize);
  auto decoder = CreateObject<NetworkCodingDecoder>(generationSize, packetSize);
  for (uint16_t i = 0; i < generationSize; ++i)
    {
      auto data = Original(i, packetSize);
      encoder->AddPacket(Create<Packet>(data.data(), data.size()), i);
    }
  // Malformed equations must neither be padded/truncated nor increase rank.
  for (bool wrongGenerationSize : {false,true})
    {
      auto malformed = Create<Packet>(packetSize - 1);
      NetworkCodingHeader header;
      header.SetGenerationSize(wrongGenerationSize ? generationSize + 1 : generationSize);
      header.SetCoefficients(std::vector<uint8_t>(header.GetGenerationSize(),1));
      if (wrongGenerationSize) malformed = Create<Packet>(packetSize);
      malformed->AddHeader(header);
      NS_TEST_ASSERT_MSG_EQ(decoder->ProcessCodedPacket(malformed), false, "reject malformed equation");
      NS_TEST_ASSERT_MSG_EQ(decoder->GetRank(),0,"malformed input leaves rank unchanged");
    }
  // Erase the first two transmissions, then deliver a full-rank Vandermonde
  // schedule in reverse order. Duplicate deliveries must not increase rank.
  encoder->GenerateCodedPacket(Coefficients(generationSize, 1));
  encoder->GenerateCodedPacket(Coefficients(generationSize, 2));
  for (uint16_t row = 0; row < generationSize; ++row)
    {
      auto coeffs = Coefficients(generationSize, generationSize + 2 - row);
      auto packet = encoder->GenerateCodedPacket(coeffs);
      NS_TEST_ASSERT_MSG_NE(packet, nullptr, "explicit encoder output");
      auto payloadPacket = packet->Copy();
      NetworkCodingHeader header;
      payloadPacket->RemoveHeader(header);
      std::vector<uint8_t> actual(packetSize), expected(packetSize, 0);
      payloadPacket->CopyData(actual.data(), actual.size());
      for (uint16_t i = 0; i < generationSize; ++i)
        {
          auto original = Original(i, packetSize);
          for (uint32_t j = 0; j < packetSize; ++j) expected[j] ^= ReferenceMultiply(coeffs[i], original[j]);
        }
      NS_TEST_ASSERT_MSG_EQ((actual == expected), true, "coded payload against polynomial oracle");
      NS_TEST_ASSERT_MSG_EQ(decoder->ProcessCodedPacket(packet), true, "independent equation");
      NS_TEST_ASSERT_MSG_EQ(decoder->ProcessCodedPacket(packet), false, "duplicate equation");
      NS_TEST_ASSERT_MSG_EQ(decoder->GetRank(), row + 1, "rank after duplicate");
      if (row + 1 < generationSize)
        {
          NS_TEST_ASSERT_MSG_EQ(decoder->CanDecode(), false, "insufficient rank");
          NS_TEST_ASSERT_MSG_EQ(decoder->GetDecodedPackets().empty(), true, "no false decoding");
        }
    }
  auto decoded = decoder->GetDecodedPackets();
  NS_TEST_ASSERT_MSG_EQ(decoded.size(), generationSize, "recovery after prescribed erasures");
  if (decoded.size() != generationSize) return;
  for (uint16_t i = 0; i < generationSize; ++i)
    {
      std::vector<uint8_t> actual(packetSize);
      decoded[i]->CopyData(actual.data(), actual.size());
      NS_TEST_ASSERT_MSG_EQ((actual == Original(i,packetSize)), true, "recovered original bytes");
    }
  decoder->NextGeneration();
  NS_TEST_ASSERT_MSG_EQ(decoder->GetRank(), 0, "new generation starts empty");
  auto stale = encoder->GenerateCodedPacket(Coefficients(generationSize, 1));
  NS_TEST_ASSERT_MSG_EQ(decoder->ProcessCodedPacket(stale), false, "reject previous generation");
  NS_TEST_ASSERT_MSG_EQ(decoder->GetRank(), 0, "stale data leaves rank unchanged");
}

//-----------------------------------------------------------------------------
// NetworkCodingTestSuite implementation
//-----------------------------------------------------------------------------

class PacketMetadataTest : public TestCase
{
public:
  PacketMetadataTest() : TestCase("packet metadata and payload boundary") {}
  void DoRun() override
  {
    const uint8_t payload[] = {0x4e, 0x43, 0x4d, 0x44, 1, 2, 3, 4, 5, 6};
    for (int mode = 0; mode < 4; ++mode)
      {
        Ptr<Packet> packet = Create<Packet>(payload, sizeof(payload));
        NetworkCodingHeader sent;
        sent.SetGenerationSize(2); sent.SetCoefficients({1, 0});
        if (mode & 1) sent.SetFinalDestination(Ipv4Address("10.1.2.3"));
        if (mode & 2) { ButterflyHeaderInfo info; info.r3 = 37; sent.SetButterflyInfo(info); sent.SetButterflyHopRole(BUTTERFLY_HOP_R3_TO_R4); }
        packet->AddHeader(sent);
        NetworkCodingHeader received;
        uint32_t removed = packet->RemoveHeader(received);
        NS_TEST_ASSERT_MSG_EQ(removed, sent.GetSerializedSize(), "header size");
        NS_TEST_ASSERT_MSG_EQ(received.HasFinalDestination(), bool(mode & 1), "destination flag");
        NS_TEST_ASSERT_MSG_EQ(received.HasButterflyInfo(), bool(mode & 2), "butterfly flag");
        if (mode & 2) { NS_TEST_ASSERT_MSG_EQ(received.GetButterflyInfo().r3, 37, "butterfly node"); }
        NS_TEST_ASSERT_MSG_EQ(packet->GetSize(), sizeof(payload), "payload size");
        uint8_t copy[sizeof(payload)] = {};
        packet->CopyData(copy, sizeof(copy));
        for (size_t i = 0; i < sizeof(payload); ++i) { NS_TEST_ASSERT_MSG_EQ(copy[i], payload[i], "payload byte"); }
      }
  }
};

NetworkCodingTestSuite::NetworkCodingTestSuite ()
  : TestSuite ("network-coding", Type::UNIT)
{
  AddTestCase (new PacketMetadataTest, Duration::QUICK);
  AddTestCase (new GaloisFieldTestCase, Duration::QUICK);
  AddTestCase (new NetworkCodingTestCase, Duration::QUICK);
}

} // namespace ns3
