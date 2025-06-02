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
#include <iomanip>  // Add this include for setw, setprecision, fixed

using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("NetworkCodingVsTcpComparison");

// Structure to hold simulation results
struct SimulationResults
{
  std::string protocol;
  uint32_t packetsSent;
  uint32_t packetsReceived;
  uint32_t innovativePackets;
  uint32_t generationsDecoded;
  double throughput;          // bits per second
  double goodput;            // useful bits per second
  double packetLossRate;
  double averageDelay;       // seconds
  double codingEfficiency;   // percentage
  double decodingRate;       // percentage
  Time totalTime;
};

/**
 * \brief Run Network Coding over UDP simulation
 */
SimulationResults RunNetworkCodingSimulation(uint32_t packetSize, uint32_t numPackets, 
                                            uint16_t generationSize, double lossRate)
{
  NS_LOG_INFO ("=== Running Network Coding over UDP Simulation ===");
  
  // Create nodes
  NodeContainer nodes;
  nodes.Create (2);
  
  // Create point-to-point link
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
  InternetStackHelper internet;
  internet.Install (nodes);
  
  // Configure IP addresses
  Ipv4AddressHelper ipv4;
  ipv4.SetBase ("10.1.1.0", "255.255.255.0");
  Ipv4InterfaceContainer interfaces = ipv4.Assign (devices);
  
  uint16_t port = 9;
  Address serverAddress = InetSocketAddress (interfaces.GetAddress (1), port);
  
  // Set up network coding applications
  NetworkCodingHelper senderHelper (serverAddress, port);
  NetworkCodingHelper receiverHelper (serverAddress, port);
  
  // Configure sender and receiver
  senderHelper.ConfigureSender (packetSize, numPackets, generationSize, DataRate ("1Mbps"));
  senderHelper.SetLossRate (lossRate);
  ApplicationContainer senderApp = senderHelper.Install (nodes.Get (0));
  
  receiverHelper.ConfigureReceiver (packetSize, generationSize);
  ApplicationContainer receiverApp = receiverHelper.Install (nodes.Get (1));
  
  // Schedule applications
  senderApp.Start (Seconds (1.0));
  senderApp.Stop (Seconds (30.0));
  receiverApp.Start (Seconds (0.5));
  receiverApp.Stop (Seconds (30.0));
  
  // Set up statistics collection
  NetworkCodingStatsHelper statsHelper;
  statsHelper.AddApplications (senderApp);
  statsHelper.AddApplications (receiverApp);
  
  // Set up flow monitor
  FlowMonitorHelper flowHelper;
  Ptr<FlowMonitor> flowMonitor = flowHelper.InstallAll ();
  
  // Run simulation
  Time startTime = Simulator::Now ();
  Simulator::Stop (Seconds (35.0));
  Simulator::Run ();
  Time endTime = Simulator::Now ();
  
  // Collect results
  SimulationResults results;
  results.protocol = "Network Coding (UDP)";
  results.totalTime = endTime - startTime;
  results.packetsSent = statsHelper.GetPacketsSent ();
  results.packetsReceived = statsHelper.GetPacketsReceived ();
  results.innovativePackets = statsHelper.GetInnovativePacketsReceived ();
  results.generationsDecoded = statsHelper.GetGenerationsDecoded ();
  results.codingEfficiency = statsHelper.GetCodingEfficiency () * 100.0;
  results.decodingRate = statsHelper.GetDecodingRate () * 100.0;
  
  // Calculate throughput and other metrics from flow monitor
  flowMonitor->CheckForLostPackets ();
  Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier> (flowHelper.GetClassifier ());
  std::map<FlowId, FlowMonitor::FlowStats> stats = flowMonitor->GetFlowStats ();
  
  results.throughput = 0.0;
  results.goodput = 0.0;
  results.averageDelay = 0.0;
  results.packetLossRate = 0.0;
  
  for (auto i = stats.begin (); i != stats.end (); ++i)
    {
      if (i->second.timeLastRxPacket.GetSeconds() > i->second.timeFirstTxPacket.GetSeconds())
        {
          double duration = i->second.timeLastRxPacket.GetSeconds() - i->second.timeFirstTxPacket.GetSeconds();
          results.throughput = i->second.rxBytes * 8.0 / duration;
          results.goodput = results.innovativePackets * packetSize * 8.0 / duration;
          
          if (i->second.rxPackets > 0)
            {
              results.averageDelay = i->second.delaySum.GetSeconds() / i->second.rxPackets;
            }
          
          if (i->second.txPackets > 0)
            {
              results.packetLossRate = 1.0 - (double)i->second.rxPackets / i->second.txPackets;
            }
        }
    }
  
  Simulator::Destroy ();
  return results;
}

