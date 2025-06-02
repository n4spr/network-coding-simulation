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

#ifndef NETWORK_CODING_HELPER_H
#define NETWORK_CODING_HELPER_H

#include "ns3/application-container.h"
#include "ns3/node-container.h"
#include "ns3/object-factory.h"
#include "ns3/data-rate.h"
#include "ns3/ipv4-address.h"
#include "ns3/ipv6-address.h"
#include "../model/network-coding-udp-application.h"

namespace ns3 {

/**
 * \ingroup network-coding
 * \brief Helper to make it easier to create and configure network coding applications
 *
 * This class helps to create and configure NetworkCodingUdpApplication
 * instances. It can install applications on multiple nodes at once and
 * configure various parameters easily.
 */
class NetworkCodingHelper
{
public:
  /**
   * \brief Create a new NetworkCodingHelper
   * \param address Address of the remote host
   * \param port Remote port
   */
  NetworkCodingHelper (Address address, uint16_t port);
  
  /**
   * \brief Create a new NetworkCodingHelper with IPv4 address
   * \param ip IPv4 address of the remote host
   * \param port Remote port
   */
  NetworkCodingHelper (Ipv4Address ip, uint16_t port);
  
  /**
   * \brief Create a new NetworkCodingHelper with IPv6 address
   * \param ip IPv6 address of the remote host
   * \param port Remote port
   */
  NetworkCodingHelper (Ipv6Address ip, uint16_t port);

  /**
   * \brief Set a parameter for the applications to be created
   * \param name Name of the attribute to set
   * \param value Value to set the attribute to
   */
  void SetAttribute (std::string name, const AttributeValue &value);

  /**
   * \brief Install network coding applications on a node
   * \param node The node on which to install the application
   * \return Container with the created application
   */
  ApplicationContainer Install (Ptr<Node> node) const;
  
  /**
   * \brief Install network coding applications on a set of nodes
   * \param nodes The nodes on which to install the applications
   * \return Container with the created applications
   */
  ApplicationContainer Install (NodeContainer nodes) const;
  
  /**
   * \brief Install network coding applications on a node specified by name
   * \param nodeName Name of the node on which to install the application
   * \return Container with the created application
   */
  ApplicationContainer Install (std::string nodeName) const;
  
  /**
   * \brief Configure encoder-only mode (sender)
   * \param packetSize Size of packets to send
   * \param numPackets Number of packets to send
   * \param generationSize Size of each generation
   * \param dataRate Rate at which to send data
   */
  void ConfigureSender (uint32_t packetSize, 
                        uint32_t numPackets, 
                        uint16_t generationSize,
                        DataRate dataRate);
  
  /**
   * \brief Configure decoder-only mode (receiver)
   * \param packetSize Size of packets to receive
   * \param generationSize Size of each generation
   */
  void ConfigureReceiver (uint32_t packetSize,
                          uint16_t generationSize);
  
  /**
   * \brief Set the simulated packet loss rate
   * \param lossRate Packet loss rate (0.0 to 1.0)
   */
  void SetLossRate (double lossRate);

private:
  /**
   * \brief Install a network coding application on the specified node
   * \param node The node on which to install the application
   * \return Created application
   */
  Ptr<Application> InstallPriv (Ptr<Node> node) const;
  
  ObjectFactory m_factory;          //!< Factory for creating applications
  Address m_remoteAddress;          //!< Address of the remote host
  uint16_t m_remotePort;            //!< Remote port
  bool m_isSender;                  //!< Flag indicating if this is a sender
  uint32_t m_packetSize;            //!< Size of packets in bytes
  uint32_t m_numPackets;            //!< Number of packets to send
  uint16_t m_generationSize;        //!< Size of each generation
  DataRate m_dataRate;              //!< Rate at which to send data
  double m_lossRate;                //!< Packet loss rate to simulate
};

/**
 * \ingroup network-coding
 * \brief Helper to make it easier to gather statistics from network coding applications
 */
class NetworkCodingStatsHelper
{
public:
  /**
   * \brief Create a new NetworkCodingStatsHelper
   */
  NetworkCodingStatsHelper ();
  
  /**
   * \brief Collect statistics from a single application
   * \param app The application to collect statistics from
   */
  void AddApplication (Ptr<NetworkCodingUdpApplication> app);
  
  /**
   * \brief Collect statistics from multiple applications
   * \param apps The container of applications to collect statistics from
   */
  void AddApplications (ApplicationContainer apps);
  
  /**
   * \brief Print the collected statistics
   * \param os The output stream to print to
   */
  void PrintStats (std::ostream& os) const;
  
  /**
   * \brief Get the number of packets sent
   * \return number of packets sent
   */
  uint32_t GetPacketsSent (void) const;
  
  /**
   * \brief Get the number of packets received
   * \return number of packets received
   */
  uint32_t GetPacketsReceived (void) const;
  
  /**
   * \brief Get the number of innovative packets received
   * \return number of innovative packets received
   */
  uint32_t GetInnovativePacketsReceived (void) const;
  
  /**
   * \brief Get the number of generations decoded
   * \return number of generations decoded
   */
  uint32_t GetGenerationsDecoded (void) const;
  
  /**
   * \brief Get the coding efficiency
   * \return coding efficiency (innovative / total received)
   */
  double GetCodingEfficiency (void) const;
  
  /**
   * \brief Get the decoding rate
   * \return decoding rate (generations decoded / total generations)
   */
  double GetDecodingRate (void) const;

private:
  std::vector<Ptr<NetworkCodingUdpApplication>> m_apps;  //!< Applications to collect statistics from
};

} // namespace ns3

#endif /* NETWORK_CODING_HELPER_H */
