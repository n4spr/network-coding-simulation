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

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/flow-monitor-module.h"
#include "../helper/network-coding-helper.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("NetworkCodingExample");

/**
 * \brief Network Coding Example
 *
 * This example demonstrates the use of network coding over TCP
 * in a point-to-point topology:
 *
 *   n0 -------------- n1
 *      point-to-point
 *
 * - Node n0 is the sender, using network coding
 * - Node n1 is the receiver, decoding the network coded packets
 * - The link has random packet loss
 * - The receiver can request uncoded packets if needed
 * - Decoding verification is performed and statistics are shown
 */
int
main (int argc, char *argv[])
{
  // Configure log components
  LogComponentEnable ("NetworkCodingExample", LOG_LEVEL_INFO);
  LogComponentEnable ("NetworkCodingUdpApplication", LOG_LEVEL_INFO);
  LogComponentEnable ("NetworkCodingEncoder", LOG_LEVEL_INFO);
  LogComponentEnable ("NetworkCodingDecoder", LOG_LEVEL_INFO);
  
  // Configure command line arguments
  uint32_t packetSize = 512;
  uint32_t numPackets = 10;
  uint16_t generationSize = 5;
  double lossRate = 0.1;
  bool useIpv6 = false;
  bool enableFlowMonitor = false;
  
  CommandLine cmd (__FILE__);
  cmd.AddValue ("packetSize", "Size of packets to send", packetSize);
  cmd.AddValue ("numPackets", "Number of packets to send", numPackets);
  cmd.AddValue ("generationSize", "Size of coding generation", generationSize);
  cmd.AddValue ("lossRate", "Packet loss rate", lossRate);
  cmd.AddValue ("useIpv6", "Use IPv6 instead of IPv4", useIpv6);
  cmd.AddValue ("enableFlowMonitor", "Enable FlowMonitor for statistics", enableFlowMonitor);
  cmd.Parse (argc, argv);
  
  // Print simulation parameters
  NS_LOG_INFO ("Network Coding Example with the following parameters:");
  NS_LOG_INFO ("  Packet size: " << packetSize << " bytes");
  NS_LOG_INFO ("  Number of packets: " << numPackets);
  NS_LOG_INFO ("  Generation size: " << generationSize << " packets");
  NS_LOG_INFO ("  Packet loss rate: " << lossRate * 100.0 << "%");
  NS_LOG_INFO ("  IP version: " << (useIpv6 ? "IPv6" : "IPv4"));
  
  // Create nodes
  NS_LOG_INFO ("Creating nodes...");
  NodeContainer nodes;
  nodes.Create (2);
  
  // Install point-to-point link
  NS_LOG_INFO ("Creating point-to-point link...");
  PointToPointHelper pointToPoint;
  pointToPoint.SetDeviceAttribute ("DataRate", StringValue ("5Mbps"));
  pointToPoint.SetChannelAttribute ("Delay", StringValue ("2ms"));
  
  // Set up error model for packet loss
  Ptr<RateErrorModel> em = CreateObject<RateErrorModel> ();
  em->SetAttribute ("ErrorRate", DoubleValue (lossRate));
  em->SetAttribute ("ErrorUnit", StringValue ("ERROR_UNIT_PACKET"));
  
  // Install network devices
  NetDeviceContainer devices = pointToPoint.Install (nodes);
  
  // Install error model on the receiving device
  if (lossRate > 0.0) 
    {
      devices.Get (1)->SetAttribute ("ReceiveErrorModel", PointerValue (em));
    }
  
  // Install internet stack
  NS_LOG_INFO ("Installing internet stack...");
  InternetStackHelper internet;
  internet.Install (nodes);
  
  // Configure IP addresses
  NS_LOG_INFO ("Assigning IP addresses...");
  
  Address serverAddress;
  Ipv4Address serverIpv4;
  Ipv6Address serverIpv6;
  uint16_t port = 9;
  
  if (useIpv6)
    {
      Ipv6AddressHelper ipv6;
      ipv6.SetBase (Ipv6Address ("2001:db8::"), Ipv6Prefix (64));
      Ipv6InterfaceContainer interfaces = ipv6.Assign (devices);
      serverIpv6 = interfaces.GetAddress (1, 1);
      serverAddress = Inet6SocketAddress (serverIpv6, port);
    }
  else
    {
      Ipv4AddressHelper ipv4;
      ipv4.SetBase ("10.1.1.0", "255.255.255.0");
      Ipv4InterfaceContainer interfaces = ipv4.Assign (devices);
      serverIpv4 = interfaces.GetAddress (1);
      serverAddress = InetSocketAddress (serverIpv4, port);
    }
  
  // Initialize network coding helper
  NS_LOG_INFO ("Setting up network coding applications...");
  NetworkCodingHelper senderHelper (serverAddress, port);
  NetworkCodingHelper receiverHelper (serverAddress, port);
  
  // Set up sender
  senderHelper.ConfigureSender (packetSize, numPackets, generationSize, DataRate ("1Mbps"));
  senderHelper.SetLossRate (lossRate);
  ApplicationContainer senderApp = senderHelper.Install (nodes.Get (0));
  
  // Set up receiver
  receiverHelper.ConfigureReceiver (packetSize, generationSize);
  ApplicationContainer receiverApp = receiverHelper.Install (nodes.Get (1));
  
  // Schedule applications
  senderApp.Start (Seconds (1.0));
  senderApp.Stop (Seconds (30.0));
  receiverApp.Start (Seconds (0.5));
  receiverApp.Stop (Seconds (30.0));
  
  // Set up statistics collection for decoding verification
  NetworkCodingStatsHelper statsHelper;
  statsHelper.AddApplications (senderApp);
  statsHelper.AddApplications (receiverApp);
  
  // Set up flow monitor
  Ptr<FlowMonitor> flowMonitor;
  FlowMonitorHelper flowHelper;
  if (enableFlowMonitor)
    {
      flowMonitor = flowHelper.InstallAll ();
    }
  
  // Enable PCAP captures
  pointToPoint.EnablePcapAll ("network-coding");
  
  // Run simulation
  NS_LOG_INFO ("Running simulation...");
  Simulator::Stop (Seconds (25.0));
  Simulator::Run ();
  
  // Print comprehensive statistics including decoding verification
  NS_LOG_INFO ("Simulation completed.");
  std::cout << std::endl << "=== NETWORK CODING SIMULATION RESULTS ===" << std::endl;
  
  // Basic simulation parameters
  std::cout << "Simulation Parameters:" << std::endl;
  std::cout << "  Packet size: " << packetSize << " bytes" << std::endl;
  std::cout << "  Number of packets: " << numPackets << std::endl;
  std::cout << "  Generation size: " << generationSize << std::endl;
  std::cout << "  Channel loss rate: " << lossRate * 100.0 << "%" << std::endl;
  std::cout << std::endl;
  
  // Network coding statistics
  std::cout << "Network Coding Statistics:" << std::endl;
  statsHelper.PrintStats (std::cout);
  std::cout << std::endl;
  
  // Calculate and display decoding verification metrics
  uint32_t packetsSent = statsHelper.GetPacketsSent();
  uint32_t packetsReceived = statsHelper.GetPacketsReceived();
  uint32_t innovativeReceived = statsHelper.GetInnovativePacketsReceived();
  uint32_t generationsDecoded = statsHelper.GetGenerationsDecoded();
  double codingEfficiency = statsHelper.GetCodingEfficiency();
  double decodingRate = statsHelper.GetDecodingRate();
  
  std::cout << "Decoding Verification Results:" << std::endl;
  std::cout << "  Total packets that should be sent: " << numPackets << std::endl;
  std::cout << "  Packets actually sent: " << packetsSent << std::endl;
  std::cout << "  Total packets received: " << packetsReceived << std::endl;
  std::cout << "  Innovative packets received: " << innovativeReceived << std::endl;
  
  uint32_t expectedGenerations = (numPackets + generationSize - 1) / generationSize; // Ceiling division
  std::cout << "  Expected generations: " << expectedGenerations << std::endl;
  std::cout << "  Generations successfully decoded: " << generationsDecoded << std::endl;
  
  if (expectedGenerations > 0) 
    {
      double generationSuccessRate = 100.0 * generationsDecoded / expectedGenerations;
      std::cout << "  Generation decoding success rate: " << generationSuccessRate << "%" << std::endl;
    }
  
  std::cout << "  Coding efficiency: " << codingEfficiency * 100.0 << "%" << std::endl;
  std::cout << "  Overall decoding rate: " << decodingRate * 100.0 << "%" << std::endl;
  
  // Verify decoding performance
  std::cout << std::endl << "Decoding Performance Analysis:" << std::endl;
  
  if (decodingRate >= 0.9) 
    {
      std::cout << "  ✓ EXCELLENT: Decoding rate >= 90%" << std::endl;
    }
  else if (decodingRate >= 0.7) 
    {
      std::cout << "  ✓ GOOD: Decoding rate >= 70%" << std::endl;
    }
  else if (decodingRate >= 0.5) 
    {
      std::cout << "  ⚠ FAIR: Decoding rate >= 50%" << std::endl;
    }
  else 
    {
      std::cout << "  ✗ POOR: Decoding rate < 50%" << std::endl;
    }
  
  if (codingEfficiency >= 0.8) 
    {
      std::cout << "  ✓ HIGH EFFICIENCY: Most received packets were innovative" << std::endl;
    }
  else if (codingEfficiency >= 0.6) 
    {
      std::cout << "  ⚠ MEDIUM EFFICIENCY: Some redundant packets received" << std::endl;
    }
  else 
    {
      std::cout << "  ✗ LOW EFFICIENCY: Many redundant packets received" << std::endl;
    }
  
  // Calculate expected vs actual redundancy
  if (lossRate > 0 && packetsReceived > 0) 
    {
      uint32_t originalPacketsRecovered = generationsDecoded * generationSize;
      if (originalPacketsRecovered > 0) 
        {
          double redundancyRatio = static_cast<double>(packetsReceived) / originalPacketsRecovered;
          std::cout << "  Redundancy ratio (received/original): " << redundancyRatio << std::endl;
          
          double theoreticalMinimum = 1.0 / (1.0 - lossRate);
          std::cout << "  Theoretical minimum redundancy: " << theoreticalMinimum << std::endl;
          
          if (redundancyRatio <= theoreticalMinimum * 1.2) 
            {
              std::cout << "  ✓ EFFICIENT: Close to theoretical optimum" << std::endl;
            }
          else 
            {
              std::cout << "  ⚠ OVERHEAD: Higher than theoretical optimum" << std::endl;
            }
        }
    }
  
  // Process flow monitor statistics if enabled
  if (enableFlowMonitor)
    {
      flowMonitor->CheckForLostPackets ();
      
      if (!useIpv6) 
        {
          Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier> (flowHelper.GetClassifier ());
          std::map<FlowId, FlowMonitor::FlowStats> stats = flowMonitor->GetFlowStats ();
          
          std::cout << std::endl << "Flow Monitor Statistics:" << std::endl;
          for (auto i = stats.begin (); i != stats.end (); ++i)
            {
              Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow (i->first);
              std::cout << "Flow " << i->first << " (" << t.sourceAddress << " -> " << t.destinationAddress << ")" << std::endl;
              std::cout << "  Tx Packets: " << i->second.txPackets << std::endl;
              std::cout << "  Rx Packets: " << i->second.rxPackets << std::endl;
              
              if (i->second.txPackets > 0) 
                {
                  double lossRatio = 100.0 * (i->second.txPackets - i->second.rxPackets) / i->second.txPackets;
                  std::cout << "  Packet Loss Ratio: " << lossRatio << "%" << std::endl;
                }
              
              if (i->second.timeLastRxPacket.GetSeconds() > i->second.timeFirstTxPacket.GetSeconds()) 
                {
                  double throughput = i->second.rxBytes * 8.0 / 
                                    (i->second.timeLastRxPacket.GetSeconds() - i->second.timeFirstTxPacket.GetSeconds()) / 1000000.0;
                  std::cout << "  Throughput: " << throughput << " Mbps" << std::endl;
                }
            }
        }
    }
  
  // Summary and recommendations
  std::cout << std::endl << "=== SUMMARY ===" << std::endl;
  if (generationsDecoded == expectedGenerations && codingEfficiency > 0.7) 
    {
      std::cout << "✓ SUCCESS: All generations decoded successfully with good efficiency!" << std::endl;
      std::cout << "  Network coding is working correctly." << std::endl;
    }
  else if (generationsDecoded >= expectedGenerations * 0.8) 
    {
      std::cout << "⚠ PARTIAL SUCCESS: Most generations decoded." << std::endl;
      std::cout << "  Consider adjusting parameters for better performance." << std::endl;
    }
  else 
    {
      std::cout << "✗ ISSUES DETECTED: Poor decoding performance." << std::endl;
      std::cout << "  Check network coding implementation or increase redundancy." << std::endl;
    }
  
  // Clean up
  Simulator::Destroy ();
  
  return 0;
}