/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Complete Butterfly Topology with Random Linear Network Coding vs TCP/IP Comparison
 * Enhanced version with both RLNC and traditional TCP routing
 */

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/flow-monitor-module.h"
#include "../helper/network-coding-helper.h"
#include "../model/network-coding-udp-application.h"
#include "../model/network-coding-encoder.h"
#include "../model/network-coding-decoder.h"
#include "../model/galois-field.h"
#include <iostream>
#include <iomanip>
#include <fstream>
#include <ctime>
#include <random>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("ButterflyRLNC");

// Enhanced simulation parameters
struct SimulationParameters {
  uint32_t packetSize = 1024;           // Packet size in bytes
  uint16_t generationSize = 2;          // Number of packets per generation
  uint32_t totalPackets = 2;            // NEW: Total number of packets to send
  double errorRate = 0.0;               // Error rate for all channels (0.0 to 1.0)
  std::string bottleneckDataRate = "1Mbps";  // Data rate for bottleneck link
  std::string normalDataRate = "10Mbps";     // Data rate for normal links
  double simulationTime = 10.0;        // Total simulation time in seconds
  uint16_t port = 1234;                 // UDP port number
  bool enablePcap = false;              // Enable PCAP tracing
  bool verbose = false;                 // Enable verbose logging
  double linkDelay = 1.0;               // Link delay in milliseconds
  double bottleneckDelay = 10.0;        // Bottleneck link delay in milliseconds
  uint32_t maxRetransmissions = 3;      // Maximum retransmission attempts
  std::string outputFile = "";          // Output file for statistics
  bool runComparison = true;            // Run both RLNC and TCP comparison
};

// Enhanced NetworkStats struct
struct NetworkStats {
  uint32_t totalTransmissions = 0;
  uint32_t bottleneckUsage = 0;
  uint32_t successfulDecodings = 0;
  double totalTime = 0;
  double packetLossRate = 0;
  double averageDelay = 0;
  double throughput = 0;
  uint32_t totalPacketsReceived = 0;    // NEW
  uint32_t redundantTransmissions = 0;  // NEW
  std::string method;
  
  NetworkStats (std::string m) : method (m) {}
  
  double GetEfficiency() const {
    return totalTransmissions > 0 ? (double)successfulDecodings / totalTransmissions : 0;
  }
  
  double GetSuccessRate() const {
    return successfulDecodings / 2.0; // 2 destinations expected
  }
  
  double GetRedundancyRatio() const {
    return totalPacketsReceived > 0 ? (double)redundantTransmissions / totalPacketsReceived : 0;
  }
};

// Enhanced ButterflyRLNCApp (keeping existing implementation)
class ButterflyRLNCApp : public Application
{
public:
  static TypeId GetTypeId (void)
  {
    static TypeId tid = TypeId ("ns3::ButterflyRLNCApp")
      .SetParent<Application> ()
      .AddConstructor<ButterflyRLNCApp> ();
    return tid;
  }

  ButterflyRLNCApp ()
    : m_socket (nullptr),
      m_nodeId (0),
      m_nodeType (INTERMEDIATE),
      m_generationSize (2),
      m_packetSize (1024),
      m_totalPackets (2),           // NEW: Initialize totalPackets
      m_packetsSent (0),
      m_packetsReceived (0),
      m_running (false)
  {
    m_gf = CreateObject<GaloisField> ();
  }

  enum NodeType {
    SOURCE = 0,
    INTERMEDIATE = 1,
    DESTINATION = 2
  };

  void Setup (uint32_t nodeId, NodeType nodeType, uint16_t port, uint32_t packetSize, 
           uint16_t generationSize, uint32_t totalPackets)
  {
    m_nodeId = nodeId;
    m_nodeType = nodeType;
    m_port = port;
    m_packetSize = packetSize;
    m_generationSize = generationSize;
    m_totalPackets = totalPackets;  // NOW WORKS - member variable exists
    
    m_encoder = CreateObject<NetworkCodingEncoder> (m_generationSize, m_packetSize);
    m_decoder = CreateObject<NetworkCodingDecoder> (m_generationSize, m_packetSize);
  }

  void AddDestination (Address dest)
  {
    m_destinations.push_back (dest);
  }

  void SendOriginalPackets ()
  {
    if (m_nodeType != SOURCE) return;

    std::cout << "[" << Simulator::Now ().GetSeconds () << "s] Source S sending " 
              << m_totalPackets << " original packets..." << std::endl;  // NOW WORKS

    // Send packets based on totalPackets parameter
    for (uint32_t i = 1; i <= m_totalPackets; i++) {
      if (i == 1) {
        SendPacket (i, InetSocketAddress ("10.1.1.2", m_port));  // To r1
      } else if (i == 2) {
        SendPacket (i, InetSocketAddress ("10.1.2.2", m_port));  // To r2
      } else {
        // For additional packets, alternate between r1 and r2
        if (i % 2 == 1) {
          SendPacket (i, InetSocketAddress ("10.1.1.2", m_port));  // To r1
        } else {
          SendPacket (i, InetSocketAddress ("10.1.2.2", m_port));  // To r2
        }
      }
    }

    std::cout << "Source sent " << m_totalPackets << " packets to intermediate nodes" << std::endl;
  }

  uint32_t GetPacketsSent () const { return m_packetsSent; }
  uint32_t GetPacketsReceived () const { return m_packetsReceived; }
  
  std::vector<Ptr<Packet>> GetDecodedPackets ()
  {
    if (m_decoder->CanDecode ()) {
      return m_decoder->GetDecodedPackets ();
    }
    return std::vector<Ptr<Packet>> ();
  }

  // ... rest of existing ButterflyRLNCApp implementation remains the same ...

private:
  virtual void StartApplication (void)
  {
    m_running = true;

    m_socket = Socket::CreateSocket (GetNode (), UdpSocketFactory::GetTypeId ());
    
    m_socket->Bind (InetSocketAddress (Ipv4Address::GetAny (), m_port));
    m_socket->SetRecvCallback (MakeCallback (&ButterflyRLNCApp::HandleRead, this));

    std::cout << "[STARTUP] Node " << GetNodeName() << " (ID=" << m_nodeId 
              << ") started and listening on port " << m_port << std::endl;

    if (m_nodeType == SOURCE) {
      Simulator::Schedule (Seconds (1.0), &ButterflyRLNCApp::SendOriginalPackets, this);
    }
  }

