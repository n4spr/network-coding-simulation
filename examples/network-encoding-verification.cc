/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/flow-monitor-module.h"
#include "../helper/network-coding-helper.h"
#include "../model/network-coding-encoder.h"
#include "../model/network-coding-packet.h"
#include <fstream>
#include <iomanip>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("NetworkCodingEncoderVerification");

class EncodedPacketMonitor : public Object
{
public:
  static TypeId GetTypeId (void)
  {
    static TypeId tid = TypeId ("ns3::EncodedPacketMonitor")
      .SetParent<Object> ()
      .SetGroupName ("NetworkCoding")
      .AddConstructor<EncodedPacketMonitor> ();
    return tid;
  }

  EncodedPacketMonitor (std::string filename = "")
    : m_packetCount (0),
      m_dumpToFile (!filename.empty())
  {
    if (m_dumpToFile)
      {
        m_outputFile.open (filename.c_str (), std::ios::out);
        NS_ASSERT_MSG (m_outputFile.is_open (), "Failed to open output file " << filename);
      }
  }

  virtual ~EncodedPacketMonitor ()
  {
    if (m_dumpToFile && m_outputFile.is_open())
      {
        m_outputFile.close ();
      }
  }

  void PacketEncoded (Ptr<const Packet> packet)
  {
    m_packetCount++;
    
    Ptr<Packet> packetCopy = packet->Copy ();
    
    NetworkCodingHeader header;
    NetworkCodingHeader tempHeader;
    if (packetCopy->GetSize() >= tempHeader.GetSerializedSize()) 
    {
        packetCopy->PeekHeader(header);
        
        const std::vector<uint8_t>& coefficients = header.GetCoefficients();
        if (!coefficients.empty())
          {
            std::cout << "=== Encoded Packet #" << m_packetCount << " ===" << std::endl;
            std::cout << "Generation ID: " << header.GetGenerationId() << std::endl;
            std::cout << "Generation Size: " << header.GetGenerationSize() << std::endl;
            
            std::cout << "Coefficients: [";
            for (size_t i = 0; i < coefficients.size(); i++)
              {
                std::cout << static_cast<uint32_t>(coefficients[i]);
                if (i < coefficients.size() - 1)
                  {
                    std::cout << ", ";
                  }
              }
            std::cout << "]" << std::endl;
            
            uint32_t nonZeroCoeffs = 0;
            for (uint8_t coeff : coefficients)
              {
                if (coeff != 0)
                  {
                    nonZeroCoeffs++;
                  }
              }
            std::cout << "Non-zero coefficients: " << nonZeroCoeffs << " (" 
                      << (100.0 * nonZeroCoeffs / coefficients.size()) << "%)" << std::endl;
            
            if (m_dumpToFile)
              {
                m_outputFile << "Packet #" << m_packetCount << "\n";
                m_outputFile << "Generation ID: " << header.GetGenerationId() << "\n";
                m_outputFile << "Generation Size: " << header.GetGenerationSize() << "\n";
                m_outputFile << "Coefficients: [";
                for (size_t i = 0; i < coefficients.size(); i++)
                  {
                    m_outputFile << static_cast<uint32_t>(coefficients[i]);
                    if (i < coefficients.size() - 1)
                      {
                        m_outputFile << ", ";
                      }
                  }
                m_outputFile << "]\n";
                
                packetCopy->RemoveHeader (header);
                uint32_t packetSize = packetCopy->GetSize();
                uint8_t buffer[16];
                packetCopy->CopyData(buffer, std::min(packetSize, (uint32_t)16));
                
                m_outputFile << "Data (first 16 bytes): ";
                for (uint32_t i = 0; i < std::min(packetSize, (uint32_t)16); i++)
                  {
                    m_outputFile << std::hex << std::setw(2) << std::setfill('0') 
                                 << static_cast<uint32_t>(buffer[i]) << " ";
                  }
                m_outputFile << std::dec << "\n\n";
              }
          }
        else
          {
            std::cout << "WARNING: Empty coefficients vector in packet" << std::endl;
          }
      }
    else
      {
        std::cout << "WARNING: Packet too small to contain NetworkCodingHeader" << std::endl;
      }
  }

  uint32_t GetPacketCount () const
  {
    return m_packetCount;
  }

