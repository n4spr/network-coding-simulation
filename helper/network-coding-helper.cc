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

#include "network-coding-helper.h"
#include "ns3/names.h"
#include "ns3/log.h"
#include "ns3/udp-socket-factory.h"
#include "ns3/inet-socket-address.h"
#include "ns3/inet6-socket-address.h"
#include "ns3/string.h"
#include "ns3/uinteger.h"
#include "ns3/double.h"
#include "ns3/pointer.h"

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("NetworkCodingHelper");

NetworkCodingHelper::NetworkCodingHelper (Address address, uint16_t port)
  : m_remoteAddress (address),
    m_isSender (false),
    m_packetSize (1024),
    m_numPackets (1000),
    m_generationSize (8),
    m_dataRate (DataRate ("1Mbps")),
    m_lossRate (0.0)
{
  NS_LOG_FUNCTION (this << address << port);
  
  m_factory.SetTypeId ("ns3::NetworkCodingUdpApplication");
  
  // Set the remote address if it's for a sender
  if (InetSocketAddress::IsMatchingType (address))
    {
      // Remove unused variable warning
      m_factory.Set ("Remote", AddressValue (address));
    }
  else if (Inet6SocketAddress::IsMatchingType (address))
    {
      // Remove unused variable warning  
      m_factory.Set ("Remote", AddressValue (address));
    }
}

NetworkCodingHelper::NetworkCodingHelper (Ipv4Address ip, uint16_t port)
  : m_remoteAddress (InetSocketAddress (ip, port)),
    m_remotePort (port),
    m_isSender (false),
    m_packetSize (1024),
    m_numPackets (1000),
    m_generationSize (8),
    m_dataRate (DataRate ("1Mbps")),
    m_lossRate (0.0)
{
  NS_LOG_FUNCTION (this << ip << port);
  
  m_factory.SetTypeId ("ns3::NetworkCodingUdpApplication");
}

NetworkCodingHelper::NetworkCodingHelper (Ipv6Address ip, uint16_t port)
  : m_remoteAddress (Inet6SocketAddress (ip, port)),
    m_remotePort (port),
    m_isSender (false),
    m_packetSize (1024),
    m_numPackets (1000),
    m_generationSize (8),
    m_dataRate (DataRate ("1Mbps")),
    m_lossRate (0.0)
{
  NS_LOG_FUNCTION (this << ip << port);
  
  m_factory.SetTypeId ("ns3::NetworkCodingUdpApplication");
}

void
NetworkCodingHelper::SetAttribute (std::string name, const AttributeValue &value)
{
  NS_LOG_FUNCTION (this << name);
  m_factory.Set (name, value);
}

ApplicationContainer
NetworkCodingHelper::Install (Ptr<Node> node) const
{
  NS_LOG_FUNCTION (this << node);
  return ApplicationContainer (InstallPriv (node));
}

ApplicationContainer
NetworkCodingHelper::Install (NodeContainer nodes) const
{
  NS_LOG_FUNCTION (this << nodes.GetN());
  ApplicationContainer apps;
  
  for (NodeContainer::Iterator i = nodes.Begin (); i != nodes.End (); ++i)
    {
      apps.Add (InstallPriv (*i));
    }
  
  return apps;
}

ApplicationContainer
NetworkCodingHelper::Install (std::string nodeName) const
{
  NS_LOG_FUNCTION (this << nodeName);
  Ptr<Node> node = Names::Find<Node> (nodeName);
  return ApplicationContainer (InstallPriv (node));
}

void
NetworkCodingHelper::ConfigureSender (uint32_t packetSize, 
                                     uint32_t numPackets, 
                                     uint16_t generationSize,
                                     DataRate dataRate)
{
  NS_LOG_FUNCTION (this << packetSize << numPackets << generationSize << dataRate);
  
  m_isSender = true;
  m_packetSize = packetSize;
  m_numPackets = numPackets;
  m_generationSize = generationSize;
  m_dataRate = dataRate;
  
  // Set up the factory attributes correctly - these must match the application's TypeId
  m_factory.Set ("PacketSize", UintegerValue (packetSize));
  m_factory.Set ("NumPackets", UintegerValue (numPackets));
  m_factory.Set ("GenerationSize", UintegerValue (generationSize));
  m_factory.Set ("DataRate", DataRateValue (dataRate));
  m_factory.Set ("LossRate", DoubleValue (m_lossRate));
  
  NS_LOG_INFO ("Configured sender: " << packetSize << " bytes, " 
               << numPackets << " packets, generation size " << generationSize);
}

void
NetworkCodingHelper::ConfigureReceiver (uint32_t packetSize,
                                       uint16_t generationSize)
{
  NS_LOG_FUNCTION (this << packetSize << generationSize);
  
  m_isSender = false;
  m_packetSize = packetSize;
  m_numPackets = 0;  // Receiver doesn't send packets
  m_generationSize = generationSize;
  
  // Set up the factory attributes
  m_factory.Set ("PacketSize", UintegerValue (packetSize));
  m_factory.Set ("NumPackets", UintegerValue (0));
  m_factory.Set ("GenerationSize", UintegerValue (generationSize));
  m_factory.Set ("LossRate", DoubleValue (0.0));  // No loss for receiver
  
  NS_LOG_INFO ("Configured receiver: " << packetSize << " bytes, generation size " << generationSize);
}