  virtual void StopApplication (void)
  {
    m_running = false;
    if (m_socket) {
      m_socket->Close ();
    }
  }

  void SendPacket (uint32_t seqNum, Address destination)
  {
    std::vector<uint8_t> data (m_packetSize);
    
    for (uint32_t i = 0; i < m_packetSize; i++) {
      data[i] = (seqNum * 100 + i) % 256;
    }

    Ptr<Packet> packet = Create<Packet> (data.data (), m_packetSize);
    
    if (m_nodeType == SOURCE) {
      m_encoder->AddPacket (packet, seqNum);
    }

    NetworkCodingHeader header;
    header.SetGenerationId (0);  // FIXED: Always use generation 0 for simple butterfly
    header.SetGenerationSize (m_generationSize);
    
    // FIXED: Handle coefficients for all packets, not just first generation
    std::vector<uint8_t> coeffs (m_generationSize, 0);
    
    // Calculate which generation this packet belongs to
    uint32_t packetIndexInGeneration = (seqNum - 1) % m_generationSize;
    
    // Set coefficient for this packet's position in its generation
    coeffs[packetIndexInGeneration] = 1;
    
    header.SetCoefficients (coeffs);
    packet->AddHeader (header);

    int result = m_socket->SendTo (packet, 0, destination);
    if (result >= 0) {
      m_packetsSent++;
      std::cout << "[" << Simulator::Now ().GetSeconds () << "s] Node " << GetNodeName () 
                << " sent packet (seq=" << seqNum 
                << ", pos=" << packetIndexInGeneration << ") with coeffs [";
      for (size_t i = 0; i < coeffs.size(); i++) {
        std::cout << (int)coeffs[i];
        if (i < coeffs.size() - 1) std::cout << ",";
      }
      std::cout << "]" << std::endl;
    }
  }

  void HandleRead (Ptr<Socket> socket)
  {
    Ptr<Packet> packet;
    Address from;

    while ((packet = socket->RecvFrom (from))) {
      if (!m_running) break;

      m_packetsReceived++;
      std::cout << "[RECEIVE] Node " << GetNodeName() << " received packet from " 
              << from << ", size=" << packet->GetSize() << std::endl;
      
      NetworkCodingHeader header;
      packet->RemoveHeader (header);

      const std::vector<uint8_t>& coeffs = header.GetCoefficients ();
      
      std::cout << "[" << Simulator::Now ().GetSeconds () << "s] Node " << GetNodeName () 
                << " received packet with coeffs [";
      for (size_t i = 0; i < coeffs.size (); i++) {
        std::cout << (int)coeffs[i];
        if (i < coeffs.size () - 1) std::cout << ",";
      }
      std::cout << "]" << std::endl;

      std::vector<uint8_t> payload (packet->GetSize ());
      packet->CopyData (payload.data (), packet->GetSize ());
      
      m_receivedCoeffs.push_back (coeffs);
      m_receivedPayloads.push_back (payload);

      if (m_nodeType == INTERMEDIATE) {
        HandleIntermediateNode (coeffs, payload);
      } else if (m_nodeType == DESTINATION) {
        HandleDestinationNode (coeffs, payload);
      }
    }
  }

  void HandleIntermediateNode (const std::vector<uint8_t>& coeffs, const std::vector<uint8_t>& payload)
  {
    std::string nodeName = GetNodeName ();
    
    if (nodeName == "r1") {
      // Forward every packet immediately
      SendReceivedPacket (coeffs, payload, InetSocketAddress ("10.1.4.2", m_port));
      SendReceivedPacket (coeffs, payload, InetSocketAddress ("10.1.3.2", m_port));
    }
    else if (nodeName == "r2") {
      // Forward every packet immediately
      SendReceivedPacket (coeffs, payload, InetSocketAddress ("10.1.6.2", m_port));
      SendReceivedPacket (coeffs, payload, InetSocketAddress ("10.1.5.2", m_port));
    }
    else if (nodeName == "r3") {
      // FIXED: Perform network coding when we have enough packets for a generation
      if (m_receivedPayloads.size () >= m_generationSize) {
        // Check if we can form a complete generation
        uint32_t completedGenerations = m_receivedPayloads.size () / m_generationSize;
        
        // Perform coding for each complete generation
        for (uint32_t gen = m_lastForwardedIndex / m_generationSize; 
             gen < completedGenerations; gen++) {
          PerformRandomLinearCodingForGeneration (gen);
        }
        
        m_lastForwardedIndex = completedGenerations * m_generationSize;
      }
    }
    else if (nodeName == "r4") {
      // Forward every received packet
      for (size_t i = m_lastForwardedIndex; i < m_receivedPayloads.size (); i++) {
        SendReceivedPacket (m_receivedCoeffs[i], m_receivedPayloads[i], 
                           InetSocketAddress ("10.1.8.2", m_port));
        SendReceivedPacket (m_receivedCoeffs[i], m_receivedPayloads[i], 
                           InetSocketAddress ("10.1.9.2", m_port));
      }
      m_lastForwardedIndex = m_receivedPayloads.size ();
    }
  }

  // NEW: Network coding for specific generation
  void PerformRandomLinearCodingForGeneration (uint32_t generationId)
  {
    uint32_t startIdx = generationId * m_generationSize;
    uint32_t endIdx = std::min(startIdx + m_generationSize, 
                              static_cast<uint32_t>(m_receivedPayloads.size()));
    
    if (endIdx - startIdx < m_generationSize) return;
    
    std::random_device rd;
    std::mt19937 gen (rd ());
    std::uniform_int_distribution<uint8_t> dis (1, 255);

    std::vector<uint8_t> randomCoeffs (m_generationSize);
    for (size_t i = 0; i < m_generationSize; i++) {
      randomCoeffs[i] = dis (gen);
    }

    std::cout << "[" << Simulator::Now ().GetSeconds () << "s] Node r3 performing RLNC for generation " 
              << generationId << " with coeffs [";
    for (size_t i = 0; i < randomCoeffs.size (); i++) {
      std::cout << (int)randomCoeffs[i];
      if (i < randomCoeffs.size () - 1) std::cout << ",";
    }
    std::cout << "] (BOTTLENECK)" << std::endl;

    // Create coded packet using packets from this generation
    std::vector<uint8_t> codedPayload (m_packetSize, 0);
    
    for (uint32_t i = 0; i < m_generationSize; i++) {
      uint32_t packetIdx = startIdx + i;
      if (packetIdx < m_receivedPayloads.size () && randomCoeffs[i] != 0) {
        for (size_t j = 0; j < m_packetSize; j++) {
          uint8_t product = m_gf->Multiply (randomCoeffs[i], m_receivedPayloads[packetIdx][j]);
          codedPayload[j] = m_gf->Add (codedPayload[j], product);
        }
      }
    }

    Ptr<Packet> packet = Create<Packet> (codedPayload.data (), m_packetSize);
    
    NetworkCodingHeader header;
    header.SetGenerationId (0);  // FIXED: Use generation 0
    header.SetGenerationSize (m_generationSize);
    header.SetCoefficients (randomCoeffs);
    
    packet->AddHeader (header);

    int result = m_socket->SendTo (packet, 0, InetSocketAddress ("10.1.7.2", m_port));
    if (result >= 0) {
      m_packetsSent++;
    }
  }