/**
 * \brief Run Plain TCP simulation
 */
SimulationResults RunTcpSimulation(uint32_t packetSize, uint32_t numPackets, double lossRate)
{
  NS_LOG_INFO ("=== Running Plain TCP Simulation ===");
  
  // Create nodes
  NodeContainer nodes;
  nodes.Create (2);
  
  // Create point-to-point link
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
  InternetStackHelper internet;
  internet.Install (nodes);
  
  // Configure IP addresses
  Ipv4AddressHelper ipv4;
  ipv4.SetBase ("10.1.2.0", "255.255.255.0");
  Ipv4InterfaceContainer interfaces = ipv4.Assign (devices);
  
  uint16_t port = 9;
  
  // Set up TCP applications
  // TCP Sink (receiver)
  PacketSinkHelper sinkHelper ("ns3::TcpSocketFactory", 
                              InetSocketAddress (Ipv4Address::GetAny (), port));
  ApplicationContainer sinkApps = sinkHelper.Install (nodes.Get (1));
  sinkApps.Start (Seconds (0.5));
  sinkApps.Stop (Seconds (30.0));
  
  // TCP Bulk Send (sender)
  BulkSendHelper sourceHelper ("ns3::TcpSocketFactory",
                              InetSocketAddress (interfaces.GetAddress (1), port));
  sourceHelper.SetAttribute ("MaxBytes", UintegerValue (numPackets * packetSize));
  sourceHelper.SetAttribute ("SendSize", UintegerValue (packetSize));
  ApplicationContainer sourceApps = sourceHelper.Install (nodes.Get (0));
  sourceApps.Start (Seconds (1.0));
  sourceApps.Stop (Seconds (30.0));
  
  // Set up flow monitor
  FlowMonitorHelper flowHelper;
  Ptr<FlowMonitor> flowMonitor = flowHelper.InstallAll ();
  
  // Run simulation
  Time startTime = Simulator::Now ();
  Simulator::Stop (Seconds (35.0));
  Simulator::Run ();
  Time endTime = Simulator::Now ();
  
  // Collect results
  SimulationResults results;
  results.protocol = "Plain TCP";
  results.totalTime = endTime - startTime;
  
  // Get statistics from packet sink
  Ptr<PacketSink> sink = DynamicCast<PacketSink> (sinkApps.Get (0));
  results.packetsReceived = sink->GetTotalRx () / packetSize; // Approximate packet count
  results.packetsSent = numPackets; // We requested this many packets worth of data
  results.innovativePackets = results.packetsReceived; // All TCP packets are "innovative"
  results.generationsDecoded = 1; // TCP doesn't use generations
  results.codingEfficiency = 100.0; // No coding overhead
  results.decodingRate = 100.0; // TCP always "decodes" successfully
  
  // Calculate metrics from flow monitor
  flowMonitor->CheckForLostPackets ();
  Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier> (flowHelper.GetClassifier ());
  std::map<FlowId, FlowMonitor::FlowStats> stats = flowMonitor->GetFlowStats ();
  
  results.throughput = 0.0;
  results.goodput = 0.0;
  results.averageDelay = 0.0;
  results.packetLossRate = 0.0;
  
  for (auto i = stats.begin (); i != stats.end (); ++i)
    {
      if (i->second.timeLastRxPacket.GetSeconds() > i->second.timeFirstTxPacket.GetSeconds())
        {
          double duration = i->second.timeLastRxPacket.GetSeconds() - i->second.timeFirstTxPacket.GetSeconds();
          results.throughput = i->second.rxBytes * 8.0 / duration;
          results.goodput = results.throughput; // Same as throughput for TCP
          
          if (i->second.rxPackets > 0)
            {
              results.averageDelay = i->second.delaySum.GetSeconds() / i->second.rxPackets;
            }
          
          if (i->second.txPackets > 0)
            {
              results.packetLossRate = 1.0 - (double)i->second.rxPackets / i->second.txPackets;
            }
        }
    }
  
  Simulator::Destroy ();
  return results;
}

