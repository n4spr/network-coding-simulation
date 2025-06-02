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
#include "ns3/gnuplot.h"
#include "../helper/network-coding-helper.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("NetworkCodingComparisonExample");

/**
 * \brief Network Coding Comparison Example
 *
 * This example compares plain TCP with network coding over TCP
 * for different packet loss rates. It measures throughput and
 * packet loss, and generates plots of the results.
 */
int
main (int argc, char *argv[])
{
  // Configure log components
  LogComponentEnable ("NetworkCodingComparisonExample", LOG_LEVEL_INFO);
  
  // Configure command line arguments
  uint32_t packetSize = 1024;
  uint32_t numPackets = 1000;
  uint16_t generationSize = 8;
  std::string rateList = "0.01,0.05,0.1,0.15,0.2,0.25";  // Comma-separated list of loss rates
  bool enablePlots = true;
  
  CommandLine cmd (__FILE__);
  cmd.AddValue ("packetSize", "Size of packets to send", packetSize);
  cmd.AddValue ("numPackets", "Number of packets to send", numPackets);
  cmd.AddValue ("generationSize", "Size of coding generation", generationSize);
  cmd.AddValue ("rateList", "Comma-separated list of loss rates to test", rateList);
  cmd.AddValue ("enablePlots", "Enable GnuPlot output", enablePlots);
  cmd.Parse (argc, argv);
  
  // Parse the loss rate list
  std::vector<double> lossRates;
  std::stringstream ss (rateList);
  std::string token;
  
  while (std::getline (ss, token, ','))
    {
      double rate = std::stod (token);
      if (rate >= 0.0 && rate <= 1.0)
        {
          lossRates.push_back (rate);
        }
      else
        {
          NS_LOG_ERROR ("Invalid loss rate: " << rate << ". Rate must be between 0.0 and 1.0");
        }
    }
  
  // Data structures to store results
  std::vector<double> ncThroughput;
  std::vector<double> tcpThroughput;
  std::vector<double> ncLoss;
  std::vector<double> tcpLoss;
  
  // Run simulation for each loss rate
  for (double lossRate : lossRates)
    {
      NS_LOG_INFO ("Running simulation with loss rate: " << lossRate);
      
      // Create nodes
      NodeContainer nodes;
      nodes.Create (4);  // Two pairs of nodes: one for NC, one for plain TCP
      
      NodeContainer ncNodes;
      ncNodes.Add (nodes.Get (0));
      ncNodes.Add (nodes.Get (1));
      
      NodeContainer tcpNodes;
      tcpNodes.Add (nodes.Get (2));
      tcpNodes.Add (nodes.Get (3));
      
      // Install point-to-point links
      PointToPointHelper pointToPoint;
      pointToPoint.SetDeviceAttribute ("DataRate", StringValue ("5Mbps"));
      pointToPoint.SetChannelAttribute ("Delay", StringValue ("2ms"));
      
      // Set up error model for packet loss
      Ptr<RateErrorModel> ncErrorModel = CreateObject<RateErrorModel> ();
      ncErrorModel->SetAttribute ("ErrorRate", DoubleValue (lossRate));
      ncErrorModel->SetAttribute ("ErrorUnit", StringValue ("ERROR_UNIT_PACKET"));
      
      Ptr<RateErrorModel> tcpErrorModel = CreateObject<RateErrorModel> ();
      tcpErrorModel->SetAttribute ("ErrorRate", DoubleValue (lossRate));
      tcpErrorModel->SetAttribute ("ErrorUnit", StringValue ("ERROR_UNIT_PACKET"));
      
      // Install network devices
      NetDeviceContainer ncDevices = pointToPoint.Install (ncNodes);
      NetDeviceContainer tcpDevices = pointToPoint.Install (tcpNodes);
      
      // Install error models on the receiving devices
      ncDevices.Get (1)->SetAttribute ("ReceiveErrorModel", PointerValue (ncErrorModel));
      tcpDevices.Get (1)->SetAttribute ("ReceiveErrorModel", PointerValue (tcpErrorModel));
      
      // Install internet stack
      InternetStackHelper internet;
      internet.Install (nodes);
      
      // Configure IP addresses
      Ipv4AddressHelper ipv4;
      
      ipv4.SetBase ("10.1.1.0", "255.255.255.0");
      Ipv4InterfaceContainer ncInterfaces = ipv4.Assign (ncDevices);
      
      ipv4.SetBase ("10.1.2.0", "255.255.255.0");
      Ipv4InterfaceContainer tcpInterfaces = ipv4.Assign (tcpDevices);
      
      // Set up network coding applications
      uint16_t port = 9;
      
      NetworkCodingHelper ncHelper (ncInterfaces.GetAddress (1), port);
      ncHelper.ConfigureSender (packetSize, numPackets, generationSize, DataRate ("1Mbps"));
      ncHelper.SetLossRate (lossRate);
      ApplicationContainer ncSenderApp = ncHelper.Install (ncNodes.Get (0));
      
      ncHelper.ConfigureReceiver (packetSize, generationSize);
      ApplicationContainer ncReceiverApp = ncHelper.Install (ncNodes.Get (1));
      
      // Set up regular TCP applications
      BulkSendHelper tcpSender ("ns3::TcpSocketFactory", InetSocketAddress (tcpInterfaces.GetAddress (1), port));
      tcpSender.SetAttribute ("MaxBytes", UintegerValue (packetSize * numPackets));
      tcpSender.SetAttribute ("SendSize", UintegerValue (packetSize));
      ApplicationContainer tcpSenderApp = tcpSender.Install (tcpNodes.Get (0));
      
      PacketSinkHelper tcpSink ("ns3::TcpSocketFactory", InetSocketAddress (Ipv4Address::GetAny (), port));
      ApplicationContainer tcpReceiverApp = tcpSink.Install (tcpNodes.Get (1));
      
      // Schedule applications
      ncSenderApp.Start (Seconds (1.0));
      ncSenderApp.Stop (Seconds (20.0));
      ncReceiverApp.Start (Seconds (0.5));
      ncReceiverApp.Stop (Seconds (20.0));
      
      tcpSenderApp.Start (Seconds (1.0));
      tcpSenderApp.Stop (Seconds (20.0));
      tcpReceiverApp.Start (Seconds (0.5));
      tcpReceiverApp.Stop (Seconds (20.0));
      
      // Set up flow monitor
      FlowMonitorHelper flowHelper;
      Ptr<FlowMonitor> flowMonitor = flowHelper.InstallAll ();
      
      // Run simulation
      Simulator::Stop (Seconds (20.0));
      Simulator::Run ();
      
      // Collect statistics
      flowMonitor->CheckForLostPackets ();
      Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier> (flowHelper.GetClassifier ());
      std::map<FlowId, FlowMonitor::FlowStats> stats = flowMonitor->GetFlowStats ();
      
      double ncThput = 0.0;
      double tcpThput = 0.0;
      double ncPktLoss = 0.0;
      double tcpPktLoss = 0.0;
      
      for (std::map<FlowId, FlowMonitor::FlowStats>::const_iterator i = stats.begin (); i != stats.end (); ++i)
        {
          Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow (i->first);
          
          if (t.destinationAddress == ncInterfaces.GetAddress (1))
            {
              // Network coding flow
              ncThput = i->second.rxBytes * 8.0 / (i->second.timeLastRxPacket.GetSeconds () - i->second.timeFirstTxPacket.GetSeconds ()) / 1000000.0;
              ncPktLoss = 100.0 * (i->second.txPackets - i->second.rxPackets) / static_cast<double> (i->second.txPackets);
              
              NS_LOG_INFO ("Network Coding:");
              NS_LOG_INFO ("  Throughput: " << ncThput << " Mbps");
              NS_LOG_INFO ("  Packet Loss: " << ncPktLoss << "%");
            }
          else if (t.destinationAddress == tcpInterfaces.GetAddress (1))
            {
              // Plain TCP flow
              tcpThput = i->second.rxBytes * 8.0 / (i->second.timeLastRxPacket.GetSeconds () - i->second.timeFirstTxPacket.GetSeconds ()) / 1000000.0;
              tcpPktLoss = 100.0 * (i->second.txPackets - i->second.rxPackets) / static_cast<double> (i->second.txPackets);
              
              NS_LOG_INFO ("Plain TCP:");
              NS_LOG_INFO ("  Throughput: " << tcpThput << " Mbps");
              NS_LOG_INFO ("  Packet Loss: " << tcpPktLoss << "%");
            }
        }
      
      // Store results
      ncThroughput.push_back (ncThput);
      tcpThroughput.push_back (tcpThput);
      ncLoss.push_back (ncPktLoss);
      tcpLoss.push_back (tcpPktLoss);
      
      // Clean up
      Simulator::Destroy ();
    }
  
  // Print summary
  std::cout << std::endl << "Simulation Results:" << std::endl;
  std::cout << "Loss Rate\tNC Throughput\tTCP Throughput\tNC Loss\tTCP Loss" << std::endl;
  
  for (size_t i = 0; i < lossRates.size (); i++)
    {
      std::cout << lossRates[i] << "\t" 
                << ncThroughput[i] << "\t" 
                << tcpThroughput[i] << "\t" 
                << ncLoss[i] << "\t" 
                << tcpLoss[i] << std::endl;
    }
  
  // Generate plots
  if (enablePlots)
    {
      std::string fileNamePrefix = "network-coding-comparison";
      
      // Throughput plot
      Gnuplot throughputPlot (fileNamePrefix + "-throughput.png");
      throughputPlot.SetTitle ("Throughput vs. Packet Loss Rate");
      throughputPlot.SetTerminal ("png");
      throughputPlot.SetLegend ("Packet Loss Rate", "Throughput (Mbps)");
      
      Gnuplot2dDataset ncThroughputDataset;
      ncThroughputDataset.SetTitle ("Network Coding");
      ncThroughputDataset.SetStyle (Gnuplot2dDataset::LINES_POINTS);
      
      Gnuplot2dDataset tcpThroughputDataset;
      tcpThroughputDataset.SetTitle ("Plain TCP");
      tcpThroughputDataset.SetStyle (Gnuplot2dDataset::LINES_POINTS);
      
      for (size_t i = 0; i < lossRates.size (); i++)
        {
          ncThroughputDataset.Add (lossRates[i], ncThroughput[i]);
          tcpThroughputDataset.Add (lossRates[i], tcpThroughput[i]);
        }
      
      throughputPlot.AddDataset (ncThroughputDataset);
      throughputPlot.AddDataset (tcpThroughputDataset);
      
      std::ofstream throughputOutfile (fileNamePrefix + "-throughput.plt");
      throughputPlot.GenerateOutput (throughputOutfile);
      throughputOutfile.close ();
      
      // Packet loss plot
      Gnuplot lossPlot (fileNamePrefix + "-loss.png");
      lossPlot.SetTitle ("Effective Packet Loss vs. Channel Loss Rate");
      lossPlot.SetTerminal ("png");
      lossPlot.SetLegend ("Channel Loss Rate", "Effective Loss (%)");
      
      Gnuplot2dDataset ncLossDataset;
      ncLossDataset.SetTitle ("Network Coding");
      ncLossDataset.SetStyle (Gnuplot2dDataset::LINES_POINTS);
      
      Gnuplot2dDataset tcpLossDataset;
      tcpLossDataset.SetTitle ("Plain TCP");
      tcpLossDataset.SetStyle (Gnuplot2dDataset::LINES_POINTS);
      
      for (size_t i = 0; i < lossRates.size (); i++)
        {
          ncLossDataset.Add (lossRates[i], ncLoss[i]);
          tcpLossDataset.Add (lossRates[i], tcpLoss[i]);
        }
      
      lossPlot.AddDataset (ncLossDataset);
      lossPlot.AddDataset (tcpLossDataset);
      
      std::ofstream lossOutfile (fileNamePrefix + "-loss.plt");
      lossPlot.GenerateOutput (lossOutfile);
      lossOutfile.close ();
      
      // Generate plot files
      int ret1 = system ((std::string ("gnuplot ") + fileNamePrefix + "-throughput.plt").c_str ());
      int ret2 = system ((std::string ("gnuplot ") + fileNamePrefix + "-loss.plt").c_str ());
      if (ret1 == -1 || ret2 == -1) {
        NS_LOG_ERROR("Failed to execute gnuplot command");
      }
    }
  
  return 0;
}