  // Update HandleDestinationNode to handle multiple generations
  void HandleDestinationNode (const std::vector<uint8_t>& coeffs, const std::vector<uint8_t>& payload)
  {
    // Create packet with original payload
    Ptr<Packet> packet = Create<Packet> (payload.data (), payload.size ());
    
    // Create header with correct generation ID and coefficients
    NetworkCodingHeader header;
    header.SetGenerationId (0);  // FIXED: Use generation 0 for basic butterfly
    header.SetGenerationSize (m_generationSize);
    header.SetCoefficients (coeffs);
    packet->AddHeader (header);

    bool innovative = m_decoder->ProcessCodedPacket (packet);
    
    std::cout << "[" << Simulator::Now ().GetSeconds () << "s] Destination " << GetNodeName () 
              << " processed packet, innovative: " << (innovative ? "YES" : "NO")
              << ", can decode: " << (m_decoder->CanDecode () ? "YES" : "NO") << std::endl;

    if (m_decoder->CanDecode () && !m_decoded) {
      m_decoded = true;
      std::cout << "*** DESTINATION " << GetNodeName () << " SUCCESSFULLY DECODED ALL MESSAGES! ***" << std::endl;
      
      auto decodedPackets = m_decoder->GetDecodedPackets ();
      for (size_t i = 0; i < decodedPackets.size (); i++) {
        std::cout << "Decoded packet " << (i+1) << " size: " << decodedPackets[i]->GetSize () << std::endl;
      }
    }
  }

  void SendReceivedPacket (const std::vector<uint8_t>& coeffs, const std::vector<uint8_t>& payload, Address destination)
  {
    Ptr<Packet> packet = Create<Packet> (payload.data (), payload.size ());
    
    NetworkCodingHeader header;
    header.SetGenerationId (0);  // FIXED: Use generation 0
    header.SetGenerationSize (m_generationSize);
    header.SetCoefficients (coeffs);
    packet->AddHeader (header);

    int result = m_socket->SendTo (packet, 0, destination);
    if (result >= 0) {
      m_packetsSent++;
    }
  }

  void PerformRandomLinearCoding ()
  {
    if (m_receivedPayloads.size () < m_generationSize) return;
    
    std::random_device rd;
    std::mt19937 gen (rd ());
    std::uniform_int_distribution<uint8_t> dis (1, 255);

    std::vector<uint8_t> randomCoeffs (m_generationSize);
    for (size_t i = 0; i < m_generationSize; i++) {
      randomCoeffs[i] = dis (gen);
    }

    std::cout << "[" << Simulator::Now ().GetSeconds () << "s] Node r3 performing RLNC with coeffs [";
    for (size_t i = 0; i < randomCoeffs.size (); i++) {
      std::cout << (int)randomCoeffs[i];
      if (i < randomCoeffs.size () - 1) std::cout << ",";
    }
    std::cout << "] (BOTTLENECK)" << std::endl;

    SendCodedPacket (randomCoeffs, InetSocketAddress ("10.1.7.2", m_port));
  }

  void SendCodedPacket (const std::vector<uint8_t>& coefficients, Address destination)
  {
    if (m_receivedPayloads.empty ()) return;

    std::vector<uint8_t> codedPayload (m_packetSize, 0);
    
    size_t numPackets = std::min (coefficients.size (), m_receivedPayloads.size ());
    
    for (size_t i = 0; i < numPackets; i++) {
      if (i < m_receivedPayloads.size () && coefficients[i] != 0) {
        for (size_t j = 0; j < m_packetSize; j++) {
          uint8_t product = m_gf->Multiply (coefficients[i], m_receivedPayloads[i][j]);
          codedPayload[j] = m_gf->Add (codedPayload[j], product);
        }
      }
    }

    Ptr<Packet> packet = Create<Packet> (codedPayload.data (), m_packetSize);
    
    NetworkCodingHeader header;
    header.SetGenerationId (0);  // FIXED: Use generation 0
    header.SetGenerationSize (m_generationSize);
    header.SetCoefficients (coefficients);
    
    packet->AddHeader (header);

    int result = m_socket->SendTo (packet, 0, destination);
    if (result >= 0) {
      m_packetsSent++;
      std::cout << "[" << Simulator::Now ().GetSeconds () << "s] Node " << GetNodeName () 
                << " sent coded packet with coeffs [";
      for (size_t i = 0; i < coefficients.size (); i++) {
        std::cout << (int)coefficients[i];
        if (i < coefficients.size () - 1) std::cout << ",";
      }
      std::cout << "]" << std::endl;
    }
  }

  std::string GetNodeName () const
  {
    switch (m_nodeId) {
      case 0: return "S";
      case 1: return "r1";
      case 2: return "r2";
      case 3: return "r3";
      case 4: return "r4";
      case 5: return "d1";
      case 6: return "d2";
      default: return "unknown";
    }
  }

  // Member variables
  Ptr<Socket> m_socket;
  uint32_t m_nodeId;
  NodeType m_nodeType;
  uint16_t m_port;
  uint16_t m_generationSize;
  uint32_t m_packetSize;
  uint32_t m_totalPackets;          // NEW: Add this member variable
  uint32_t m_packetsSent;
  uint32_t m_packetsReceived;
  bool m_running;
  bool m_decoded = false;
  size_t m_lastForwardedIndex = 0;

