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
#include "ns3/simulator.h"
#include <random>

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("NetworkCodingTestSuite");

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
  // Test multiplication
  NS_TEST_ASSERT_MSG_EQ (gf->Multiply (0x53, 0xCA), 0x01, "GF(2^8) multiplication failed");
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
  // Test with different packet sizes and generation sizes
  TestCoding (64, 4);
  TestCoding (1024, 8);
  TestCoding (1500, 16);
  
  // Test with packet loss
  TestCodingWithLoss (1024, 8, 0.1);
  TestCodingWithLoss (1024, 8, 0.2);
  TestCodingWithLoss (1024, 16, 0.3);
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
      
      // Fill with deterministic data
      for (uint32_t j = 0; j < packetSize; j++)
        {
          buffer[j] = (i * j) % 256;
        }
      
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
      Ptr<Packet> codedPacket = encoder->GenerateCodedPacket ();
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
NetworkCodingTestCase::TestCodingWithLoss (uint32_t packetSize, uint16_t generationSize, double lossRate)
{
  // Create encoder and decoder
  Ptr<NetworkCodingEncoder> encoder = CreateObject<NetworkCodingEncoder> (generationSize, packetSize);
  Ptr<NetworkCodingDecoder> decoder = CreateObject<NetworkCodingDecoder> (generationSize, packetSize);
  
  // Create random number generator for packet loss simulation
  std::random_device rd;
  std::mt19937 gen (rd ());
  std::uniform_real_distribution<> dis (0.0, 1.0);
  
  // Create original packets
  for (uint16_t i = 0; i < generationSize; i++)
    {
      uint8_t* buffer = new uint8_t[packetSize];
      
      // Fill with deterministic data
      for (uint32_t j = 0; j < packetSize; j++)
        {
          buffer[j] = (i * j) % 256;
        }
      
      Ptr<Packet> packet = Create<Packet> (buffer, packetSize);
      
      // Add packet to encoder
      encoder->AddPacket (packet, i);
      
      delete[] buffer;
    }
  
  // Generate extra coded packets to account for loss
  uint16_t extraPackets = static_cast<uint16_t> (generationSize * lossRate / (1.0 - lossRate));
  uint16_t totalPackets = generationSize + extraPackets;
  
  // Generate and process coded packets
  uint16_t receivedPackets = 0;
  
  for (uint16_t i = 0; i < totalPackets; i++)
    {
      // Generate a coded packet
      Ptr<Packet> codedPacket = encoder->GenerateCodedPacket ();
      
      // Simulate packet loss
      if (dis (gen) >= lossRate)
        {
          // Packet not lost, process it
          decoder->ProcessCodedPacket (codedPacket);
          receivedPackets++;
        }
      
      // If we can decode, stop sending more packets
      if (decoder->CanDecode ())
        {
          break;
        }
    }
  
  // Verify that we can decode with high probability (may fail occasionally)
  NS_TEST_ASSERT_MSG_EQ (decoder->CanDecode (), true, "Failed to decode with packet loss");
  
  // Verify that we received fewer packets than we sent
  NS_TEST_ASSERT_MSG_LT (receivedPackets, totalPackets, "Received too many packets");
  
  // Get decoded packets and verify their size
  std::vector<Ptr<Packet>> decodedPackets = decoder->GetDecodedPackets ();
  NS_TEST_ASSERT_MSG_EQ (decodedPackets.size (), generationSize, "Number of decoded packets doesn't match generation size");
}

//-----------------------------------------------------------------------------
// NetworkCodingTestSuite implementation
//-----------------------------------------------------------------------------

NetworkCodingTestSuite::NetworkCodingTestSuite ()
  : TestSuite ("network-coding", Type::UNIT)
{
  AddTestCase (new GaloisFieldTestCase, Duration::QUICK);
  AddTestCase (new NetworkCodingTestCase, Duration::QUICK);
}

} // namespace ns3
