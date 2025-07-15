/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Complete Butterfly Topology with XOR Network Coding vs TCP/IP Comparison
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

NS_LOG_COMPONENT_DEFINE ("ButterflyXOR");

// Enhanced simulation parameters
struct SimulationParameters {
  uint32_t packetSize = 1024;           // Packet size in bytes
  uint16_t generationSize = 2;          // Number of packets per generation
  uint32_t totalPackets = 2;            // Total number of packets to send
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
  std::string outputFile = "outfile";          // Output file for statistics
  std::string csvFile = "results.csv";             // CSV file for results
  bool runComparison = true;            // Run both XOR and TCP comparison
};

// Enhanced NetworkStats struct
struct NetworkStats {
  uint32_t totalTransmissions = 0;
  uint32_t bottleneckUsage = 0;
  uint32_t successfulDecodings = 0;
  double totalTime = 0;
  double packetLossRate = 0;
  double averageDelay = 0;
  double goodput = 0;
  double throughput = 0;
  uint32_t totalPacketsReceived = 0;
  uint32_t redundantTransmissions = 0;
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

// Enhanced ButterflyXORApp
class ButterflyXORApp : public Application
{
public:
  static TypeId GetTypeId (void)
  {
    static TypeId tid = TypeId ("ns3::ButterflyXORApp")
      .SetParent<Application> ()
      .AddConstructor<ButterflyXORApp> ();
    return tid;
  }

  enum NodeType {
    SOURCE = 0,
    INTERMEDIATE = 1,
    DESTINATION = 2
  };

  ButterflyXORApp ()
    : m_socket (nullptr),
      m_nodeId (0),
      m_nodeType (INTERMEDIATE),
      m_generationSize (2),
      m_packetSize (1024),
      m_totalPackets (2),
      m_packetsSent (0),
      m_packetsReceived (0),
      m_running (false),
      m_decoded (false),
      m_lastForwardedIndex (0),
      m_port (0),
      m_innovativeAcksReceived (0),
      m_retransmissionsSent (0),
      m_retransmissionTimeout (Seconds(2.0))
  {
    m_gf = CreateObject<GaloisField> ();
  }