  std::vector<Address> m_destinations;
  std::vector<std::vector<uint8_t>> m_receivedCoeffs;
  std::vector<std::vector<uint8_t>> m_receivedPayloads;

  Ptr<GaloisField> m_gf;
  Ptr<NetworkCodingEncoder> m_encoder;
  Ptr<NetworkCodingDecoder> m_decoder;
};

// FIXED: Traditional TCP application for comparison
class TcpButterflyApp : public Application
{
public:
  static TypeId GetTypeId (void)
  {
    static TypeId tid = TypeId ("ns3::TcpButterflyApp")
      .SetParent<Application> ()
      .AddConstructor<TcpButterflyApp> ();
    return tid;
  }

  TcpButterflyApp ()
    : m_nodeId (0),
      m_nodeType (INTERMEDIATE),
      m_packetSize (1024),
      m_totalPackets (2),
      m_packetsSent (0),
      m_packetsReceived (0),
      m_running (false),
      m_receivedBothPackets (false),
      m_totalBytesReceived (0)
  {
  }

  enum NodeType {
    SOURCE = 0,
    INTERMEDIATE = 1,
    DESTINATION = 2
  };

  void Setup (uint32_t nodeId, NodeType nodeType, uint16_t port, uint32_t packetSize,  uint32_t totalPackets)
  {
    m_nodeId = nodeId;
    m_nodeType = nodeType;
    m_port = port;
    m_packetSize = packetSize;
    m_totalPackets = totalPackets;
  }

void SendOriginalPackets ()
{
    if (m_nodeType != SOURCE) return;

    std::cout << "[TCP] Source S sending packets via multiple paths..." << std::endl;
    
    // Calculate total bytes to send - EACH destination needs ALL data
    uint32_t totalBytesToSend = m_totalPackets * m_packetSize;
    
    // Send ALL data to EACH destination (not split between them)
    // Path 1: S -> r1 -> d1 (send all packets)
    ApplicationContainer app1;
    BulkSendHelper bulkSend1 ("ns3::TcpSocketFactory", InetSocketAddress ("10.1.4.2", m_port));
    bulkSend1.SetAttribute ("MaxBytes", UintegerValue (totalBytesToSend));  // ALL bytes to d1
    app1 = bulkSend1.Install (GetNode ());
    app1.Start (Seconds (1.0));
    app1.Stop (Seconds (5.0));
    
    // Path 2: S -> r2 -> d2 (send all packets)
    ApplicationContainer app2;
    BulkSendHelper bulkSend2 ("ns3::TcpSocketFactory", InetSocketAddress ("10.1.6.2", m_port));
    bulkSend2.SetAttribute ("MaxBytes", UintegerValue (totalBytesToSend));  // ALL bytes to d2
    app2 = bulkSend2.Install (GetNode ());
    app2.Start (Seconds (1.0));
    app2.Stop (Seconds (5.0));

    // TCP sends duplicate data to each destination
    m_packetsSent = m_totalPackets * 2; // Count packets sent to both destinations
    std::cout << "[TCP] Source sending " << totalBytesToSend << " bytes to EACH destination (" 
              << totalBytesToSend << " bytes x 2 destinations = " << (totalBytesToSend * 2) << " total bytes)" << std::endl;
}

  uint32_t GetPacketsSent () const { return m_packetsSent; }
  uint32_t GetPacketsReceived () const { return m_packetsReceived; }
  bool HasReceivedBothPackets () const { return m_receivedBothPackets; }

  // MOVED TO PUBLIC: GetNodeName method
  std::string GetNodeName () const
  {
    switch (m_nodeId) {
      case 0: return "S";
      case 1: return "r1";
      case 2: return "r2";
      case 3: return "r3";
      case 4: return "r4";
      case 5: return "d1";
      case 6: return "d2";
      default: return "unknown";
    }
  }

private:
  virtual void StartApplication (void)
  {
    m_running = true;

    if (m_nodeType == DESTINATION) {
      // FIXED: Use PacketSink with correct trace callback signature
      PacketSinkHelper sinkHelper ("ns3::TcpSocketFactory", 
                                   InetSocketAddress (Ipv4Address::GetAny (), m_port));
      ApplicationContainer sinkApps = sinkHelper.Install (GetNode ());
      sinkApps.Start (Seconds (0.0));
      sinkApps.Stop (Seconds (10.0));
      
      // FIXED: Connect trace with correct signature (packet, address)
      Ptr<PacketSink> sink = DynamicCast<PacketSink> (sinkApps.Get (0));
      if (sink) {
        sink->TraceConnectWithoutContext ("Rx", MakeCallback (&TcpButterflyApp::OnPacketReceived, this));
      }
    }

    std::cout << "[TCP STARTUP] Node " << GetNodeName() << " (ID=" << m_nodeId 
              << ") started" << std::endl;

    if (m_nodeType == SOURCE) {
      Simulator::Schedule (Seconds (1.0), &TcpButterflyApp::SendOriginalPackets, this);
    }
  }

  virtual void StopApplication (void)
  {
    m_running = false;
  }

  // FIXED: Correct callback signature for PacketSink::Rx trace
  void OnPacketReceived (Ptr<const Packet> packet, const Address& from)
  {
      m_packetsReceived++;
      m_totalBytesReceived += packet->GetSize();
      
      // Each destination expects ALL the data (not half)
      uint32_t expectedBytes = m_totalPackets * m_packetSize;
      
      std::cout << "[TCP] Destination " << GetNodeName() << " received packet, size=" 
                << packet->GetSize() << " from " << from 
                << " (total: " << m_totalBytesReceived << "/" << expectedBytes << " bytes)" << std::endl;
      
      if (m_totalBytesReceived >= expectedBytes) {
          if (!m_receivedBothPackets) {
              m_receivedBothPackets = true;
              std::cout << "*** TCP DESTINATION " << GetNodeName () 
                        << " RECEIVED COMPLETE DATA! ***" << std::endl;
          }
      }
  }

  // Member variables
  uint32_t m_nodeId;
  NodeType m_nodeType;
  uint16_t m_port;
  uint32_t m_packetSize;
  uint32_t m_packetsSent;
  uint32_t m_packetsReceived;
  bool m_running;
  bool m_receivedBothPackets;
  uint32_t m_totalBytesReceived;
  uint32_t m_totalPackets;
};