  void OpenOutputFile (std::string filename)
  {
    if (m_outputFile.is_open())
      {
        m_outputFile.close ();
      }
    
    m_dumpToFile = true;
    m_packetCount = 0;
    m_outputFile.open (filename.c_str (), std::ios::out);
    NS_ASSERT_MSG (m_outputFile.is_open (), "Failed to open output file " << filename);
  }

private:
  uint32_t m_packetCount;
  bool m_dumpToFile;
  std::ofstream m_outputFile;
};

class EncoderVerificationApp : public Application
{
public:
  static TypeId GetTypeId (void)
  {
    static TypeId tid = TypeId ("ns3::EncoderVerificationApp")
      .SetParent<Application> ()
      .SetGroupName ("NetworkCoding")
      .AddConstructor<EncoderVerificationApp> ()
      .AddAttribute ("PacketSize", "The size of packets to send",
                    UintegerValue (1024),
                    MakeUintegerAccessor (&EncoderVerificationApp::m_packetSize),
                    MakeUintegerChecker<uint16_t> (1, 65507))
      .AddAttribute ("NumPackets", "The number of packets to send",
                    UintegerValue (1000),
                    MakeUintegerAccessor (&EncoderVerificationApp::m_numPackets),
                    MakeUintegerChecker<uint32_t> (1))
      .AddAttribute ("GenerationSize", "The size of each generation",
                    UintegerValue (8),
                    MakeUintegerAccessor (&EncoderVerificationApp::m_generationSize),
                    MakeUintegerChecker<uint16_t> (1, 255))
      .AddAttribute ("RemoteAddress", "The destination Address",
                    AddressValue (),
                    MakeAddressAccessor (&EncoderVerificationApp::m_peer),
                    MakeAddressChecker ())
      .AddAttribute ("RemotePort", "The destination port",
                    UintegerValue (0),
                    MakeUintegerAccessor (&EncoderVerificationApp::m_peerPort),
                    MakeUintegerChecker<uint16_t> ())
      .AddAttribute ("EnableReporting", "Whether to report detailed encoding information",
                     BooleanValue (true),
                     MakeBooleanAccessor (&EncoderVerificationApp::m_enableReporting),
                     MakeBooleanChecker ())
      .AddAttribute ("OutputFile", "File to output encoded packets to",
                     StringValue (""),
                     MakeStringAccessor (&EncoderVerificationApp::m_outputFile),
                     MakeStringChecker ())
      .AddTraceSource ("EncodedPacket", "A packet was encoded",
                     MakeTraceSourceAccessor (&EncoderVerificationApp::m_encodedPacketTrace),
                     "ns3::Packet::TracedCallback");
    return tid;
  }

  EncoderVerificationApp ()
    : m_socket (nullptr),
      m_encoder (nullptr),
      m_packetSize (1024),
      m_numPackets (1000),
      m_generationSize (8),
      m_peerPort (0),
      m_enableReporting (true),
      m_running (false),
      m_packetsSent (0),
      m_nextSeq (0)
  {
    NS_LOG_FUNCTION (this);
  }

  virtual ~EncoderVerificationApp ()
  {
    NS_LOG_FUNCTION (this);
  }

  void Setup (Address address, uint16_t port, uint16_t packetSize, 
             uint32_t numPackets, uint16_t generationSize)
  {
    NS_LOG_FUNCTION (this << address << port << packetSize << numPackets << generationSize);
    
    m_peer = address;
    m_peerPort = port;
    m_packetSize = packetSize;
    m_numPackets = numPackets;
    m_generationSize = generationSize;
  }

  uint32_t GetPacketsSent () const
  {
    return m_packetsSent;
  }

  uint32_t GetGenerationsSent () const
  {
    return m_packetsSent / m_generationSize;
  }

protected:
  virtual void DoDispose (void)
  {
    NS_LOG_FUNCTION (this);
    m_socket = nullptr;
    m_encoder = nullptr;
    Application::DoDispose ();
  }

private:
  virtual void StartApplication (void)
  {
    NS_LOG_FUNCTION (this);
    
    m_running = true;
    m_packetsSent = 0;
    m_nextSeq = 0;
    
    m_encoder = CreateObject<NetworkCodingEncoder> (m_generationSize, m_packetSize);
    
    if (m_enableReporting)
      {
        m_packetMonitor = CreateObject<EncodedPacketMonitor>();
        
        if (!m_outputFile.empty())
          {
            m_packetMonitor->OpenOutputFile(m_outputFile);
          }
          
        m_encodedPacketTrace.ConnectWithoutContext (
          MakeCallback (&EncodedPacketMonitor::PacketEncoded, m_packetMonitor));
      }
    
    if (!m_socket)
      {
        m_socket = Socket::CreateSocket (GetNode (), TcpSocketFactory::GetTypeId ());
        
        if (Inet6SocketAddress::IsMatchingType (m_peer))
          {
            m_socket->Bind6 ();
          }
        else if (InetSocketAddress::IsMatchingType (m_peer))
          {
            m_socket->Bind ();
          }
        
        m_socket->Connect (m_peer);
      }
    
    SendPacket ();
  }