/**
 * \brief Print comparison results
 */
void PrintComparisonResults(const SimulationResults& ncResults, const SimulationResults& tcpResults,
                          uint32_t packetSize, uint32_t numPackets, uint16_t generationSize, double lossRate)
{
  std::cout << "\n" << std::string(80, '=') << std::endl;
  std::cout << "NETWORK CODING vs TCP COMPARISON RESULTS" << std::endl;
  std::cout << std::string(80, '=') << std::endl;
  
  std::cout << "Simulation Parameters:" << std::endl;
  std::cout << "  Packet size: " << packetSize << " bytes" << std::endl;
  std::cout << "  Number of packets: " << numPackets << std::endl;
  std::cout << "  Generation size: " << generationSize << std::endl;
  std::cout << "  Channel loss rate: " << (lossRate * 100) << "%" << std::endl;
  std::cout << std::string(80, '-') << std::endl;

  // Header
  std::cout << std::left 
            << std::setw(20) << "Protocol"
            << std::setw(12) << "Sent"
            << std::setw(12) << "Received" 
            << std::setw(15) << "Throughput"
            << std::setw(15) << "Goodput"
            << std::setw(12) << "Loss %"
            << std::setw(12) << "Avg Delay"
            << std::endl;
  
  std::cout << std::string(80, '-') << std::endl;

  // Network Coding results
  std::cout << std::left 
            << std::setw(20) << ncResults.protocol
            << std::setw(12) << ncResults.packetsSent
            << std::setw(12) << ncResults.packetsReceived
            << std::setw(15) << std::fixed << std::setprecision(1) 
            << (ncResults.throughput / 1000) << " kbps"
            << std::setw(15) << std::fixed << std::setprecision(1)
            << (ncResults.goodput / 1000) << " kbps"
            << std::setw(12) << std::fixed << std::setprecision(1)
            << (ncResults.packetLossRate * 100) << "%"
            << std::setw(12) << std::fixed << std::setprecision(3)
            << ncResults.averageDelay << " s"
            << std::endl;

  // TCP results
  std::cout << std::left 
            << std::setw(20) << tcpResults.protocol
            << std::setw(12) << tcpResults.packetsSent
            << std::setw(12) << tcpResults.packetsReceived
            << std::setw(15) << std::fixed << std::setprecision(1) 
            << (tcpResults.throughput / 1000) << " kbps"
            << std::setw(15) << std::fixed << std::setprecision(1)
            << (tcpResults.goodput / 1000) << " kbps"
            << std::setw(12) << std::fixed << std::setprecision(1)
            << (tcpResults.packetLossRate * 100) << "%"
            << std::setw(12) << std::fixed << std::setprecision(3)
            << tcpResults.averageDelay << " s"
            << std::endl;

  std::cout << std::string(80, '-') << std::endl;

  // Detailed analysis
  std::cout << "\nDetailed Analysis:" << std::endl;
  
  std::cout << "\nNetwork Coding (UDP):" << std::endl;
  std::cout << "  Innovative packets: " << ncResults.innovativePackets << std::endl;
  std::cout << "  Generations decoded: " << ncResults.generationsDecoded << std::endl;
  std::cout << "  Coding efficiency: " << std::fixed << std::setprecision(1) 
            << ncResults.codingEfficiency << "%" << std::endl;
  std::cout << "  Decoding rate: " << std::fixed << std::setprecision(1) 
            << ncResults.decodingRate << "%" << std::endl;
  std::cout << "  Throughput: " << std::fixed << std::setprecision(2) 
            << (ncResults.throughput / 1000000) << " Mbps" << std::endl;
  std::cout << "  Goodput: " << std::fixed << std::setprecision(2) 
            << (ncResults.goodput / 1000000) << " Mbps" << std::endl;

  std::cout << "\nPlain TCP:" << std::endl;
  std::cout << "  Packets received: " << tcpResults.packetsReceived << std::endl;
  std::cout << "  Reliability: " << std::fixed << std::setprecision(1) 
            << (100.0 - tcpResults.packetLossRate * 100) << "%" << std::endl;
  std::cout << "  Throughput: " << std::fixed << std::setprecision(2) 
            << (tcpResults.throughput / 1000000) << " Mbps" << std::endl;
  std::cout << "  Goodput: " << std::fixed << std::setprecision(2) 
            << (tcpResults.goodput / 1000000) << " Mbps" << std::endl;

  // Performance comparison
  std::cout << "\n" << std::string(50, '=') << std::endl;
  std::cout << "PERFORMANCE COMPARISON" << std::endl;
  std::cout << std::string(50, '=') << std::endl;

  std::cout << "\nThroughput Comparison:" << std::endl;
  std::cout << "  Network Coding: " 
            << std::fixed << std::setprecision(1)
            << (ncResults.throughput / tcpResults.throughput * 100) << "% of TCP" << std::endl;

  std::cout << "\nReliability Comparison:" << std::endl;
  std::cout << "  Network Coding effective loss: " 
            << std::fixed << std::setprecision(1)
            << (ncResults.packetLossRate * 100) << "%" << std::endl;
  std::cout << "  TCP effective loss: " 
            << std::fixed << std::setprecision(1)
            << (tcpResults.packetLossRate * 100) << "%" << std::endl;

  std::cout << "\nDelay Comparison:" << std::endl;
  if (tcpResults.averageDelay > 0)
    {
      std::cout << "  Network Coding: " 
                << std::fixed << std::setprecision(1)
                << (ncResults.averageDelay / tcpResults.averageDelay * 100) << "% of TCP delay" << std::endl;
    }

  std::cout << "\nEfficiency Analysis:" << std::endl;
  std::cout << "  Network Coding efficiency: " << ncResults.codingEfficiency << "%" << std::endl;
  std::cout << "  TCP efficiency: " << tcpResults.codingEfficiency << "% (no coding overhead)" << std::endl;

  std::cout << "\n" << std::string(80, '=') << std::endl;
}