// Updated topology creation function (same as before)
NodeContainer CreateExactButterflyTopology (const SimulationParameters& params, NetDeviceContainer& devices, Ipv4InterfaceContainer& interfaces)
{
  std::cout << "\n=== Creating Butterfly Topology (matching diagram) ===" << std::endl;
  
  NodeContainer nodes;
  nodes.Create (7);
  
  std::cout << "Nodes created:" << std::endl;
  std::cout << "  S  (Source):      Node 0" << std::endl;
  std::cout << "  r1 (Intermediate): Node 1" << std::endl;
  std::cout << "  r2 (Intermediate): Node 2" << std::endl;
  std::cout << "  r3 (Intermediate): Node 3" << std::endl;
  std::cout << "  r4 (Intermediate): Node 4" << std::endl;
  std::cout << "  d1 (Destination): Node 5" << std::endl;
  std::cout << "  d2 (Destination): Node 6" << std::endl;

  InternetStackHelper internet;
  internet.Install (nodes);

  PointToPointHelper p2p;
  Ipv4AddressHelper ipv4;
  
  p2p.SetDeviceAttribute ("DataRate", StringValue (params.normalDataRate));
  p2p.SetChannelAttribute ("Delay", StringValue (std::to_string(params.linkDelay) + "ms"));

  std::cout << "\nCreating links with individual IP assignments:" << std::endl;
  
  // S -> r1 (10.1.1.0/30)
  ipv4.SetBase ("10.1.1.0", "255.255.255.252");
  NetDeviceContainer link1 = p2p.Install (nodes.Get (0), nodes.Get (1));
  interfaces.Add(ipv4.Assign (link1));
  std::cout << "  S -> r1: 10.1.1.1 -> 10.1.1.2" << std::endl;
  
  // S -> r2 (10.1.2.0/30)
  ipv4.SetBase ("10.1.2.0", "255.255.255.252");
  NetDeviceContainer link2 = p2p.Install (nodes.Get (0), nodes.Get (2));
  interfaces.Add(ipv4.Assign (link2));
  std::cout << "  S -> r2: 10.1.2.1 -> 10.1.2.2" << std::endl;

  // r1 -> r3 (10.1.3.0/30)
  ipv4.SetBase ("10.1.3.0", "255.255.255.252");
  NetDeviceContainer link3 = p2p.Install (nodes.Get (1), nodes.Get (3));
  interfaces.Add(ipv4.Assign (link3));
  std::cout << "  r1 -> r3: 10.1.3.1 -> 10.1.3.2" << std::endl;

  // r1 -> d1 (10.1.4.0/30)
  ipv4.SetBase ("10.1.4.0", "255.255.255.252");
  NetDeviceContainer link4 = p2p.Install (nodes.Get (1), nodes.Get (5));
  interfaces.Add(ipv4.Assign (link4));
  std::cout << "  r1 -> d1: 10.1.4.1 -> 10.1.4.2" << std::endl;

  // r2 -> r3 (10.1.5.0/30)
  ipv4.SetBase ("10.1.5.0", "255.255.255.252");
  NetDeviceContainer link5 = p2p.Install (nodes.Get (2), nodes.Get (3));
  interfaces.Add(ipv4.Assign (link5));
  std::cout << "  r2 -> r3: 10.1.5.1 -> 10.1.5.2" << std::endl;

  // r2 -> d2 (10.1.6.0/30)
  ipv4.SetBase ("10.1.6.0", "255.255.255.252");
  NetDeviceContainer link6 = p2p.Install (nodes.Get (2), nodes.Get (6));
  interfaces.Add(ipv4.Assign (link6));
  std::cout << "  r2 -> d2: 10.1.6.1 -> 10.1.6.2" << std::endl;

  // BOTTLENECK: r3 -> r4 (10.1.7.0/30)
  p2p.SetDeviceAttribute ("DataRate", StringValue (params.bottleneckDataRate));
  p2p.SetChannelAttribute ("Delay", StringValue (std::to_string(params.bottleneckDelay) + "ms"));
  ipv4.SetBase ("10.1.7.0", "255.255.255.252");
  NetDeviceContainer link7 = p2p.Install (nodes.Get (3), nodes.Get (4));
  interfaces.Add(ipv4.Assign (link7));
  std::cout << "  r3 -> r4: 10.1.7.1 -> 10.1.7.2 [BOTTLENECK]" << std::endl;

  // r4 -> d1 (10.1.8.0/30)
  p2p.SetDeviceAttribute ("DataRate", StringValue (params.normalDataRate));
  p2p.SetChannelAttribute ("Delay", StringValue (std::to_string(params.linkDelay) + "ms"));
  ipv4.SetBase ("10.1.8.0", "255.255.255.252");
  NetDeviceContainer link8 = p2p.Install (nodes.Get (4), nodes.Get (5));
  interfaces.Add(ipv4.Assign (link8));
  std::cout << "  r4 -> d1: 10.1.8.1 -> 10.1.8.2" << std::endl;

  // r4 -> d2 (10.1.9.0/30)
  ipv4.SetBase ("10.1.9.0", "255.255.255.252");
  NetDeviceContainer link9 = p2p.Install (nodes.Get (4), nodes.Get (6));
  interfaces.Add(ipv4.Assign (link9));
  std::cout << "  r4 -> d2: 10.1.9.1 -> 10.1.9.2" << std::endl;

  devices.Add(link1); devices.Add(link2); devices.Add(link3);
  devices.Add(link4); devices.Add(link5); devices.Add(link6);
  devices.Add(link7); devices.Add(link8); devices.Add(link9);

  if (params.errorRate > 0.0) {
    std::cout << "\nApplying error model with rate " << (params.errorRate * 100) << "% to all links" << std::endl;
    
    for (uint32_t i = 0; i < devices.GetN (); i++) {
      Ptr<RateErrorModel> errorModel = CreateObject<RateErrorModel> ();
      errorModel->SetAttribute ("ErrorRate", DoubleValue (params.errorRate));
      errorModel->SetAttribute ("ErrorUnit", StringValue ("ERROR_UNIT_PACKET"));
      
      devices.Get (i)->SetAttribute ("ReceiveErrorModel", PointerValue (errorModel));
    }
  }

  Ipv4GlobalRoutingHelper::PopulateRoutingTables ();

  return nodes;
}