  virtual void StopApplication (void)
  {
    NS_LOG_FUNCTION (this);
    
    m_running = false;
    
    if (m_sendEvent.IsPending())
      {
        Simulator::Cancel (m_sendEvent);
      }
    
    if (m_socket)
      {
        m_socket->Close ();
      }
    
    if (m_enableReporting)
      {
        std::cout << "\nEncoder Verification Results:" << std::endl;
        std::cout << "  Total Packets Sent: " << m_packetsSent << std::endl;
        std::cout << "  Total Generations Sent: " << GetGenerationsSent () << std::endl;
        std::cout << "  Total Encoded Packets Generated: " << m_packetMonitor->GetPacketCount () << std::endl;
        
        double completeness = 100.0;
        if (m_numPackets > 0)
          {
            completeness = 100.0 * m_packetsSent / m_numPackets;
          }
        std::cout << "  Generation Completeness: " << completeness << "%" << std::endl;
      }
  }

  void SendPacket (void)
  {
    NS_LOG_FUNCTION (this);
    
    if (!m_running || m_packetsSent >= m_numPackets)
      {
        return;
      }
    
    Ptr<Packet> packet = Create<Packet> (m_packetSize);
    uint8_t* pktBuffer = new uint8_t[m_packetSize];
    for (uint32_t i = 0; i < m_packetSize; i++) {
      pktBuffer[i] = (m_nextSeq + i) % 256;
    }
    packet = Create<Packet>(pktBuffer, m_packetSize);
    delete[] pktBuffer;
    
    std::cout << "DEBUG: Adding packet " << m_nextSeq << " to encoder" << std::endl;
    
    bool added = m_encoder->AddPacket (packet, m_nextSeq);
    
    if (added)
      {
        std::cout << "DEBUG: Successfully added packet to encoder" << std::endl;
        
        if (m_encoder->IsGenerationComplete ())
          {
            NS_LOG_INFO ("Generation " << m_encoder->GetCurrentGenerationId () 
                        << " is complete, sending coded packets");
            
            for (uint16_t i = 0; i < m_generationSize; i++)
              {
                std::cout << "DEBUG: About to generate coded packet" << std::endl;
                
                Ptr<Packet> codedPacket = m_encoder->GenerateCodedPacket ();
                
                if (codedPacket)
                  {
                    std::cout << "DEBUG: Generated coded packet size: " << codedPacket->GetSize() << std::endl;
                    
                    m_encodedPacketTrace (codedPacket);
                    
                    m_socket->Send (codedPacket);
                    
                    NS_LOG_INFO ("Sent coded packet " << i << " for generation " 
                                << m_encoder->GetCurrentGenerationId ());
                  }
              }
            
            m_encoder->NextGeneration ();
          }
        
        m_packetsSent++;
        m_nextSeq++;
        
        Time delay = Seconds (0.01);
        m_sendEvent = Simulator::Schedule (delay, &EncoderVerificationApp::SendPacket, this);
      }
    else
      {
        NS_LOG_WARN ("Failed to add packet to encoder");
      }
  }

  Ptr<Socket> m_socket;
  Ptr<NetworkCodingEncoder> m_encoder;
  Ptr<EncodedPacketMonitor> m_packetMonitor;
  
  uint16_t m_packetSize;
  uint32_t m_numPackets;
  uint16_t m_generationSize;
  Address m_peer;
  uint16_t m_peerPort;
  bool m_enableReporting;
  std::string m_outputFile;
  
  bool m_running;
  EventId m_sendEvent;
  uint32_t m_packetsSent;
  uint32_t m_nextSeq;
  
  TracedCallback<Ptr<const Packet>> m_encodedPacketTrace;
};