/**
 * \brief Main function
 */
int
main (int argc, char *argv[])
{
  // Configure command line arguments
  uint32_t packetSize = 512;
  uint32_t numPackets = 100;
  uint16_t generationSize = 8;
  double lossRate = 0.1;
  bool verbose = false;
  
  CommandLine cmd (__FILE__);
  cmd.AddValue ("packetSize", "Size of packets to send", packetSize);
  cmd.AddValue ("numPackets", "Number of packets to send", numPackets);
  cmd.AddValue ("generationSize", "Size of coding generation", generationSize);
  cmd.AddValue ("lossRate", "Packet loss rate", lossRate);
  cmd.AddValue ("verbose", "Enable verbose logging", verbose);
  cmd.Parse (argc, argv);
  
  if (verbose)
    {
      LogComponentEnable ("NetworkCodingVsTcpComparison", LOG_LEVEL_INFO);
      LogComponentEnable ("NetworkCodingUdpApplication", LOG_LEVEL_INFO);
      LogComponentEnable ("NetworkCodingEncoder", LOG_LEVEL_INFO);
      LogComponentEnable ("NetworkCodingDecoder", LOG_LEVEL_INFO);
    }
  
  std::cout << "Network Coding vs TCP Comparison with the following parameters:" << std::endl;
  std::cout << "  Packet size: " << packetSize << " bytes" << std::endl;
  std::cout << "  Number of packets: " << numPackets << std::endl;
  std::cout << "  Generation size: " << generationSize << " packets" << std::endl;
  std::cout << "  Packet loss rate: " << (lossRate * 100) << "%" << std::endl;
  
  // Run both simulations
  SimulationResults ncResults = RunNetworkCodingSimulation(packetSize, numPackets, generationSize, lossRate);
  SimulationResults tcpResults = RunTcpSimulation(packetSize, numPackets, lossRate);
  
  // Print comparison results
  PrintComparisonResults(ncResults, tcpResults, packetSize, numPackets, generationSize, lossRate);
  
  return 0;
}