// Run RLNC simulation (existing function)
NetworkStats RunButterflyRLNCSimulation (const SimulationParameters& params)
{
  std::cout << "\n=== Running Random Linear Network Coding Simulation ===" << std::endl;
  
  NetworkStats stats ("Random Linear Network Coding");
  
  NetDeviceContainer devices;
  Ipv4InterfaceContainer interfaces;
  NodeContainer nodes = CreateExactButterflyTopology (params, devices, interfaces);
  
  std::vector<Ptr<ButterflyRLNCApp>> apps (7);
  
  // Source S
  apps[0] = CreateObject<ButterflyRLNCApp> ();
  apps[0]->Setup (0, ButterflyRLNCApp::SOURCE, params.port, params.packetSize, 
                  params.generationSize, params.totalPackets);  // UPDATED
  nodes.Get (0)->AddApplication (apps[0]);
  
  // Intermediate nodes r1, r2, r3, r4
  for (int i = 1; i <= 4; i++) {
    apps[i] = CreateObject<ButterflyRLNCApp> ();
    apps[i]->Setup (i, ButterflyRLNCApp::INTERMEDIATE, params.port, params.packetSize, 
                    params.generationSize, params.totalPackets);  // UPDATED
    nodes.Get (i)->AddApplication (apps[i]);
  }
  
  // Destinations d1, d2
  for (int i = 5; i <= 6; i++) {
    apps[i] = CreateObject<ButterflyRLNCApp> ();
    apps[i]->Setup (i, ButterflyRLNCApp::DESTINATION, params.port, params.packetSize, 
                    params.generationSize, params.totalPackets);  // UPDATED
    nodes.Get (i)->AddApplication (apps[i]);
  }

  // Set application times
  for (int i = 0; i < 7; i++) {
    apps[i]->SetStartTime (Seconds (0.0));
    apps[i]->SetStopTime (Seconds (params.simulationTime));
  }
  
  if (params.enablePcap) {
    PointToPointHelper p2p;
    p2p.EnablePcapAll ("butterfly-rlnc");
    std::cout << "PCAP tracing enabled (files: butterfly-rlnc-*.pcap)" << std::endl;
  }
  
  FlowMonitorHelper flowHelper;
  Ptr<FlowMonitor> flowMonitor = flowHelper.InstallAll ();
  
  Time startTime = Simulator::Now ();
  
  if (params.verbose) {
    std::cout << "Starting simulation for " << params.simulationTime << " seconds..." << std::endl;
  }
  
  Simulator::Stop (Seconds (params.simulationTime));
  Simulator::Run ();
  
  Time endTime = Simulator::Now ();
  stats.totalTime = (endTime - startTime).GetSeconds ();
  
  flowMonitor->CheckForLostPackets ();
  auto flowStats = flowMonitor->GetFlowStats ();
  
  uint32_t totalTxPackets = 0;
  uint32_t totalRxPackets = 0;
  uint32_t totalLostPackets = 0;
  double totalDelay = 0;
  uint32_t delayCount = 0;
  
  for (auto& flow : flowStats) {
    totalTxPackets += flow.second.txPackets;
    totalRxPackets += flow.second.rxPackets;
    totalLostPackets += flow.second.lostPackets;
    
    if (flow.second.rxPackets > 0) {
      totalDelay += flow.second.delaySum.GetSeconds();
      delayCount += flow.second.rxPackets;
    }
  }
  
  for (int i = 0; i < 7; i++) {
    stats.totalTransmissions += apps[i]->GetPacketsSent ();
    stats.totalPacketsReceived += apps[i]->GetPacketsReceived ();
    if (i == 3) { // r3 is bottleneck node
      stats.bottleneckUsage += apps[i]->GetPacketsSent ();
    }
  }
  
  for (int i = 5; i <= 6; i++) {
    auto decodedPackets = apps[i]->GetDecodedPackets ();
    if (decodedPackets.size () >= params.generationSize) {
      stats.successfulDecodings++;
    }
  }
  
  stats.packetLossRate = totalTxPackets > 0 ? (double)totalLostPackets / totalTxPackets : 0;
  stats.averageDelay = delayCount > 0 ? totalDelay / delayCount : 0;
  stats.throughput = stats.totalTime > 0 ? (totalRxPackets * params.packetSize * 8) / stats.totalTime : 0;
  
  if (params.verbose) {
    std::cout << "Simulation completed. Flow monitor statistics:" << std::endl;
    std::cout << "  Total TX packets: " << totalTxPackets << std::endl;
    std::cout << "  Total RX packets: " << totalRxPackets << std::endl;
    std::cout << "  Total lost packets: " << totalLostPackets << std::endl;
    std::cout << "  Average delay: " << (stats.averageDelay * 1000) << " ms" << std::endl;
  }
  
  Simulator::Destroy ();
  return stats;
}