  void Setup (uint32_t nodeId, NodeType nodeType, uint16_t port, uint32_t packetSize, 
           uint16_t generationSize, uint32_t totalPackets, Address sourceAddress = Address())
  {
    m_nodeId = nodeId;
    m_nodeType = nodeType;
    m_port = port;
    m_packetSize = packetSize;
    m_generationSize = generationSize;
    m_totalPackets = totalPackets;
    m_sourceAddress = sourceAddress;
    
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
              << m_totalPackets << " original packets..." << std::endl;

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
  // Member variables
  Ptr<Socket> m_socket;
  uint32_t m_nodeId;
  NodeType m_nodeType;
  uint16_t m_generationSize;
  uint32_t m_packetSize;
  uint32_t m_totalPackets;
  uint32_t m_packetsSent;
  uint32_t m_packetsReceived;
  bool m_running;
  bool m_decoded;
  uint32_t m_lastForwardedIndex;
  uint16_t m_port;
  
  // Retransmission scheme members
  uint32_t m_innovativeAcksReceived;
  uint32_t m_retransmissionsSent;
  EventId m_retransmissionTimer;
  Time m_retransmissionTimeout;
  Address m_sourceAddress;

  // Network coding objects
  Ptr<NetworkCodingEncoder> m_encoder;
  Ptr<NetworkCodingDecoder> m_decoder;
  Ptr<GaloisField> m_gf;
  
  // Storage for received packets
  std::vector<std::vector<uint8_t>> m_receivedCoeffs;
  std::vector<std::vector<uint8_t>> m_receivedPayloads;
  std::vector<Address> m_destinations;

  // Add member variables to track packets per destination
  uint32_t m_packetsReceivedDest1 = 0;
  uint32_t m_packetsReceivedDest2 = 0;

  virtual void StartApplication (void)
  {
    m_running = true;

    m_socket = Socket::CreateSocket (GetNode (), UdpSocketFactory::GetTypeId ());
    
    m_socket->Bind (InetSocketAddress (Ipv4Address::GetAny (), m_port));
    m_socket->SetRecvCallback (MakeCallback (&ButterflyXORApp::HandleRead, this));

    std::cout << "[STARTUP] Node " << GetNodeName() << " (ID=" << m_nodeId 
              << ") started and listening on port " << m_port << std::endl;

    if (m_nodeType == SOURCE) {
      Simulator::Schedule (Seconds (1.0), &ButterflyXORApp::SendOriginalPackets, this);
      // Start retransmission timer at the source
      m_retransmissionTimer = Simulator::Schedule(m_retransmissionTimeout, &ButterflyXORApp::HandleRetransmissionTimeout, this);
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
    header.SetGenerationId (0);
    header.SetGenerationSize (m_generationSize);
    
    std::vector<uint8_t> coeffs (m_generationSize, 0);
    uint32_t packetIndexInGeneration = (seqNum - 1) % m_generationSize;
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

      // Check for control packet (ACK)
      if (packet->GetSize() < 10) { // Heuristic to check if it could be a control packet
          Ptr<Packet> copy = packet->Copy();
          NetworkCodingControlHeader ctrlHeader;
          if (copy->PeekHeader(ctrlHeader)) {
              if (m_nodeType == SOURCE && ctrlHeader.GetControlType() == NetworkCodingControlHeader::INNOVATIVE_ACK) {
                  HandleInnovativeAck(ctrlHeader);
              }
              continue; // Processed as control packet
          }
      }

      m_packetsReceived++;
      std::cout << "[RECEIVE] Node " << GetNodeName() << " received data packet from " 
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

  void HandleDestinationNode (const std::vector<uint8_t>& coeffs, const std::vector<uint8_t>& payload)
  {
    Ptr<Packet> packet = Create<Packet> (payload.data (), payload.size ());
    
    NetworkCodingHeader header;
    header.SetGenerationId (0);
    header.SetGenerationSize (m_generationSize);
    header.SetCoefficients (coeffs);
    packet->AddHeader (header);

    bool innovative = m_decoder->ProcessCodedPacket (packet);
    
    if (innovative) {
        SendInnovativeAck();
    }

    std::cout << "[" << Simulator::Now ().GetSeconds () << "s] Destination " << GetNodeName () 
              << " processed packet, innovative: " << (innovative ? "YES" : "NO")
              << ", can decode: " << (m_decoder->CanDecode () ? "YES" : "NO") << std::endl;

    // Determine which destination received the packet and increment counter
    if (GetNode()->GetId() == 5) {
      m_packetsReceivedDest1++;
    } else if (GetNode()->GetId() == 6) {
      m_packetsReceivedDest2++;
    }
    
    if (m_decoder->CanDecode () && !m_decoded) {
      m_decoded = true;
      std::cout << "*** DESTINATION " << GetNodeName () << " SUCCESSFULLY DECODED ALL MESSAGES! ***" << std::endl;
      
      auto decodedPackets = m_decoder->GetDecodedPackets ();
      for (size_t i = 0; i < decodedPackets.size (); i++) {
        std::cout << "Decoded packet " << (i+1) << " size: " << decodedPackets[i]->GetSize () << std::endl;
      }
      
      CheckAndStopSimulation();
    }
  }

  void SendInnovativeAck()
  {
      NS_LOG_INFO("Destination " << GetNodeName() << " sending INNOVATIVE_ACK to source " << m_sourceAddress);
      NetworkCodingControlHeader header(NetworkCodingControlHeader::INNOVATIVE_ACK, 0);
      Ptr<Packet> ackPacket = Create<Packet>(0);
      ackPacket->AddHeader(header);
      m_socket->SendTo(ackPacket, 0, m_sourceAddress);
  }

  void HandleInnovativeAck(const NetworkCodingControlHeader& header)
  {
      if (m_nodeType != SOURCE) return;

      m_innovativeAcksReceived++;
      NS_LOG_INFO("Source received INNOVATIVE_ACK. Total ACKs: " << m_innovativeAcksReceived);

      // Reset the retransmission timer since we received useful feedback
      Simulator::Cancel(m_retransmissionTimer);
      m_retransmissionTimer = Simulator::Schedule(m_retransmissionTimeout, &ButterflyXORApp::HandleRetransmissionTimeout, this);

      // If we have received enough ACKs (one from each destination for each original packet), we might not need to retransmit.
      // For the butterfly, 2 packets are sent, and they are XORed. Each destination needs 2 innovative packets to decode.
      // So we expect 4 innovative ACKs in total.
      if (m_innovativeAcksReceived >= m_generationSize * 2) {
          NS_LOG_INFO("All required innovative ACKs received. Stopping retransmission timer.");
          Simulator::Cancel(m_retransmissionTimer);
      }
  }

  void HandleRetransmissionTimeout()
  {
      if (m_nodeType != SOURCE || m_retransmissionsSent >= 3) return;

      NS_LOG_INFO("Source timeout. ACKs received: " << m_innovativeAcksReceived << ". Retransmitting...");
      
      // Retransmit original packets to the intermediate nodes
      SendOriginalPackets();
      m_retransmissionsSent++;

      // Reschedule the timer
      m_retransmissionTimer = Simulator::Schedule(m_retransmissionTimeout, &ButterflyXORApp::HandleRetransmissionTimeout, this);
  }

  void CheckAndStopSimulation()
  {
    static bool destination1Decoded = false;
    static bool destination2Decoded = false;
    
    if (GetNodeName() == "d1") {
      destination1Decoded = true;
    } else if (GetNodeName() == "d2") {
      destination2Decoded = true;
    }
    
    if (destination1Decoded && destination2Decoded) {
      std::cout << "\n*** BOTH DESTINATIONS HAVE DECODED - STOPPING SIMULATION ***" << std::endl;
      Simulator::Stop();
    }
  }

  void HandleIntermediateNode (const std::vector<uint8_t>& coeffs, const std::vector<uint8_t>& payload)
  {
    std::string nodeName = GetNodeName ();
    
    if (nodeName == "r1") {
      SendReceivedPacket (coeffs, payload, InetSocketAddress ("10.1.4.2", m_port));
      SendReceivedPacket (coeffs, payload, InetSocketAddress ("10.1.3.2", m_port));
    }
    else if (nodeName == "r2") {
      SendReceivedPacket (coeffs, payload, InetSocketAddress ("10.1.6.2", m_port));
      SendReceivedPacket (coeffs, payload, InetSocketAddress ("10.1.5.2", m_port));
    }
    else if (nodeName == "r3") {
      // r3 is the bottleneck node that performs XOR coding
      if (m_receivedPayloads.size() >= m_generationSize) {
        uint32_t currentCompleteGenerations = m_receivedPayloads.size() / m_generationSize;
        uint32_t lastProcessedGenerations = m_lastForwardedIndex / m_generationSize;
        
        for (uint32_t gen = lastProcessedGenerations; gen < currentCompleteGenerations; gen++) {
          PerformXORCoding(gen);
        }
        
        m_lastForwardedIndex = currentCompleteGenerations * m_generationSize;
      }
    }
    else if (nodeName == "r4") {
      // r4 now just forwards the coded packet from r3 to the destinations
      SendReceivedPacket (coeffs, payload, InetSocketAddress ("10.1.8.2", m_port)); // Forward to d1
      SendReceivedPacket (coeffs, payload, InetSocketAddress ("10.1.9.2", m_port)); // Forward to d2
    }
  }

  void PerformXORCoding(uint32_t generationId)
  {
    uint32_t startIdx = generationId * m_generationSize;
    uint32_t endIdx = std::min(startIdx + m_generationSize, 
                              static_cast<uint32_t>(m_receivedPayloads.size()));
    
    if (endIdx - startIdx < m_generationSize) return;
    
    std::cout << "[" << Simulator::Now().GetSeconds() << "s] Node r3 performing XOR for generation " 
              << generationId << " (BOTTLENECK)" << std::endl;
    
    // Simple XOR coding: we just XOR all packets together
    std::vector<uint8_t> xorCoeffs(m_generationSize, 1); // All 1's for XOR coding
    std::vector<uint8_t> xorPayload(m_packetSize, 0);
      
    for (uint32_t i = 0; i < m_generationSize; i++) {
      uint32_t packetIdx = startIdx + i;
      if (packetIdx < m_receivedPayloads.size()) {
        for (size_t j = 0; j < m_packetSize; j++) {
          xorPayload[j] ^= m_receivedPayloads[packetIdx][j];
        }
      }
    }

    std::cout << "[" << Simulator::Now().GetSeconds() << "s] Node r3 sending XOR coded packet with coeffs [";
    for (size_t i = 0; i < xorCoeffs.size(); i++) {
      std::cout << (int)xorCoeffs[i];
      if (i < xorCoeffs.size() - 1) std::cout << ",";
    }
    std::cout << "]" << std::endl;

    // Send the single coded packet to r4, which will then forward it
    SendReceivedPacket(xorCoeffs, xorPayload, InetSocketAddress("10.1.7.2", m_port));
  }

  void SendCodedPacketToBothDestinations(const std::vector<uint8_t>& coeffs, const std::vector<uint8_t>& payload)
  {
    Ptr<Packet> packet1 = Create<Packet>(payload.data(), payload.size());
    Ptr<Packet> packet2 = Create<Packet>(payload.data(), payload.size());
    
    NetworkCodingHeader header1, header2;
    header1.SetGenerationId(0);
    header1.SetGenerationSize(m_generationSize);
    header1.SetCoefficients(coeffs);
    
    header2.SetGenerationId(0);
    header2.SetGenerationSize(m_generationSize);
    header2.SetCoefficients(coeffs);
    
    packet1->AddHeader(header1);
    packet2->AddHeader(header2);

    int result1 = m_socket->SendTo(packet1, 0, InetSocketAddress("10.1.8.2", m_port));
    int result2 = m_socket->SendTo(packet2, 0, InetSocketAddress("10.1.9.2", m_port));
    
    if (result1 >= 0) m_packetsSent++;
    if (result2 >= 0) m_packetsSent++;
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
};

// TCP application for comparison
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

  enum NodeType {
    SOURCE = 0,
    INTERMEDIATE = 1,
    DESTINATION = 2
  };

  TcpButterflyApp ()
    : m_nodeId (0),
      m_nodeType (INTERMEDIATE),
      m_port (0),
      m_packetSize (1024),
      m_totalBytesToSend (0),
      m_packetsSent (0),
      m_packetsReceived (0),
      m_running (false),
      m_receivedBothPackets (false),
      m_totalBytesReceived (0),
      m_totalPackets (2)
  {
  }

  void Setup (uint32_t nodeId, NodeType nodeType, uint16_t port, uint32_t packetSize, uint32_t totalPackets)
  {
    m_nodeId = nodeId;
    m_nodeType = nodeType;
    m_port = port;
    m_packetSize = packetSize;
    m_totalPackets = totalPackets;
    m_totalBytesToSend = totalPackets * packetSize;
  }

  void SendOriginalPackets ()
  {
    if (m_nodeType != SOURCE) return;

    std::cout << "[TCP] Source S sending packets via multiple paths..." << std::endl;
    
    ApplicationContainer app1;
    BulkSendHelper bulkSend1 ("ns3::TcpSocketFactory", InetSocketAddress ("10.1.4.2", m_port));
    bulkSend1.SetAttribute ("MaxBytes", UintegerValue (m_totalBytesToSend));
    app1 = bulkSend1.Install (GetNode ());
    app1.Start (Seconds (1.0));
    app1.Stop (Seconds (5.0));
    
    ApplicationContainer app2;
    BulkSendHelper bulkSend2 ("ns3::TcpSocketFactory", InetSocketAddress ("10.1.6.2", m_port));
    bulkSend2.SetAttribute ("MaxBytes", UintegerValue (m_totalBytesToSend));
    app2 = bulkSend2.Install (GetNode ());
    app2.Start (Seconds (1.0));
    app2.Stop (Seconds (5.0));

    // For statistics, we're sending the same data to both destinations
    m_packetsSent = m_totalPackets * 2;
    std::cout << "[TCP] Source sending " << m_totalBytesToSend << " bytes to EACH destination" << std::endl;
  }

  uint32_t GetPacketsSent () const { return m_packetsSent; }
  uint32_t GetPacketsReceived () const { return m_packetsReceived; }
  uint32_t GetTotalBytesReceived () const { return m_totalBytesReceived; }
  uint32_t GetTotalBytesToReceive () const { return m_totalBytesToSend; }
  bool HasReceivedBothPackets () const { return m_receivedBothPackets; }

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
  uint32_t m_nodeId;
  NodeType m_nodeType;
  uint16_t m_port;
  uint32_t m_packetSize;
  uint32_t m_totalBytesToSend;
  uint32_t m_packetsSent;
  uint32_t m_packetsReceived;
  bool m_running;
  bool m_receivedBothPackets;
  uint32_t m_totalBytesReceived;
  uint32_t m_totalPackets;

  virtual void StartApplication (void)
  {
    m_running = true;

    if (m_nodeType == DESTINATION) {
      PacketSinkHelper sinkHelper ("ns3::TcpSocketFactory", 
                                   InetSocketAddress (Ipv4Address::GetAny (), m_port));
      ApplicationContainer sinkApps = sinkHelper.Install (GetNode ());
      sinkApps.Start (Seconds (0.0));
      sinkApps.Stop (Seconds (10.0));
      
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

  void OnPacketReceived (Ptr<const Packet> packet, const Address& from)
  {
    m_packetsReceived++;
    m_totalBytesReceived += packet->GetSize();
    
    std::cout << "[TCP] Destination " << GetNodeName() << " received packet, size=" 
              << packet->GetSize() << " from " << from 
              << " (total: " << m_totalBytesReceived << "/" << m_totalBytesToSend << " bytes)" << std::endl;
    
    if (m_totalBytesReceived >= m_totalBytesToSend) {
      if (!m_receivedBothPackets) {
        m_receivedBothPackets = true;
        std::cout << "*** TCP DESTINATION " << GetNodeName () 
                  << " RECEIVED COMPLETE DATA! ***" << std::endl;
        
        CheckTcpAndStopSimulation();
      }
    }
  }

  void CheckTcpAndStopSimulation()
  {
    static bool tcpDestination1Complete = false;
    static bool tcpDestination2Complete = false;
    
    if (GetNodeName() == "d1") {
      tcpDestination1Complete = true;
    } else if (GetNodeName() == "d2") {
      tcpDestination2Complete = true;
    }
    
    if (tcpDestination1Complete && tcpDestination2Complete) {
      std::cout << "\n*** BOTH TCP DESTINATIONS HAVE RECEIVED ALL DATA - STOPPING SIMULATION ***" << std::endl;
      Simulator::Stop();
    }
  }
};

// Topology creation function
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

// Run XOR Network Coding simulation
NetworkStats RunButterflyXORSimulation (const SimulationParameters& params)
{
  std::cout << "\n=== Running XOR Network Coding Simulation ===" << std::endl;
  
  NetworkStats stats ("XOR Network Coding");
  
  NetDeviceContainer devices;
  Ipv4InterfaceContainer interfaces;
  NodeContainer nodes = CreateExactButterflyTopology (params, devices, interfaces);
  
  std::vector<Ptr<ButterflyXORApp>> apps (7);
  
  // Source S
  apps[0] = CreateObject<ButterflyXORApp> ();
  apps[0]->Setup (0, ButterflyXORApp::SOURCE, params.port, params.packetSize, 
                  params.generationSize, params.totalPackets);
  nodes.Get (0)->AddApplication (apps[0]);
  
  // Intermediate nodes r1, r2, r3, r4
  for (int i = 1; i <= 4; i++) {
    apps[i] = CreateObject<ButterflyXORApp> ();
    apps[i]->Setup (i, ButterflyXORApp::INTERMEDIATE, params.port, params.packetSize, 
                    params.generationSize, params.totalPackets);
    nodes.Get (i)->AddApplication (apps[i]);
  }
  
  // Destinations d1, d2
  for (int i = 5; i <= 6; i++) {
    apps[i] = CreateObject<ButterflyXORApp> ();
    apps[i]->Setup (i, ButterflyXORApp::DESTINATION, params.port, params.packetSize, 
                    params.generationSize, params.totalPackets, interfaces.GetAddress(0,0)); // Pass source address
    nodes.Get (i)->AddApplication (apps[i]);
  }

  // Set application times
  for (int i = 0; i < 7; i++) {
    apps[i]->SetStartTime (Seconds (0.0));
    apps[i]->SetStopTime (Seconds (params.simulationTime));
  }
  
  if (params.enablePcap) {
    PointToPointHelper p2p;
    p2p.EnablePcapAll ("butterfly-xor");
    std::cout << "PCAP tracing enabled (files: butterfly-xor-*.pcap)" << std::endl;
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
  
  // Calculate packets received by destinations only
  uint32_t destinationPacketsReceived = apps[5]->GetPacketsReceived() + apps[6]->GetPacketsReceived();
  
  stats.packetLossRate = totalTxPackets > 0 ? (double)totalLostPackets / totalTxPackets : 0;
  stats.averageDelay = delayCount > 0 ? totalDelay / delayCount : 0;
  stats.goodput = stats.totalTime > 0 ? (destinationPacketsReceived * params.packetSize * 8) / stats.totalTime : 0;
  stats.throughput = stats.totalTime > 0 ? (totalRxPackets * params.packetSize * 8) / stats.totalTime : 0;
  if (params.verbose) {
    std::cout << "Simulation completed. Flow monitor statistics:" << std::endl;
    std::cout << "  Total TX packets: " << totalTxPackets << std::endl;
    std::cout << "  Total RX packets: " << totalRxPackets << std::endl;
    std::cout << "  Total lost packets: " << totalLostPackets << std::endl;
    std::cout << "  Average delay: " << (stats.averageDelay * 1000) << " ms" << std::endl;
    std::cout << "  Total transmission time: " << stats.totalTime << std::endl;
  }
  
  Simulator::Destroy ();
  return stats;
}
// Run TCP comparison simulation
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
  
  // Intermediate nodes
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
  
  stats.bottleneckUsage = 0;
  
  // Calculate actual bytes received by destinations (not using params.packetSize)
  uint32_t destinationBytesReceived = 0;
  uint32_t destinationPacketsReceived = 0;
  
  for (int i = 5; i <= 6; i++) {
    destinationBytesReceived += apps[i]->GetTotalBytesReceived();
    destinationPacketsReceived += apps[i]->GetPacketsReceived();
  }
  
  stats.packetLossRate = totalTxPackets > 0 ? (double)totalLostPackets / totalTxPackets : 0;
  stats.averageDelay = delayCount > 0 ? totalDelay / delayCount : 0;
  
  // Use actual bytes received, not estimated packet size
  stats.goodput = stats.totalTime > 0 ? (destinationBytesReceived * 8) / stats.totalTime : 0;
  stats.throughput = stats.totalTime > 0 ? (totalRxPackets * (destinationBytesReceived / std::max(1u, destinationPacketsReceived)) * 8) / stats.totalTime : 0;

  if (params.verbose) {
    std::cout << "TCP simulation completed. Flow monitor statistics:" << std::endl;
    std::cout << "  Total TX packets: " << totalTxPackets << std::endl;
    std::cout << "  Total RX packets: " << totalRxPackets << std::endl;
    std::cout << "  Total lost packets: " << totalLostPackets << std::endl;
    std::cout << "  Average delay: " << (stats.averageDelay * 1000) << " ms" << std::endl;
    std::cout << "  Total transmission time: " << stats.totalTime << std::endl;
    std::cout << "  Destination bytes received: " << destinationBytesReceived << std::endl;
    std::cout << "  Destination packets received: " << destinationPacketsReceived << std::endl;
    std::cout << "  Average actual packet size: " << (destinationPacketsReceived > 0 ? destinationBytesReceived / destinationPacketsReceived : 0) << " bytes" << std::endl;
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
            << std::setw(20) << params.totalPackets << " packets" << std::endl;
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

void PrintResults (const NetworkStats& xorStats, const NetworkStats& tcpStats)
{
  std::cout << "\n" << std::string (80, '=') << std::endl;
  std::cout << "COMPARISON: XOR NETWORK CODING vs TRADITIONAL TCP/IP" << std::endl;
  std::cout << std::string (80, '=') << std::endl;
  
  std::cout << std::left << std::setw (25) << "Metric" 
            << std::setw (15) << "XOR" 
            << std::setw (15) << "TCP/IP" 
            << std::setw (25) << "XOR Performance" << std::endl;
  std::cout << std::string (80, '-') << std::endl;
  
  std::cout << std::left << std::setw (25) << "Total Transmissions" 
            << std::setw (15) << xorStats.totalTransmissions 
            << std::setw (15) << tcpStats.totalTransmissions 
            << std::setw (25) << (tcpStats.totalTransmissions > xorStats.totalTransmissions ? "Fewer packets" : "More packets") << std::endl;
  
  std::cout << std::left << std::setw (25) << "Bottleneck Usage" 
            << std::setw (15) << xorStats.bottleneckUsage 
            << std::setw (15) << tcpStats.bottleneckUsage 
            << std::setw (25) << (xorStats.bottleneckUsage > 0 ? "Uses bottleneck" : "Bypasses bottleneck") << std::endl;
  
  std::cout << std::left << std::setw (25) << "Success Rate" 
            << std::setw (15) << std::fixed << std::setprecision (1) << (xorStats.GetSuccessRate() * 100) << "%" 
            << std::setw (15) << std::fixed << std::setprecision (1) << (tcpStats.GetSuccessRate() * 100) << "%" 
            << std::setw (25) << (xorStats.GetSuccessRate() >= tcpStats.GetSuccessRate() ? "Equal/Better" : "Worse") << std::endl;
  
  std::cout << std::left << std::setw (25) << "Avg Delay (ms)" 
            << std::setw (15) << std::fixed << std::setprecision (2) << (xorStats.averageDelay * 1000) 
            << std::setw (15) << std::fixed << std::setprecision (2) << (tcpStats.averageDelay * 1000) 
            << std::setw (25) << (xorStats.averageDelay < tcpStats.averageDelay ? "Lower delay" : "Higher delay") << std::endl;
  
  std::cout << std::left << std::setw (25) << "Throughput (bps)" 
            << std::setw (15) << std::fixed << std::setprecision (0) << xorStats.throughput 
            << std::setw (15) << std::fixed << std::setprecision (0) << tcpStats.throughput 
            << std::setw (25) << (xorStats.throughput > tcpStats.throughput ? "Higher" : "Lower") << std::endl;

  std::cout << std::left << std::setw (25) << "Goodput (bps)" 
            << std::setw (15) << std::fixed << std::setprecision (0) << xorStats.goodput 
            << std::setw (15) << std::fixed << std::setprecision (0) << tcpStats.goodput 
            << std::setw (25) << (xorStats.goodput > tcpStats.goodput ? "Higher" : "Lower") << std::endl;
  
  std::cout << "\n" << std::string (80, '=') << std::endl;
  std::cout << "ANALYSIS SUMMARY" << std::endl;
  std::cout << std::string (80, '=') << std::endl;
  
  if (xorStats.GetSuccessRate() >= tcpStats.GetSuccessRate()) {
    std::cout << " XOR: Successfully delivered data to both destinations" << std::endl;
    std::cout << " Network Coding Advantage: Can utilize bottleneck link efficiently" << std::endl;
    std::cout << " XOR uses " << xorStats.bottleneckUsage << " coded transmissions through bottleneck" << std::endl;
  }
  
  if (tcpStats.bottleneckUsage == 0) {
    std::cout << " TCP: Bypassed bottleneck link entirely (direct paths only)" << std::endl;
    std::cout << " TCP: Cannot benefit from network coding - treats bottleneck as unusable" << std::endl;
  }
  
  if (xorStats.totalTransmissions <= tcpStats.totalTransmissions) {
    std::cout << " XOR: More efficient - " << (tcpStats.totalTransmissions - xorStats.totalTransmissions) 
              << " fewer transmissions needed" << std::endl;
  }
  
  std::cout << "\n Key Insight: Network coding allows efficient use of bottleneck links" << std::endl;
  std::cout << "   that traditional routing would avoid!" << std::endl;
}

void WriteToCsv(const std::string& filename, const SimulationParameters& params, const NetworkStats& xorStats, const NetworkStats& tcpStats)
{
    if (filename.empty()) {
        return;
    }

    std::ofstream outFile;
    bool fileExists = std::ifstream(filename).good();

    outFile.open(filename, std::ios_base::app);
    if (!outFile.is_open()) {
        std::cerr << "Error: Could not open CSV file: " << filename << std::endl;
        return;
    }

    // Write header only if the file is new/empty
    if (!fileExists) {
        outFile << "packetSize,genSize,numPackets,errorRate,normalDataRate,bottleneckDataRate,"
                << "tcpTransmissionTime,xorTransmissionTime,tcpTxPackets,xorTxPackets,"
                << "tcpBottleneckUsage,xorBottleneckUsage,tcpSuccessRate,xorSuccessRate,"
                << "tcpAvgDelay,xorAvgDelay,tcpThroughput,xorThroughput,tcpGoodput,xorGoodput\n";
    }

    // Write data row
    outFile << params.packetSize << ","
            << params.generationSize << ","
            << params.totalPackets << ","
            << params.errorRate << ","
            << params.normalDataRate << ","
            << params.bottleneckDataRate << ","
            << tcpStats.totalTime << ","
            << xorStats.totalTime << ","
            << tcpStats.totalTransmissions << ","
            << xorStats.totalTransmissions << ","
            << tcpStats.bottleneckUsage << ","
            << xorStats.bottleneckUsage << ","
            << tcpStats.GetSuccessRate() << ","
            << xorStats.GetSuccessRate() << ","
            << tcpStats.averageDelay << ","
            << xorStats.averageDelay << ","
            << tcpStats.throughput << ","
            << xorStats.throughput << ","
            << tcpStats.goodput << ","
            << xorStats.goodput << "\n";

    outFile.close();
    std::cout << "\nResults appended to " << filename << std::endl;
}


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
  SimulationParameters params;
  
  CommandLine cmd (__FILE__);
  cmd.AddValue ("packetSize", "Size of packets in bytes", params.packetSize);
  cmd.AddValue ("generationSize", "Number of packets per generation", params.generationSize);
  cmd.AddValue ("totalPackets", "Total number of packets to send", params.totalPackets);
  cmd.AddValue ("errorRate", "Error rate for all channels (0.0 to 1.0)", params.errorRate);
  cmd.AddValue ("bottleneckDataRate", "Data rate for bottleneck link", params.bottleneckDataRate);
  cmd.AddValue ("normalDataRate", "Data rate for normal links", params.normalDataRate);
  cmd.AddValue ("simulationTime", "Total simulation time in seconds", params.simulationTime);
  cmd.AddValue ("verbose", "Enable verbose logging", params.verbose);
  cmd.AddValue ("enablePcap", "Enable PCAP tracing", params.enablePcap);
  cmd.AddValue ("runComparison", "Run both XOR and TCP comparison", params.runComparison);
  cmd.AddValue ("csvFile", "CSV file to append results to", params.csvFile);
  
  cmd.Parse (argc, argv);
  
  if (params.totalPackets < params.generationSize) {
    std::cout << "WARNING: totalPackets (" << params.totalPackets 
              << ") is less than generationSize (" << params.generationSize 
              << "). Setting totalPackets = generationSize." << std::endl;
    params.totalPackets = params.generationSize;
  }
  
  if (params.verbose) {
    LogComponentEnable ("ButterflyXOR", LOG_LEVEL_INFO);
  }
  
  std::cout << "Butterfly Topology: XOR Network Coding vs TCP/IP Comparison" << std::endl;
  std::cout << "====================================================================" << std::endl;
  
  PrintSimulationParameters(params);
  
  if (params.runComparison) {
    NetworkStats xorResults = RunButterflyXORSimulation (params);
    NetworkStats tcpResults = RunTcpComparisonSimulation (params);
    PrintResults (xorResults, tcpResults);
    WriteToCsv(params.csvFile, params, xorResults, tcpResults);
  } else {
    NetworkStats results = RunButterflyXORSimulation (params);
    PrintResults (results);
  }
  
  return 0;
}