class SimplePacketSink : public Application
{
public:
  static TypeId GetTypeId (void)
  {
    static TypeId tid = TypeId ("ns3::SimplePacketSink")
      .SetParent<Application> ()
      .SetGroupName ("NetworkCoding")
      .AddConstructor<SimplePacketSink> ()
      .AddAttribute ("Local", "The Address on which to Bind the rx socket.",
                     AddressValue (),
                     MakeAddressAccessor (&SimplePacketSink::m_local),
                     MakeAddressChecker ())
      .AddAttribute ("Protocol", "The type id of the Protocol to use for the rx socket.",
                     TypeIdValue (TcpSocketFactory::GetTypeId ()),
                     MakeTypeIdAccessor (&SimplePacketSink::m_tid),
                     MakeTypeIdChecker ())
      .AddTraceSource ("Rx", "A packet was received",
                       MakeTraceSourceAccessor (&SimplePacketSink::m_rxTrace),
                       "ns3::Packet::TracedCallback");
    return tid;
  }

  SimplePacketSink ()
    : m_socket (0),
      m_totalRx (0),
      m_packetsReceived (0)
  {
    NS_LOG_FUNCTION (this);
  }

  virtual ~SimplePacketSink ()
  {
    NS_LOG_FUNCTION (this);
  }

  uint32_t GetTotalRx () const
  {
    return m_totalRx;
  }

  uint32_t GetPacketsReceived () const
  {
    return m_packetsReceived;
  }

protected:
  virtual void DoDispose (void)
  {
    NS_LOG_FUNCTION (this);
    m_socket = 0;
    Application::DoDispose ();
  }

private:
  virtual void StartApplication (void)
  {
    NS_LOG_FUNCTION (this);
    
    if (!m_socket)
      {
        m_socket = Socket::CreateSocket (GetNode (), m_tid);
        m_socket->Bind (m_local);
        m_socket->Listen ();
        m_socket->ShutdownSend ();
        
        if (addressUtils::IsMulticast (m_local))
          {
            Ptr<UdpSocket> udpSocket = DynamicCast<UdpSocket> (m_socket);
            if (udpSocket)
              {
                udpSocket->MulticastJoinGroup (0, m_local);
              }
            else
              {
                NS_FATAL_ERROR ("Error: joining multicast on a non-UDP socket");
              }
          }
      }
    
    m_socket->SetRecvCallback (MakeCallback (&SimplePacketSink::HandleRead, this));
    m_socket->SetAcceptCallback (
      MakeNullCallback<bool, Ptr<Socket>, const Address &> (),
      MakeCallback (&SimplePacketSink::HandleAccept, this));
  }

  virtual void StopApplication (void)
  {
    NS_LOG_FUNCTION (this);
    
    if (m_socket)
      {
        m_socket->Close ();
        m_socket->SetRecvCallback (MakeNullCallback<void, Ptr<Socket>> ());
      }
  }

  void HandleRead (Ptr<Socket> socket)
  {
    NS_LOG_FUNCTION (this << socket);
    
    Ptr<Packet> packet;
    Address from;
    
    while ((packet = socket->RecvFrom (from)))
      {
        if (packet->GetSize () == 0)
          {
            break;
          }
        
        m_totalRx += packet->GetSize ();
        m_packetsReceived++;
        
        NS_LOG_INFO ("Received packet: size=" << packet->GetSize () 
                    << " from=" << from);
        
        m_rxTrace (packet);
        
        NetworkCodingHeader header;
        if (packet->PeekHeader (header))
          {
            NS_LOG_INFO ("  Network Coding Header: GenID=" << header.GetGenerationId () 
                        << " GenSize=" << header.GetGenerationSize ());
          }
      }
  }

  void HandleAccept (Ptr<Socket> socket, const Address& from)
  {
    NS_LOG_FUNCTION (this << socket << from);
    socket->SetRecvCallback (MakeCallback (&SimplePacketSink::HandleRead, this));
  }

  Ptr<Socket> m_socket;
  Address m_local;
  TypeId m_tid;
  uint32_t m_totalRx;
  uint32_t m_packetsReceived;
  TracedCallback<Ptr<const Packet>> m_rxTrace;
};