// FIXED: Run TCP comparison simulation
NetworkStats RunTcpComparisonSimulation (const SimulationParameters& params)
{
  std::cout << "\n=== Running TCP/IP Comparison Simulation ===" << std::endl;
  
  NetworkStats stats ("Traditional TCP/IP");
  
  NetDeviceContainer devices;
  Ipv4InterfaceContainer interfaces;
  NodeContainer nodes = CreateExactButterflyTopology (params, devices, interfaces);
  
  std::vector<Ptr<TcpButterflyApp>> apps (7);
  
  // Source S
  apps[0] = CreateObject<TcpButterflyApp> ();
  apps[0]->Setup (0, TcpButterflyApp::SOURCE, params.port + 100, params.packetSize, params.totalPackets);
  nodes.Get (0)->AddApplication (apps[0]);
  
  // Intermediate nodes (no special handling needed for TCP)
  for (int i = 1; i <= 4; i++) {
    apps[i] = CreateObject<TcpButterflyApp> ();
    apps[i]->Setup (i, TcpButterflyApp::INTERMEDIATE, params.port + 100, params.packetSize, params.totalPackets);
    nodes.Get (i)->AddApplication (apps[i]);
  }
  
  // Destinations d1, d2
  for (int i = 5; i <= 6; i++) {
    apps[i] = CreateObject<TcpButterflyApp> ();
    apps[i]->Setup (i, TcpButterflyApp::DESTINATION, params.port + 100, params.packetSize, params.totalPackets);
    nodes.Get (i)->AddApplication (apps[i]);
  }

  // Set application times
  for (int i = 0; i < 7; i++) {
    apps[i]->SetStartTime (Seconds (0.0));
    apps[i]->SetStopTime (Seconds (params.simulationTime));
  }
  
  if (params.enablePcap) {
    PointToPointHelper p2p;
    p2p.EnablePcapAll ("butterfly-tcp");
    std::cout << "PCAP tracing enabled (files: butterfly-tcp-*.pcap)" << std::endl;
  }
  
  FlowMonitorHelper flowHelper;
  Ptr<FlowMonitor> flowMonitor = flowHelper.InstallAll ();
  
  Time startTime = Simulator::Now ();
  
  if (params.verbose) {
    std::cout << "Starting TCP simulation for " << params.simulationTime << " seconds..." << std::endl;
  }
  
  Simulator::Stop (Seconds (params.simulationTime));
  Simulator::Run ();
  
  Time endTime = Simulator::Now ();
  stats.totalTime = (endTime - startTime).GetSeconds ();
  
  flowMonitor->CheckForLostPackets ();
  auto flowStats = flowMonitor->GetFlowStats ();
  
  uint32_t totalTxPackets = 0;
  uint32_t totalRxPackets = 0;
  uint32_t totalLostPackets = 0;
  double totalDelay = 0;
  uint32_t delayCount = 0;
  
  for (auto& flow : flowStats) {
    totalTxPackets += flow.second.txPackets;
    totalRxPackets += flow.second.rxPackets;
    totalLostPackets += flow.second.lostPackets;
    
    if (flow.second.rxPackets > 0) {
      totalDelay += flow.second.delaySum.GetSeconds();
      delayCount += flow.second.rxPackets;
    }
  }
  
  for (int i = 0; i < 7; i++) {
    stats.totalTransmissions += apps[i]->GetPacketsSent ();
    stats.totalPacketsReceived += apps[i]->GetPacketsReceived ();
  }
  
  // Check TCP destinations
  for (int i = 5; i <= 6; i++) {
    if (apps[i]->HasReceivedBothPackets ()) {
      stats.successfulDecodings++;
    }
    if (params.verbose) {
      std::cout << "TCP Destination " << apps[i]->GetNodeName() 
                << " received data: " << (apps[i]->HasReceivedBothPackets() ? "YES" : "NO") << std::endl;
    }
  }
  
  // TCP uses direct paths, so bottleneck usage should be 0 (bypassed)
  stats.bottleneckUsage = 0;
  
  stats.packetLossRate = totalTxPackets > 0 ? (double)totalLostPackets / totalTxPackets : 0;
  stats.averageDelay = delayCount > 0 ? totalDelay / delayCount : 0;
  stats.throughput = stats.totalTime > 0 ? (totalRxPackets * params.packetSize * 8) / stats.totalTime : 0;
  
  if (params.verbose) {
    std::cout << "TCP simulation completed. Flow monitor statistics:" << std::endl;
    std::cout << "  Total TX packets: " << totalTxPackets << std::endl;
    std::cout << "  Total RX packets: " << totalRxPackets << std::endl;
    std::cout << "  Total lost packets: " << totalLostPackets << std::endl;
    std::cout << "  Average delay: " << (stats.averageDelay * 1000) << " ms" << std::endl;
  }
  
  Simulator::Destroy ();
  return stats;
}


void PrintSimulationParameters(const SimulationParameters& params)
{
  std::cout << "\n" << std::string(80, '=') << std::endl;
  std::cout << "SIMULATION PARAMETERS" << std::endl;
  std::cout << std::string(80, '=') << std::endl;
  
  std::cout << std::left << std::setw(25) << "Parameter" << std::setw(20) << "Value" << std::endl;
  std::cout << std::string(45, '-') << std::endl;
  
  std::cout << std::left << std::setw(25) << "Packet Size" 
            << std::setw(20) << params.packetSize << " bytes" << std::endl;
  std::cout << std::left << std::setw(25) << "Generation Size" 
            << std::setw(20) << params.generationSize << " packets" << std::endl;
  std::cout << std::left << std::setw(25) << "Total Packets" 
            << std::setw(20) << params.totalPackets << " packets" << std::endl;  // NEW
  std::cout << std::left << std::setw(25) << "Error Rate" 
            << std::setw(20) << std::fixed << std::setprecision(3) 
            << (params.errorRate * 100) << "%" << std::endl;
  std::cout << std::left << std::setw(25) << "Bottleneck Data Rate" 
            << std::setw(20) << params.bottleneckDataRate << std::endl;
  std::cout << std::left << std::setw(25) << "Normal Data Rate" 
            << std::setw(20) << params.normalDataRate << std::endl;
  std::cout << std::left << std::setw(25) << "Simulation Time" 
            << std::setw(20) << params.simulationTime << " seconds" << std::endl;
}

