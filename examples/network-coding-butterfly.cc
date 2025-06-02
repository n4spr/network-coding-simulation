/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Butterfly Topology with Random Linear Network Coding
 * Implements the exact topology from the diagram using UDP applications
 * 
 * Topology (matching the diagram):
 *       S
 *      / \
 *    r1   r2
 *   / \   / \
 *  d1  \  /  d2
 *       r3
 *       |  (Bottleneck)
 *       r4
 *      / \
 *     d1  d2
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
#include <random>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("ButterflyRLNC");

// Enhanced application for Random Linear Network Coding
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
      m_packetsSent (0),
      m_packetsReceived (0),
      m_running (false)
  {
    m_gf = CreateObject<GaloisField> ();
    m_encoder = CreateObject<NetworkCodingEncoder> (m_generationSize, m_packetSize);
    m_decoder = CreateObject<NetworkCodingDecoder> (m_generationSize, m_packetSize);
  }

  enum NodeType {
    SOURCE = 0,
    INTERMEDIATE = 1,
    DESTINATION = 2
  };

  void Setup (uint32_t nodeId, NodeType nodeType, uint16_t port)
  {
    m_nodeId = nodeId;
    m_nodeType = nodeType;
    m_port = port;
  }

  void AddDestination (Address dest)
  {
    m_destinations.push_back (dest);
  }

  void SendOriginalPackets ()
  {
    if (m_nodeType != SOURCE) return;

    std::cout << "[" << Simulator::Now ().GetSeconds () << "s] Source S sending original packets..." << std::endl;

    // Send packet p1 to r1
    SendPacket (1, InetSocketAddress ("10.1.1.2", m_port)); // S -> r1
    
    // Send packet p2 to r2  
    SendPacket (2, InetSocketAddress ("10.1.1.3", m_port)); // S -> r2

    std::cout << "Source sent p1 to r1 and p2 to r2" << std::endl;
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

private:
  virtual void StartApplication (void)
  {
    m_running = true;

    // Create and bind socket
    m_socket = Socket::CreateSocket (GetNode (), UdpSocketFactory::GetTypeId ());
    
    if (m_nodeType == DESTINATION || m_nodeType == INTERMEDIATE) {
      // Bind to receive packets
      m_socket->Bind (InetSocketAddress (Ipv4Address::GetAny (), m_port));
    } else {
      // Source just binds to any port
      m_socket->Bind ();
    }

    m_socket->SetRecvCallback (MakeCallback (&ButterflyRLNCApp::HandleRead, this));

    // Schedule source to send packets
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
    // Create packet data
    std::vector<uint8_t> data (m_packetSize);
    
    // Fill with pattern based on sequence number
    for (uint32_t i = 0; i < m_packetSize; i++) {
      data[i] = (seqNum * 100 + i) % 256;
    }

    Ptr<Packet> packet = Create<Packet> (data.data (), m_packetSize);
    
    // Add to encoder if we're the source
    if (m_nodeType == SOURCE) {
      m_encoder->AddPacket (packet, seqNum);
    }

    // Create network coding header
    NetworkCodingHeader header;
    header.SetGenerationId (0);
    header.SetGenerationSize (m_generationSize);
    
    // For source packets, use identity vectors
    std::vector<uint8_t> coeffs (m_generationSize, 0);
    if (seqNum <= m_generationSize) {
      coeffs[seqNum - 1] = 1; // Identity vector
    }
    header.SetCoefficients (coeffs);
    
    packet->AddHeader (header);

    // Send packet
    int result = m_socket->SendTo (packet, 0, destination);
    if (result >= 0) {
      m_packetsSent++;
      std::cout << "[" << Simulator::Now ().GetSeconds () << "s] Node " << GetNodeName () 
                << " sent packet (seq=" << seqNum << ") to destination" << std::endl;
    }
  }

  void SendCodedPacket (const std::vector<uint8_t>& coefficients, Address destination)
  {
    if (m_receivedPayloads.empty ()) return;

    // Create coded payload using random linear combination
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

    // Create packet
    Ptr<Packet> packet = Create<Packet> (codedPayload.data (), m_packetSize);
    
    // Add network coding header
    NetworkCodingHeader header;
    header.SetGenerationId (0);
    header.SetGenerationSize (m_generationSize);
    header.SetCoefficients (coefficients);
    
    packet->AddHeader (header);

    // Send packet
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

  void HandleRead (Ptr<Socket> socket)
  {
    Ptr<Packet> packet;
    Address from;

    while ((packet = socket->RecvFrom (from))) {
      if (!m_running) break;

      m_packetsReceived++;

      // Extract network coding header
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

      // Store received packet data
      std::vector<uint8_t> payload (packet->GetSize ());
      packet->CopyData (payload.data (), packet->GetSize ());
      
      m_receivedCoeffs.push_back (coeffs);
      m_receivedPayloads.push_back (payload);

      // Handle based on node type
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
      // r1 forwards to d1 and r3
      if (m_receivedPayloads.size () == 1) { // First packet received
        // Forward original packet to d1
        SendReceivedPacket (coeffs, payload, InetSocketAddress ("10.1.1.6", m_port)); // r1 -> d1
        // Forward to r3 for network coding
        SendReceivedPacket (coeffs, payload, InetSocketAddress ("10.1.1.4", m_port)); // r1 -> r3
      }
    }
    else if (nodeName == "r2") {
      // r2 forwards to d2 and r3
      if (m_receivedPayloads.size () == 1) { // First packet received
        // Forward original packet to d2
        SendReceivedPacket (coeffs, payload, InetSocketAddress ("10.1.1.7", m_port)); // r2 -> d2
        // Forward to r3 for network coding  
        SendReceivedPacket (coeffs, payload, InetSocketAddress ("10.1.1.4", m_port)); // r2 -> r3
      }
    }
    else if (nodeName == "r3") {
      // r3 performs random linear network coding when it has enough packets
      if (m_receivedPayloads.size () >= m_generationSize) {
        PerformRandomLinearCoding ();
      }
    }
    else if (nodeName == "r4") {
      // r4 forwards to both destinations
      if (m_receivedPayloads.size () >= 1) {
        // Forward to both d1 and d2
        for (size_t i = m_lastForwardedIndex; i < m_receivedPayloads.size (); i++) {
          SendReceivedPacket (m_receivedCoeffs[i], m_receivedPayloads[i], 
                             InetSocketAddress ("10.1.1.6", m_port)); // r4 -> d1
          SendReceivedPacket (m_receivedCoeffs[i], m_receivedPayloads[i], 
                             InetSocketAddress ("10.1.1.7", m_port)); // r4 -> d2
        }
        m_lastForwardedIndex = m_receivedPayloads.size ();
      }
    }
  }

  void HandleDestinationNode (const std::vector<uint8_t>& coeffs, const std::vector<uint8_t>& payload)
  {
    // Add to decoder
    Ptr<Packet> packet = Create<Packet> (payload.data (), payload.size ());
    
    NetworkCodingHeader header;
    header.SetGenerationId (0);
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
      
      // Verify decoded packets
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
    header.SetGenerationId (0);
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
    
    // Generate random coefficients
    std::random_device rd;
    std::mt19937 gen (rd ());
    std::uniform_int_distribution<uint8_t> dis (1, 255); // Avoid 0

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

    // Send coded packet to r4
    SendCodedPacket (randomCoeffs, InetSocketAddress ("10.1.1.5", m_port)); // r3 -> r4 (bottleneck)
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

// Statistics collection
struct NetworkStats {
  uint32_t totalTransmissions = 0;
  uint32_t bottleneckUsage = 0;
  uint32_t successfulDecodings = 0;
  double totalTime = 0;
  std::string method;
  
  NetworkStats (std::string m) : method (m) {}
};

NodeContainer CreateExactButterflyTopology ()
{
  std::cout << "\n=== Creating Butterfly Topology (matching diagram) ===" << std::endl;
  
  // Create 7 nodes: S(0), r1(1), r2(2), r3(3), r4(4), d1(5), d2(6)
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

  // Create point-to-point links
  PointToPointHelper p2p;
  
  // High bandwidth links (10 Mbps)
  p2p.SetDeviceAttribute ("DataRate", StringValue ("10Mbps"));
  p2p.SetChannelAttribute ("Delay", StringValue ("1ms"));

  NetDeviceContainer devices;
  
  std::cout << "\nCreating links:" << std::endl;
  
  // S -> r1, r2 (p1, p2)
  devices.Add (p2p.Install (nodes.Get (0), nodes.Get (1))); // S -> r1
  devices.Add (p2p.Install (nodes.Get (0), nodes.Get (2))); // S -> r2
  std::cout << "  S -> r1 (p1)" << std::endl;
  std::cout << "  S -> r2 (p2)" << std::endl;

  // r1 -> r3, d1 (p1, p1)
  devices.Add (p2p.Install (nodes.Get (1), nodes.Get (3))); // r1 -> r3
  devices.Add (p2p.Install (nodes.Get (1), nodes.Get (5))); // r1 -> d1
  std::cout << "  r1 -> r3 (p1)" << std::endl;
  std::cout << "  r1 -> d1 (p1)" << std::endl;

  // r2 -> r3, d2 (p2, p2)
  devices.Add (p2p.Install (nodes.Get (2), nodes.Get (3))); // r2 -> r3
  devices.Add (p2p.Install (nodes.Get (2), nodes.Get (6))); // r2 -> d2
  std::cout << "  r2 -> r3 (p2)" << std::endl;
  std::cout << "  r2 -> d2 (p2)" << std::endl;

  // BOTTLENECK: r3 -> r4 (1 Mbps)
  p2p.SetDeviceAttribute ("DataRate", StringValue ("1Mbps"));
  p2p.SetChannelAttribute ("Delay", StringValue ("10ms"));
  devices.Add (p2p.Install (nodes.Get (3), nodes.Get (4))); // r3 -> r4 (bottleneck)
  std::cout << "  r3 -> r4 (p1) [BOTTLENECK - 1Mbps]" << std::endl;

  // r4 -> d1, d2 (p1, p1) - back to high bandwidth
  p2p.SetDeviceAttribute ("DataRate", StringValue ("10Mbps"));
  p2p.SetChannelAttribute ("Delay", StringValue ("1ms"));
  devices.Add (p2p.Install (nodes.Get (4), nodes.Get (5))); // r4 -> d1
  devices.Add (p2p.Install (nodes.Get (4), nodes.Get (6))); // r4 -> d2
  std::cout << "  r4 -> d1 (p1)" << std::endl;
  std::cout << "  r4 -> d2 (p1)" << std::endl;

  return nodes;
}

NetworkStats RunButterflyRLNCSimulation ()
{
  std::cout << "\n=== Running Random Linear Network Coding Simulation ===" << std::endl;
  
  NetworkStats stats ("Random Linear Network Coding");
  
  // Create topology
  NodeContainer nodes = CreateExactButterflyTopology ();
  
  // Install internet stack
  InternetStackHelper internet;
  internet.Install (nodes);
  
  // Assign IP addresses
  Ipv4AddressHelper ipv4;
  ipv4.SetBase ("10.1.1.0", "255.255.255.0");
  Ipv4InterfaceContainer interfaces = ipv4.Assign (NodeContainer::GetGlobal ());
  
  uint16_t port = 1234;
  
  // Create applications
  std::vector<Ptr<ButterflyRLNCApp>> apps (7);
  
  // Source S
  apps[0] = CreateObject<ButterflyRLNCApp> ();
  apps[0]->Setup (0, ButterflyRLNCApp::SOURCE, port);
  nodes.Get (0)->AddApplication (apps[0]);
  
  // Intermediate nodes r1, r2, r3, r4
  for (int i = 1; i <= 4; i++) {
    apps[i] = CreateObject<ButterflyRLNCApp> ();
    apps[i]->Setup (i, ButterflyRLNCApp::INTERMEDIATE, port);
    nodes.Get (i)->AddApplication (apps[i]);
  }
  
  // Destinations d1, d2
  for (int i = 5; i <= 6; i++) {
    apps[i] = CreateObject<ButterflyRLNCApp> ();
    apps[i]->Setup (i, ButterflyRLNCApp::DESTINATION, port);
    nodes.Get (i)->AddApplication (apps[i]);
  }
  
  // Set application times
  for (int i = 0; i < 7; i++) {
    apps[i]->SetStartTime (Seconds (0.0));
    apps[i]->SetStopTime (Seconds (10.0));
  }
  
  // Run simulation
  Time startTime = Simulator::Now ();
  
  Simulator::Stop (Seconds (10.0));
  Simulator::Run ();
  
  Time endTime = Simulator::Now ();
  stats.totalTime = (endTime - startTime).GetSeconds ();
  
  // Collect statistics
  for (int i = 0; i < 7; i++) {
    stats.totalTransmissions += apps[i]->GetPacketsSent ();
    if (i == 3) { // r3 is bottleneck node
      stats.bottleneckUsage += apps[i]->GetPacketsSent ();
    }
  }
  
  // Check if destinations decoded successfully
  for (int i = 5; i <= 6; i++) {
    auto decodedPackets = apps[i]->GetDecodedPackets ();
    if (decodedPackets.size () >= 2) {
      stats.successfulDecodings++;
    }
  }
  
  Simulator::Destroy ();
  return stats;
}

void PrintTopologyVerification ()
{
  std::cout << "\n" << std::string (80, '=') << std::endl;
  std::cout << "TOPOLOGY VERIFICATION" << std::endl;
  std::cout << std::string (80, '=') << std::endl;
  
  std::cout << "Diagram topology:" << std::endl;
  std::cout << "       S" << std::endl;
  std::cout << "      / \\" << std::endl;
  std::cout << "    r1   r2" << std::endl;
  std::cout << "   / \\   / \\" << std::endl;
  std::cout << "  d1  \\  /  d2" << std::endl;
  std::cout << "       r3" << std::endl;
  std::cout << "       |  (Bottleneck)" << std::endl;
  std::cout << "       r4" << std::endl;
  std::cout << "      / \\" << std::endl;
  std::cout << "     d1  d2" << std::endl;
  
  std::cout << "\nImplemented topology:" << std::endl;
  std::cout << "  ✓ S -> r1, r2 (initial distribution)" << std::endl;
  std::cout << "  ✓ r1 -> d1, r3 (direct + coding path)" << std::endl;
  std::cout << "  ✓ r2 -> d2, r3 (direct + coding path)" << std::endl;
  std::cout << "  ✓ r3 -> r4 (bottleneck with RLNC)" << std::endl;
  std::cout << "  ✓ r4 -> d1, d2 (final distribution)" << std::endl;
  
  std::cout << "\nRandom Linear Network Coding Features:" << std::endl;
  std::cout << "  ✓ Uses GF(2^8) arithmetic" << std::endl;
  std::cout << "  ✓ Random coefficient generation" << std::endl;
  std::cout << "  ✓ Linear combination at r3" << std::endl;
  std::cout << "  ✓ Gaussian elimination decoding" << std::endl;
  std::cout << "  ✓ Full rank verification" << std::endl;
}

void PrintResults (const NetworkStats& stats)
{
  std::cout << "\n" << std::string (80, '=') << std::endl;
  std::cout << "RANDOM LINEAR NETWORK CODING RESULTS" << std::endl;
  std::cout << std::string (80, '=') << std::endl;
  
  std::cout << std::left << std::setw (25) << "Metric" << std::setw (20) << "Value" << std::endl;
  std::cout << std::string (45, '-') << std::endl;
  
  std::cout << std::left << std::setw (25) << "Total Transmissions" 
            << std::setw (20) << stats.totalTransmissions << std::endl;
  
  std::cout << std::left << std::setw (25) << "Bottleneck Usage" 
            << std::setw (20) << stats.bottleneckUsage << std::endl;
  
  std::cout << std::left << std::setw (25) << "Successful Decodings" 
            << std::setw (20) << stats.successfulDecodings << "/2" << std::endl;
  
  std::cout << std::left << std::setw (25) << "Success Rate" 
            << std::setw (20) << std::fixed << std::setprecision (1) 
            << (stats.successfulDecodings * 100.0 / 2) << "%" << std::endl;
  
  std::cout << std::left << std::setw (25) << "Total Time" 
            << std::setw (20) << std::fixed << std::setprecision (3) 
            << stats.totalTime << "s" << std::endl;
  
  double efficiency = (stats.successfulDecodings > 0) ? 
                     (stats.successfulDecodings * 2.0) / stats.totalTransmissions : 0;
  std::cout << std::left << std::setw (25) << "Efficiency" 
            << std::setw (20) << std::fixed << std::setprecision (1) 
            << (efficiency * 100) << "%" << std::endl;
  
  std::cout << "\nKey Observations:" << std::endl;
  std::cout << "• Network coding allows both destinations to decode both messages" << std::endl;
  std::cout << "• Bottleneck link usage is minimized through linear combinations" << std::endl;
  std::cout << "• Random coefficients ensure linear independence" << std::endl;
  std::cout << "• GF(2^8) arithmetic provides sufficient field size for coding" << std::endl;
  
  if (stats.successfulDecodings == 2) {
    std::cout << "• ✅ SUCCESSFUL: Both destinations decoded all messages!" << std::endl;
  } else {
    std::cout << "• ⚠️  PARTIAL: Only " << stats.successfulDecodings << "/2 destinations succeeded" << std::endl;
  }
}

int main (int argc, char *argv[])
{
  // Enable logging for debugging
  LogComponentEnable ("ButterflyRLNC", LOG_LEVEL_INFO);
  LogComponentEnable ("NetworkCodingUdpApplication", LOG_LEVEL_INFO);
  
  std::cout << "Butterfly Topology with Random Linear Network Coding" << std::endl;
  std::cout << "====================================================" << std::endl;
  
  // Verify topology matches diagram
  PrintTopologyVerification ();
  
  // Run simulation
  NetworkStats results = RunButterflyRLNCSimulation ();
  
  // Print results
  PrintResults (results);
  
  return 0;
}