void
NetworkCodingHelper::SetLossRate (double lossRate)
{
  NS_LOG_FUNCTION (this << lossRate);
  
  NS_ASSERT_MSG (lossRate >= 0.0 && lossRate <= 1.0, "Loss rate must be between 0.0 and 1.0");
  
  m_lossRate = lossRate;
  m_factory.Set ("LossRate", DoubleValue (lossRate));
}

Ptr<Application>
NetworkCodingHelper::InstallPriv (Ptr<Node> node) const
{
  NS_LOG_FUNCTION (this << node);
  
  // Create the application using the factory
  Ptr<NetworkCodingUdpApplication> app = m_factory.Create<NetworkCodingUdpApplication> ();
  
  // Add the application to the node first
  node->AddApplication (app);
  
  // The application will be configured through the attributes set in the factory
  // No need to call Setup() since attributes are set automatically
  
  return app;
}

//----------------------------------------------------------
// NetworkCodingStatsHelper implementation
//----------------------------------------------------------

NetworkCodingStatsHelper::NetworkCodingStatsHelper ()
{
  NS_LOG_FUNCTION (this);
}

void
NetworkCodingStatsHelper::AddApplication (Ptr<NetworkCodingUdpApplication> app)
{
  NS_LOG_FUNCTION (this << app);
  m_apps.push_back (app);
}

void
NetworkCodingStatsHelper::AddApplications (ApplicationContainer apps)
{
  NS_LOG_FUNCTION (this << &apps);
  
  for (ApplicationContainer::Iterator i = apps.Begin (); i != apps.End (); ++i)
    {
      Ptr<NetworkCodingUdpApplication> app = (*i)->GetObject<NetworkCodingUdpApplication> ();
      if (app)
        {
          m_apps.push_back (app);
        }
    }
}

void
NetworkCodingStatsHelper::PrintStats (std::ostream& os) const
{
  NS_LOG_FUNCTION (this);
  
  uint32_t packetsSent = GetPacketsSent ();
  uint32_t packetsReceived = GetPacketsReceived ();
  uint32_t innovativePacketsReceived = GetInnovativePacketsReceived ();
  uint32_t generationsDecoded = GetGenerationsDecoded ();
  double codingEfficiency = GetCodingEfficiency ();
  double decodingRate = GetDecodingRate ();
  
  os << "Network Coding Statistics:" << std::endl;
  os << "  Packets sent: " << packetsSent << std::endl;
  os << "  Packets received: " << packetsReceived << std::endl;
  os << "  Innovative packets received: " << innovativePacketsReceived << std::endl;
  os << "  Generations decoded: " << generationsDecoded << std::endl;
  os << "  Coding efficiency: " << codingEfficiency * 100.0 << "%" << std::endl;
  os << "  Decoding rate: " << decodingRate * 100.0 << "%" << std::endl;
}

uint32_t
NetworkCodingStatsHelper::GetPacketsSent (void) const
{
  uint32_t packetsSent = 0;
  
  for (auto app : m_apps)
    {
      packetsSent += app->GetPacketsSent ();
    }
  
  return packetsSent;
}

uint32_t
NetworkCodingStatsHelper::GetPacketsReceived (void) const
{
  uint32_t packetsReceived = 0;
  
  for (auto app : m_apps)
    {
      packetsReceived += app->GetPacketsReceived ();
    }
  
  return packetsReceived;
}

uint32_t
NetworkCodingStatsHelper::GetInnovativePacketsReceived (void) const
{
  uint32_t innovativePacketsReceived = 0;
  
  for (auto app : m_apps)
    {
      innovativePacketsReceived += app->GetInnovativePacketsReceived ();
    }
  
  return innovativePacketsReceived;
}

uint32_t
NetworkCodingStatsHelper::GetGenerationsDecoded (void) const
{
  uint32_t generationsDecoded = 0;
  
  for (auto app : m_apps)
    {
      generationsDecoded += app->GetGenerationsDecoded ();
    }
  
  return generationsDecoded;
}

double
NetworkCodingStatsHelper::GetCodingEfficiency (void) const
{
  uint32_t packetsReceived = GetPacketsReceived ();
  
  if (packetsReceived == 0)
    {
      return 0.0;
    }
  
  return static_cast<double> (GetInnovativePacketsReceived ()) / packetsReceived;
}

double
NetworkCodingStatsHelper::GetDecodingRate (void) const
{
  uint32_t totalGenerations = 0;
  
  for (auto app : m_apps)
    {
      // Fix: Create UintegerValue and properly get the attribute
      UintegerValue generationSizeValue;
      app->GetAttribute("GenerationSize", generationSizeValue);
      uint16_t generationSize = generationSizeValue.Get();
      
      if (generationSize > 0)
        {
          totalGenerations += app->GetPacketsSent() / generationSize;
        }
    }
  
  if (totalGenerations == 0)
    {
      return 0.0;
    }
  
  return static_cast<double> (GetGenerationsDecoded ()) / totalGenerations;
}

} // namespace ns3
