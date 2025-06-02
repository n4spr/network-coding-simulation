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

#ifndef NETWORK_CODING_TEST_SUITE_H
#define NETWORK_CODING_TEST_SUITE_H

#include "ns3/test.h"
#include "../model/galois-field.h"
#include "../model/network-coding-encoder.h"
#include "../model/network-coding-decoder.h"
#include "../model/network-coding-udp-application.h"

namespace ns3 {

/**
 * \ingroup network-coding-test
 * \brief Test case for Galois Field arithmetic
 */
class GaloisFieldTestCase : public TestCase
{
public:
  /**
   * \brief Constructor
   */
  GaloisFieldTestCase ();
  
  /**
   * \brief Destructor
   */
  virtual ~GaloisFieldTestCase ();

private:
  /**
   * \brief Run the test
   */
  virtual void DoRun (void);
  
  /**
   * \brief Test addition in GF(2^8)
   * \param gf GaloisField object
   */
  void TestAddition (Ptr<GaloisField> gf);
  
  /**
   * \brief Test multiplication in GF(2^8)
   * \param gf GaloisField object
   */
  void TestMultiplication (Ptr<GaloisField> gf);
  
  /**
   * \brief Test division in GF(2^8)
   * \param gf GaloisField object
   */
  void TestDivision (Ptr<GaloisField> gf);
  
  /**
   * \brief Test inverse in GF(2^8)
   * \param gf GaloisField object
   */
  void TestInverse (Ptr<GaloisField> gf);
};

/**
 * \ingroup network-coding-test
 * \brief Test case for Network Coding Encoder and Decoder
 */
class NetworkCodingTestCase : public TestCase
{
public:
  /**
   * \brief Constructor
   */
  NetworkCodingTestCase ();
  
  /**
   * \brief Destructor
   */
  virtual ~NetworkCodingTestCase ();

private:
  /**
   * \brief Run the test
   */
  virtual void DoRun (void);
  
  /**
   * \brief Test packet encoding and decoding
   * \param packetSize Size of packets
   * \param generationSize Size of generation
   */
  void TestCoding (uint32_t packetSize, uint16_t generationSize);
  
  /**
   * \brief Test packet encoding and decoding with packet loss
   * \param packetSize Size of packets
   * \param generationSize Size of generation
   * \param lossRate Packet loss rate
   */
  void TestCodingWithLoss (uint32_t packetSize, uint16_t generationSize, double lossRate);
};

/**
 * \ingroup network-coding-test
 * \brief Test suite for Network Coding
 */
class NetworkCodingTestSuite : public TestSuite
{
public:
  /**
   * \brief Constructor
   */
  NetworkCodingTestSuite ();
};

} // namespace ns3

#endif /* NETWORK_CODING_TEST_SUITE_H */