// NEW: Enhanced results printing with comparison (OVERLOADED function)
void PrintResults (const NetworkStats& rlncStats, const NetworkStats& tcpStats)
{
  std::cout << "\n" << std::string (80, '=') << std::endl;
  std::cout << "COMPARISON: RLNC vs TRADITIONAL TCP/IP" << std::endl;
  std::cout << std::string (80, '=') << std::endl;
  
  std::cout << std::left << std::setw (25) << "Metric" 
            << std::setw (15) << "RLNC" 
            << std::setw (15) << "TCP/IP" 
            << std::setw (25) << "RLNC Advantage" << std::endl;
  std::cout << std::string (80, '-') << std::endl;
  
  std::cout << std::left << std::setw (25) << "Total Transmissions" 
            << std::setw (15) << rlncStats.totalTransmissions 
            << std::setw (15) << tcpStats.totalTransmissions 
            << std::setw (25) << (tcpStats.totalTransmissions > rlncStats.totalTransmissions ? "Fewer packets" : "More packets") << std::endl;
  
  std::cout << std::left << std::setw (25) << "Bottleneck Usage" 
            << std::setw (15) << rlncStats.bottleneckUsage 
            << std::setw (15) << tcpStats.bottleneckUsage 
            << std::setw (25) << (rlncStats.bottleneckUsage > 0 ? "Uses bottleneck" : "Bypasses bottleneck") << std::endl;
  
  std::cout << std::left << std::setw (25) << "Success Rate" 
            << std::setw (15) << std::fixed << std::setprecision (1) << (rlncStats.GetSuccessRate() * 100) << "%" 
            << std::setw (15) << std::fixed << std::setprecision (1) << (tcpStats.GetSuccessRate() * 100) << "%" 
            << std::setw (25) << (rlncStats.GetSuccessRate() >= tcpStats.GetSuccessRate() ? "Equal/Better" : "Worse") << std::endl;
  
  std::cout << std::left << std::setw (25) << "Avg Delay (ms)" 
            << std::setw (15) << std::fixed << std::setprecision (2) << (rlncStats.averageDelay * 1000) 
            << std::setw (15) << std::fixed << std::setprecision (2) << (tcpStats.averageDelay * 1000) 
            << std::setw (25) << (rlncStats.averageDelay < tcpStats.averageDelay ? "Lower delay" : "Higher delay") << std::endl;
  
  std::cout << std::left << std::setw (25) << "Throughput (bps)" 
            << std::setw (15) << std::fixed << std::setprecision (0) << rlncStats.throughput 
            << std::setw (15) << std::fixed << std::setprecision (0) << tcpStats.throughput 
            << std::setw (25) << (rlncStats.throughput > tcpStats.throughput ? "Higher" : "Lower") << std::endl;

  std::cout << "\n" << std::string (80, '=') << std::endl;
  std::cout << "ANALYSIS SUMMARY" << std::endl;
  std::cout << std::string (80, '=') << std::endl;
  
  if (rlncStats.GetSuccessRate() >= tcpStats.GetSuccessRate()) {
    std::cout << " RLNC: Successfully delivered data to both destinations" << std::endl;
    std::cout << " Network Coding Advantage: Can utilize bottleneck link efficiently" << std::endl;
    std::cout << " RLNC uses " << rlncStats.bottleneckUsage << " coded transmissions through bottleneck" << std::endl;
  }
  
  if (tcpStats.bottleneckUsage == 0) {
    std::cout << " TCP: Bypassed bottleneck link entirely (direct paths only)" << std::endl;
    std::cout << " TCP: Cannot benefit from network coding - treats bottleneck as unusable" << std::endl;
  }
  
  if (rlncStats.totalTransmissions <= tcpStats.totalTransmissions) {
    std::cout << " RLNC: More efficient - " << (tcpStats.totalTransmissions - rlncStats.totalTransmissions) 
              << " fewer transmissions needed" << std::endl;
  }
  
  std::cout << "\n Key Insight: Network coding allows efficient use of bottleneck links" << std::endl;
  std::cout << "   that traditional routing would avoid!" << std::endl;
}

// Keep the existing single PrintResults function as-is
void PrintResults (const NetworkStats& stats)
{
  std::cout << "\n" << std::string (80, '=') << std::endl;
  std::cout << stats.method << " RESULTS" << std::endl;
  std::cout << std::string (80, '=') << std::endl;
  
  std::cout << std::left << std::setw (30) << "Metric" << std::setw (20) << "Value" << std::setw (15) << "Unit" << std::endl;
  std::cout << std::string (65, '-') << std::endl;
  
  std::cout << std::left << std::setw (30) << "Total Transmissions" 
            << std::setw (20) << stats.totalTransmissions 
            << std::setw (15) << "packets" << std::endl;
  
  std::cout << std::left << std::setw (30) << "Bottleneck Usage" 
            << std::setw (20) << stats.bottleneckUsage 
            << std::setw (15) << "packets" << std::endl;
  
  std::cout << std::left << std::setw (30) << "Successful Decodings" 
            << std::setw (20) << (std::to_string(stats.successfulDecodings) + "/2")
            << std::setw (15) << "destinations" << std::endl;
  
  std::cout << std::left << std::setw (30) << "Success Rate" 
            << std::setw (20) << std::fixed << std::setprecision (1) 
            << (stats.GetSuccessRate() * 100) 
            << std::setw (15) << "%" << std::endl;
  
  if (stats.successfulDecodings == 2) {
    std::cout << "✅ SUCCESS: Both destinations decoded all messages!" << std::endl;
  } else {
    std::cout << "⚠️  PARTIAL: Only " << stats.successfulDecodings << "/2 destinations succeeded" << std::endl;
  }
}

int main (int argc, char *argv[])
{
  // Simulation parameters with defaults
  SimulationParameters params;
  
  // ENHANCED: Command line argument parsing
  CommandLine cmd (__FILE__);
  cmd.AddValue ("packetSize", "Size of packets in bytes", params.packetSize);
  cmd.AddValue ("generationSize", "Number of packets per generation", params.generationSize);
  cmd.AddValue ("totalPackets", "Total number of packets to send", params.totalPackets);  // NEW
  cmd.AddValue ("errorRate", "Error rate for all channels (0.0 to 1.0)", params.errorRate);
  cmd.AddValue ("bottleneckDataRate", "Data rate for bottleneck link", params.bottleneckDataRate);
  cmd.AddValue ("normalDataRate", "Data rate for normal links", params.normalDataRate);
  cmd.AddValue ("simulationTime", "Total simulation time in seconds", params.simulationTime);
  cmd.AddValue ("verbose", "Enable verbose logging", params.verbose);
  cmd.AddValue ("enablePcap", "Enable PCAP tracing", params.enablePcap);
  cmd.AddValue ("runComparison", "Run both RLNC and TCP comparison", params.runComparison);
  
  cmd.Parse (argc, argv);
  
  // VALIDATION: Ensure totalPackets >= generationSize
  if (params.totalPackets < params.generationSize) {
    std::cout << "WARNING: totalPackets (" << params.totalPackets 
              << ") is less than generationSize (" << params.generationSize 
              << "). Setting totalPackets = generationSize." << std::endl;
    params.totalPackets = params.generationSize;
  }
  
  // Enable logging based on verbosity
  if (params.verbose) {
    LogComponentEnable ("ButterflyRLNC", LOG_LEVEL_INFO);
  }
  
  std::cout << "Butterfly Topology: Random Linear Network Coding vs TCP/IP Comparison" << std::endl;
  std::cout << "====================================================================" << std::endl;
  
  // Print simulation parameters
  PrintSimulationParameters(params);
  
  if (params.runComparison) {
    // Run both simulations for comparison
    NetworkStats rlncResults = RunButterflyRLNCSimulation (params);
    NetworkStats tcpResults = RunTcpComparisonSimulation (params);
    
    // Print comparison results
    PrintResults (rlncResults, tcpResults);
  } else {
    // Run only RLNC simulation
    NetworkStats results = RunButterflyRLNCSimulation (params);
    PrintResults (results);
  }
  
  return 0;
}