int main (int argc, char *argv[])
{
  LogComponentEnable ("NetworkCodingEncoderVerification", LOG_LEVEL_INFO);
  
  uint16_t packetSize = 512;
  uint32_t numPackets = 100;
  uint16_t generationSize = 8;
  bool enablePcap = false;
  std::string outputFile = "";
  
  CommandLine cmd (__FILE__);
  cmd.AddValue ("packetSize", "Size of packets to encode", packetSize);
  cmd.AddValue ("numPackets", "Number of packets to encode", numPackets);
  cmd.AddValue ("generationSize", "Size of each generation", generationSize);
  cmd.AddValue ("enablePcap", "Enable PCAP traces", enablePcap);
  cmd.AddValue ("outputFile", "File to output encoded packet details", outputFile);
  cmd.Parse (argc, argv);
  
  NS_LOG_INFO ("Network Coding Encoder Verification");
  NS_LOG_INFO ("  Packet size: " << packetSize << " bytes");
  NS_LOG_INFO ("  Number of packets: " << numPackets);
  NS_LOG_INFO ("  Generation size: " << generationSize);
  NS_LOG_INFO ("  Output file: " << (outputFile.empty() ? "none" : outputFile));
  
  NodeContainer nodes;
  nodes.Create (2);
  
  PointToPointHelper pointToPoint;
  pointToPoint.SetDeviceAttribute ("DataRate", StringValue ("5Mbps"));
  pointToPoint.SetChannelAttribute ("Delay", StringValue ("2ms"));
  
  NetDeviceContainer devices = pointToPoint.Install (nodes);
  
  InternetStackHelper internet;
  internet.Install (nodes);
  
  Ipv4AddressHelper ipv4;
  ipv4.SetBase ("10.1.1.0", "255.255.255.0");
  Ipv4InterfaceContainer interfaces = ipv4.Assign (devices);
  
  uint16_t port = 12345;
  Address sinkAddress (InetSocketAddress (interfaces.GetAddress (1), port));
  
  Ptr<SimplePacketSink> sink = CreateObject<SimplePacketSink> ();
  sink->SetAttribute ("Local", AddressValue (InetSocketAddress (Ipv4Address::GetAny (), port)));
  nodes.Get (1)->AddApplication (sink);
  sink->SetStartTime (Seconds (0.0));
  sink->SetStopTime (Seconds (60.0));
  
  Ptr<EncoderVerificationApp> encoder = CreateObject<EncoderVerificationApp> ();
  encoder->SetAttribute ("RemoteAddress", AddressValue (sinkAddress));
  encoder->SetAttribute ("RemotePort", UintegerValue (port));
  encoder->SetAttribute ("PacketSize", UintegerValue (packetSize));
  encoder->SetAttribute ("NumPackets", UintegerValue (numPackets));
  encoder->SetAttribute ("GenerationSize", UintegerValue (generationSize));
  encoder->SetAttribute ("OutputFile", StringValue (outputFile));
  nodes.Get (0)->AddApplication (encoder);
  encoder->SetStartTime (Seconds (1.0));
  encoder->SetStopTime (Seconds (50.0));
  
  if (enablePcap)
    {
      pointToPoint.EnablePcapAll ("network-coding-encoder-verification");
    }
  
  FlowMonitorHelper flowHelper;
  Ptr<FlowMonitor> flowMonitor = flowHelper.InstallAll();
  
  Simulator::Stop (Seconds (60.0));
  Simulator::Run ();
  
  flowMonitor->CheckForLostPackets ();
  Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier> (flowHelper.GetClassifier ());
  std::map<FlowId, FlowMonitor::FlowStats> stats = flowMonitor->GetFlowStats ();
  
  std::cout << "\nFlow Monitor Statistics:" << std::endl;
  for (std::map<FlowId, FlowMonitor::FlowStats>::const_iterator i = stats.begin (); i != stats.end (); ++i)
    {
      Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow (i->first);
      std::cout << "Flow " << i->first << " (" << t.sourceAddress << " -> " << t.destinationAddress << ")" << std::endl;
      std::cout << "  Tx Packets: " << i->second.txPackets << std::endl;
      std::cout << "  Rx Packets: " << i->second.rxPackets << std::endl;
      std::cout << "  Packet Loss: " << 100.0 * (i->second.txPackets - i->second.rxPackets) / i->second.txPackets << "%" << std::endl;
      std::cout << "  Throughput: " << i->second.rxBytes * 8.0 / (i->second.timeLastRxPacket.GetSeconds () - i->second.timeFirstTxPacket.GetSeconds ()) / 1000 << " kbps" << std::endl;
    }
  
  std::cout << "\nEncoder Statistics:" << std::endl;
  std::cout << "  Packets Sent: " << encoder->GetPacketsSent () << std::endl;
  std::cout << "  Generations Sent: " << encoder->GetGenerationsSent () << std::endl;
  
  std::cout << "Receiver Statistics:" << std::endl;
  std::cout << "  Packets Received: " << sink->GetPacketsReceived () << std::endl;
  std::cout << "  Total Bytes Received: " << sink->GetTotalRx () << std::endl;
  
  Simulator::Destroy ();
  
  return 0;
} // namespace ns3