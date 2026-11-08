/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/point-to-point-net-device.h"
#include "ns3/applications-module.h"
#include "ns3/tcp-socket-factory.h"
#include "ns3/socket.h"
#include "ns3/callback.h"
#include "ns3/olsr-helper.h"
#include "ns3/olsr-routing-protocol.h"
#include "ns3/node-list.h"
#include "ns3/ipv4-static-routing-helper.h"
#include "ns3/ipv4-list-routing-helper.h"
#include "ns3/ipv4-route.h"
#include "ns3/ipv4-routing-protocol.h"
#include "ns3/ipv4-header.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/ipv4-flow-classifier.h"
#include "ns3/trace-helper.h"
#include "ns3/ipv6-address.h"
#include "../model/network-coding-packet.h"
#include <boost/graph/adjacency_list.hpp>
#include <boost/graph/graph_traits.hpp>
#include <boost/graph/subgraph.hpp>
#include <boost/graph/copy.hpp>
#include <boost/graph/vf2_sub_graph_iso.hpp>
#include <iostream>
#include <fstream>
#include <string>
#include <cassert>
#include <random>
#include <map>
#include <vector>
#include <set>
#include <queue>
#include <limits>
#include <algorithm>
#include <iomanip>
#include <sstream>
#include <cstring>
#include <unordered_map>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("RandomGraphButterflyNC");

static bool g_logRoutingTables = false;
static std::set<std::pair<uint32_t, uint32_t>> g_butterflySourceDestEdges;
static std::map<uint64_t, std::vector<uint32_t>> g_packetFlowPaths;
static const uint32_t kInvalidNodeId = std::numeric_limits<uint32_t>::max();
static const uint16_t kNcAppPort = 9999;

uint32_t ResolveNodeIdFromIpv4(const Ipv4Address& addr)
{
    if (addr == Ipv4Address("0.0.0.0") || addr == Ipv4Address::GetAny()) {
        return kInvalidNodeId;
    }

    for (uint32_t i = 0; i < NodeList::GetNNodes(); ++i) {
        Ptr<Node> node = NodeList::GetNode(i);
        Ptr<Ipv4> ipv4 = node->GetObject<Ipv4>();
        if (!ipv4) {
            continue;
        }
        for (uint32_t iface = 0; iface < ipv4->GetNInterfaces(); ++iface) {
            for (uint32_t addrIndex = 0; addrIndex < ipv4->GetNAddresses(iface); ++addrIndex) {
                Ipv4InterfaceAddress ifAddr = ipv4->GetAddress(iface, addrIndex);
                if (ifAddr.GetLocal() == addr) {
                    return node->GetId();
                }
            }
        }
    }

    return kInvalidNodeId;
}

void TrackPacketFlowHop(uint32_t nodeId, Ptr<Packet> packet)
{
    if (!packet) {
        return;
    }

    uint64_t uid = packet->GetUid();
    auto& path = g_packetFlowPaths[uid];
    if (path.empty() || path.back() != nodeId) {
        path.push_back(nodeId);
    }
}

std::string DescribePacketFlow(uint64_t packetUid)
{
    auto it = g_packetFlowPaths.find(packetUid);
    if (it == g_packetFlowPaths.end() || it->second.empty()) {
        return "unknown";
    }

    std::ostringstream oss;
    for (size_t i = 0; i < it->second.size(); ++i) {
        if (i > 0) {
            oss << " -> ";
        }
        oss << "Node " << it->second[i];
    }
    return oss.str();
}

// BGL graph type definitions with vertex properties for butterfly detection
typedef boost::property<boost::vertex_name_t, uint32_t> VertexProperty;
typedef boost::adjacency_list<boost::vecS, boost::vecS, boost::undirectedS, VertexProperty> Graph;
typedef boost::adjacency_list<boost::vecS, boost::vecS, boost::undirectedS, VertexProperty> ButterflyPattern;
typedef boost::graph_traits<Graph>::vertex_descriptor Vertex;
typedef boost::graph_traits<Graph>::edge_descriptor Edge;
typedef boost::graph_traits<Graph>::vertex_iterator VertexIterator;
typedef boost::graph_traits<Graph>::edge_iterator EdgeIterator;
typedef boost::graph_traits<Graph>::adjacency_iterator AdjacencyIterator;

struct ButterflyInfo {  
    uint32_t source;
    uint32_t r1, r2;
    uint32_t r3, r4;
    uint32_t d1, d2;
};

void RegisterButterflyPatterns(uint32_t ownerNodeId, const std::vector<ButterflyInfo>& butterflies);
void ResetButterflyVisualizationState();
void WriteCombinedButterflyGraph(uint32_t numNodes, uint32_t graphSeed);

struct SimulationParameters {
    uint32_t minNodes = 5;
    uint32_t maxNodes = 50;
    double edgeProbability = 0.3;
    uint32_t packetSize = 1024;
    uint16_t generationSize = 2;
    uint32_t totalPackets = 2;
    double simulationTime = 60.0;  // Increased simulation time
    uint32_t randomSeed = 42;
    std::string outputPrefix = "random-graph";
    bool verbose = false;
    uint32_t numGraphsPerSize = 3;
    double dataRate = 10.0; // Mbps
    double linkDelay = 2.0; // ms
};

struct NetworkStats {
    std::string method;
    uint32_t nodeCount = 0;
    uint32_t edgeCount = 0;
    uint64_t totalTransmissions = 0;
    uint32_t successfulDecodings = 0;
    double totalTime = 0;
    double packetLossRate = 0;
    double averageDelay = 0;
    double throughput = 0;
    double goodput = 0;
    double successRate = 0;
    uint32_t bottleneckUsage = 0;
    uint32_t butterflyUsageCount = 0;
    uint32_t ncTransmissions = 0;
    uint32_t butterflyXorStarts = 0;
    
    NetworkStats(std::string m) : method(m) {}
};

// Forward declarations
void CreateGraphVisualization(const std::vector<std::pair<uint32_t, uint32_t>>& edges, 
                             uint32_t numNodes, const std::string& filename);
bool IsReachable(uint32_t source, uint32_t dest,
                const std::vector<std::pair<uint32_t, uint32_t>>& edges,
                uint32_t numNodes);
uint32_t SelectReachableNode(uint32_t source,
                             const std::vector<std::pair<uint32_t, uint32_t>>& edges,
                             uint32_t numNodes, uint32_t seed);

class ButterflyDetector
{
public:
    ButterflyDetector(uint32_t nodeId) : m_nodeId(nodeId) {}

    void UpdateTopology(const std::map<uint32_t, std::set<uint32_t>>& neighbors)
    {
        m_neighbors = neighbors;
        m_butterflies.clear();
        DetectButterflies();
    }

    void UpdateTopologyFromOLSR(Ptr<Node> node)
    {
        Ptr<Ipv4> ipv4 = node->GetObject<Ipv4>();
        if (!ipv4) return;
        
        Ptr<Ipv4RoutingProtocol> routing = ipv4->GetRoutingProtocol();
        if (!routing) return;
        
        // Handle list routing protocol (OLSR + static)
        Ptr<Ipv4ListRouting> listRouting = DynamicCast<Ipv4ListRouting>(routing);
        if (listRouting) {
            // Find OLSR in the list
            for (uint32_t i = 0; i < listRouting->GetNRoutingProtocols(); i++) {
                int16_t priority;
                Ptr<Ipv4RoutingProtocol> proto = listRouting->GetRoutingProtocol(i, priority);
                Ptr<olsr::RoutingProtocol> olsrRouting = DynamicCast<olsr::RoutingProtocol>(proto);
                
                if (olsrRouting) {
                    // Access neighbor and topology tables
                    BuildNeighborMapFromOLSR(node, olsrRouting);
                    DetectButterflies();
                    return;
                }
            }
        } else {
            // Try direct cast
            Ptr<olsr::RoutingProtocol> olsrRouting = DynamicCast<olsr::RoutingProtocol>(routing);
            if (olsrRouting) {
                BuildNeighborMapFromOLSR(node, olsrRouting);
                DetectButterflies();
                return;
            }
        }
    }

    const std::vector<ButterflyInfo>& GetButterflies() const
    {
        return m_butterflies;
    }

    bool IsSourceOfButterfly() const
    {
        return !m_butterflies.empty();
    }

private:
    uint32_t m_nodeId;
    std::map<uint32_t, std::set<uint32_t>> m_neighbors;
    std::vector<ButterflyInfo> m_butterflies;

    void DetectButterflies()
    {
        m_butterflies.clear();
        
        auto neighborhood = BuildTwoHopNeighborhood();
        std::cout << "  Butterfly Detection: analyzing " << neighborhood.size() 
                  << " nodes reachable within 2 hops of Node " << m_nodeId << std::endl;

        if (neighborhood.size() < 7) {
            std::cout << "  Neighborhood too small for butterfly patterns (need ≥7 vertices, have "
                      << neighborhood.size() << ")" << std::endl;
            return;
        }

        // Build the induced subgraph for this neighborhood
        Graph networkGraph;
        std::map<uint32_t, Vertex> nodeToVertex;
        for (uint32_t nodeId : neighborhood) {
            Vertex v = boost::add_vertex(networkGraph);
            boost::put(boost::vertex_name, networkGraph, v, nodeId);
            nodeToVertex[nodeId] = v;
        }

        uint32_t edgesAdded = 0;
        for (uint32_t nodeId : neighborhood) {
            auto it = m_neighbors.find(nodeId);
            if (it == m_neighbors.end()) {
                continue;
            }

            for (uint32_t neighborId : it->second) {
                if (nodeId >= neighborId) {
                    continue;  // Undirected graph - only add once
                }
                if (neighborhood.count(neighborId) == 0) {
                    continue;  // Limit to 2-hop neighborhood
                }

                boost::add_edge(nodeToVertex[nodeId], nodeToVertex[neighborId], networkGraph);
                edgesAdded++;
            }
        }

        std::cout << "  Built 2-hop subgraph with " << boost::num_vertices(networkGraph)
                  << " vertices and " << edgesAdded << " edges" << std::endl;

        if (boost::num_edges(networkGraph) < 8) {
            std::cout << "  Neighborhood too sparse for butterfly patterns (need ≥8 edges, have "
                      << boost::num_edges(networkGraph) << ")" << std::endl;
            return;
        }

        Graph butterflyPattern = CreateButterflyPattern();
        FindButterflySubgraphs(networkGraph, butterflyPattern);

        std::cout << "  Butterfly Detection Result: found " << m_butterflies.size()
                  << " butterfly patterns" << std::endl;
    }
    
    Graph CreateButterflyPattern()
    {
        Graph pattern;
        
        // Create vertices with labels corresponding to butterfly roles
        std::map<std::string, Vertex> roleToVertex;
        std::vector<std::string> roles = {"s", "r1", "r2", "r3", "r4", "d1", "d2"};
        
        std::cout << "  Creating canonical butterfly pattern with 7 nodes..." << std::endl;
        for (size_t i = 0; i < roles.size(); i++) {
            Vertex v = boost::add_vertex(pattern);
            boost::put(boost::vertex_name, pattern, v, i);  // Use index as identifier
            roleToVertex[roles[i]] = v;
        }
        
        // Add edges according to butterfly topology:
        // s connects to r1, r2
        boost::add_edge(roleToVertex["s"], roleToVertex["r1"], pattern);
        boost::add_edge(roleToVertex["s"], roleToVertex["r2"], pattern);
        
        // r1 connects to r3, d1
        boost::add_edge(roleToVertex["r1"], roleToVertex["r3"], pattern);
        boost::add_edge(roleToVertex["r1"], roleToVertex["d1"], pattern);
        
        // r2 connects to r3, d2
        boost::add_edge(roleToVertex["r2"], roleToVertex["r3"], pattern);
        boost::add_edge(roleToVertex["r2"], roleToVertex["d2"], pattern);
        
        // r3 connects to r4
        boost::add_edge(roleToVertex["r3"], roleToVertex["r4"], pattern);
        
        // r4 connects to d1, d2
        boost::add_edge(roleToVertex["r4"], roleToVertex["d1"], pattern);
        boost::add_edge(roleToVertex["r4"], roleToVertex["d2"], pattern);
        
        std::cout << "  Created butterfly pattern with " << boost::num_vertices(pattern) 
                 << " vertices and " << boost::num_edges(pattern) << " edges" << std::endl;
        
        return pattern;
    }
    
    void FindButterflySubgraphs(const Graph& networkGraph, const Graph& pattern)
    {
        std::set<std::tuple<uint32_t, std::pair<uint32_t, uint32_t>, uint32_t, uint32_t, std::pair<uint32_t, uint32_t>>> seenSignatures;

        struct SourceVertexFilter
        {
            const Graph& patternGraph;
            const Graph& networkGraph;
            uint32_t sourceId;

            bool operator()(Vertex patternVertex, Vertex networkVertex) const
            {
                uint32_t patternIndex = boost::get(boost::vertex_name, patternGraph, patternVertex);
                if (patternIndex == 0)
                {
                    uint32_t networkNodeId = boost::get(boost::vertex_name, networkGraph, networkVertex);
                    return networkNodeId == sourceId;
                }
                return true;
            }
        };

        struct AlwaysTrueEdge
        {
            bool operator()(Edge, Edge) const
            {
                return true;
            }
        };
        
        // Callback function for each found isomorphism
        auto callback = [&](auto correspondence_map_1_to_2, auto correspondence_map_2_to_1) {
            // Extract node mappings from pattern to network
            std::map<uint32_t, uint32_t> patternToNetwork;  // pattern vertex index -> network node ID
            
            VertexIterator vi, vi_end;
            for (boost::tie(vi, vi_end) = boost::vertices(pattern); vi != vi_end; ++vi) {
                Vertex patternVertex = *vi;
                Vertex networkVertex = boost::get(correspondence_map_1_to_2, patternVertex);
                
                uint32_t patternIndex = boost::get(boost::vertex_name, pattern, patternVertex);
                uint32_t networkNodeId = boost::get(boost::vertex_name, networkGraph, networkVertex);
                
                patternToNetwork[patternIndex] = networkNodeId;
            }
            
            // Map indices to butterfly roles (same order as CreateButterflyPattern)
            uint32_t s = patternToNetwork[0];   // "s"
            uint32_t r1 = patternToNetwork[1];  // "r1" 
            uint32_t r2 = patternToNetwork[2];  // "r2"
            uint32_t r3 = patternToNetwork[3];  // "r3"
            uint32_t r4 = patternToNetwork[4];  // "r4"
            uint32_t d1 = patternToNetwork[5];  // "d1"
            uint32_t d2 = patternToNetwork[6];  // "d2"
            
            // Create canonical signature to avoid duplicates
            // Canonicalize (r1,r2) and (d1,d2) ordering, keep s,r3,r4 fixed
            std::pair<uint32_t, uint32_t> canonicalR12 = std::make_pair(std::min(r1, r2), std::max(r1, r2));
            std::pair<uint32_t, uint32_t> canonicalD12 = std::make_pair(std::min(d1, d2), std::max(d1, d2));
            
            auto signature = std::make_tuple(s, canonicalR12, r3, r4, canonicalD12);
            
            if (seenSignatures.find(signature) == seenSignatures.end()) {
                seenSignatures.insert(signature);
                
                // Only accept butterflies where current node is the source
                if (s == m_nodeId) {
                    // Create butterfly info
                    ButterflyInfo butterfly;
                    butterfly.source = s;
                    butterfly.r1 = canonicalR12.first;
                    butterfly.r2 = canonicalR12.second; 
                    butterfly.r3 = r3;
                    butterfly.r4 = r4;
                    butterfly.d1 = canonicalD12.first;
                    butterfly.d2 = canonicalD12.second;
                    
                    m_butterflies.push_back(butterfly);
                    
                    std::cout << "    Found REAL butterfly with Node " << m_nodeId << " as source: s=" << s 
                             << " r1=" << butterfly.r1 << " r2=" << butterfly.r2
                             << " r3=" << r3 << " r4=" << r4 
                             << " d1=" << butterfly.d1 << " d2=" << butterfly.d2 << std::endl;
                }
            }
            
            return true;  // Continue searching for more
        };

        SourceVertexFilter vertexComp{pattern, networkGraph, m_nodeId};
        AlwaysTrueEdge edgeComp;

        auto params = boost::vertices_equivalent(vertexComp).edges_equivalent(edgeComp);
        
        // Use VF2 subgraph monomorphism (non-induced) like NetworkX's subgraph_monomorphisms_iter
        bool found = boost::vf2_subgraph_mono(pattern, networkGraph, callback,
                              boost::vertex_order_by_mult(pattern),
                              params);
        
        if (!found) {
            std::cout << "    No butterfly patterns found in this network topology" << std::endl;
        }
    }

    void SearchButterflyPatterns(const Graph& g, const std::map<uint32_t, Vertex>& nodeToVertex)
    {
        if (m_neighbors.count(m_nodeId) == 0) return;
        
        auto sourceNeighbors = m_neighbors[m_nodeId];
        if (sourceNeighbors.size() < 2) return;

        // Try all pairs of neighbors as r1, r2
        for (auto it1 = sourceNeighbors.begin(); it1 != sourceNeighbors.end(); ++it1) {
            for (auto it2 = std::next(it1); it2 != sourceNeighbors.end(); ++it2) {
                uint32_t r1 = *it1;
                uint32_t r2 = *it2;
                
                // Find common neighbor r3 of r1 and r2
                if (m_neighbors.count(r1) && m_neighbors.count(r2)) {
                    std::set<uint32_t> commonNeighs;
                    std::set_intersection(
                        m_neighbors[r1].begin(), m_neighbors[r1].end(),
                        m_neighbors[r2].begin(), m_neighbors[r2].end(),
                        std::inserter(commonNeighs, commonNeighs.begin())
                    );
                    
                    for (uint32_t r3 : commonNeighs) {
                        if (r3 == m_nodeId || r3 == r1 || r3 == r2) continue;
                        
                        // Find r4 neighbor of r3
                        if (m_neighbors.count(r3)) {
                            for (uint32_t r4 : m_neighbors[r3]) {
                                if (r4 == m_nodeId || r4 == r1 || r4 == r2 || r4 == r3) continue;
                                
                                // Find d1, d2 that complete butterfly
                                CheckButterflyCompletion(r1, r2, r3, r4);
                            }
                        }
                    }
                }
            }
        }
    }

    void CheckButterflyCompletion(uint32_t r1, uint32_t r2, uint32_t r3, uint32_t r4)
    {
        if (!m_neighbors.count(r1) || !m_neighbors.count(r2) || !m_neighbors.count(r4)) return;
        
        // Find destinations connected to both r1 and r4
        std::set<uint32_t> r1Neighbors = m_neighbors[r1];
        std::set<uint32_t> r4Neighbors = m_neighbors[r4];
        
        // Remove already used nodes
        r1Neighbors.erase(m_nodeId);
        r1Neighbors.erase(r3);
        r4Neighbors.erase(r3);
        
        for (uint32_t d1 : r1Neighbors) {
            if (r4Neighbors.count(d1)) {
                // Find second destination connected to r2 and r4
                std::set<uint32_t> r2Neighbors = m_neighbors[r2];
                r2Neighbors.erase(m_nodeId);
                r2Neighbors.erase(r3);
                
                for (uint32_t d2 : r2Neighbors) {
                    if (d2 != d1 && r4Neighbors.count(d2)) {
                        ButterflyInfo butterfly;
                        butterfly.source = m_nodeId;
                        butterfly.r1 = r1;
                        butterfly.r2 = r2;
                        butterfly.r3 = r3;
                        butterfly.r4 = r4;
                        butterfly.d1 = d1;
                        butterfly.d2 = d2;
                        
                        m_butterflies.push_back(butterfly);
                        
                        if (m_butterflies.size() >= 10) return; // Limit to prevent explosion
                    }
                }
            }
        }
    }

    void BuildNeighborMapFromOLSR(Ptr<Node> node, Ptr<olsr::RoutingProtocol> olsrRouting)
    {
        m_neighbors.clear();
        
        Ptr<Ipv4> ipv4 = node->GetObject<Ipv4>();
        if (!ipv4) return;
        
        Ipv4Address myAddress = SelectPrimaryAddress(ipv4);
        if (myAddress == Ipv4Address("0.0.0.0")) {
            std::cout << "  Node " << m_nodeId << " has no valid primary address for OLSR mapping" << std::endl;
            return;
        }
        
        // Get neighbor set (1-hop neighbors)
        const olsr::NeighborSet& neighbors = olsrRouting->GetNeighbors();
        
        // Build 1-hop neighbor map
        std::map<Ipv4Address, std::set<Ipv4Address>> oneHopNeighbors;
        std::map<Ipv4Address, uint32_t> addressToNodeId;
        
        // Map addresses to node IDs
        addressToNodeId[myAddress] = m_nodeId;
        
        for (const auto& neighbor : neighbors) {
            if (neighbor.status == olsr::NeighborTuple::STATUS_SYM) {
                uint32_t neighborId = GetNodeIdFromAddress(neighbor.neighborMainAddr);
                if (neighborId == std::numeric_limits<uint32_t>::max()) {
                    continue;
                }
                addressToNodeId[neighbor.neighborMainAddr] = neighborId;
                
                // Add bidirectional link
                oneHopNeighbors[myAddress].insert(neighbor.neighborMainAddr);
                oneHopNeighbors[neighbor.neighborMainAddr].insert(myAddress);
            }
        }
        
        // Get topology set for 2-hop information
        const olsr::TopologySet& topology = olsrRouting->GetTopologySet();
        
        for (const auto& topoEntry : topology) {
            Ipv4Address lastAddr = topoEntry.lastAddr;
            Ipv4Address destAddr = topoEntry.destAddr;
            
            // Only include if within 2-hop distance from current node
            bool lastAddrIn2Hop = (lastAddr == myAddress) || 
                                  oneHopNeighbors[myAddress].count(lastAddr) > 0;
            bool destAddrIn2Hop = (destAddr == myAddress) || 
                                  oneHopNeighbors[myAddress].count(destAddr) > 0;
            
            if (lastAddrIn2Hop || destAddrIn2Hop) {
                uint32_t lastNodeId = GetNodeIdFromAddress(lastAddr);
                uint32_t destNodeId = GetNodeIdFromAddress(destAddr);
                if (lastNodeId == std::numeric_limits<uint32_t>::max() ||
                    destNodeId == std::numeric_limits<uint32_t>::max()) {
                    continue;
                }
                
                addressToNodeId[lastAddr] = lastNodeId;
                addressToNodeId[destAddr] = destNodeId;
                
                // Add topology link
                oneHopNeighbors[lastAddr].insert(destAddr);
                oneHopNeighbors[destAddr].insert(lastAddr);
            }
        }
        
        // Convert to node ID based neighbor map
        for (const auto& [addr, neighAddrs] : oneHopNeighbors) {
            if (addressToNodeId.count(addr)) {
                uint32_t nodeId = addressToNodeId[addr];
                for (const Ipv4Address& neighAddr : neighAddrs) {
                    if (addressToNodeId.count(neighAddr)) {
                        uint32_t neighNodeId = addressToNodeId[neighAddr];
                        m_neighbors[nodeId].insert(neighNodeId);
                    }
                }
            }
        }
    }
    
    Ipv4Address SelectPrimaryAddress(Ptr<Ipv4> ipv4)
    {
        for (uint32_t iface = 0; iface < ipv4->GetNInterfaces(); ++iface) {
            for (uint32_t addrIndex = 0; addrIndex < ipv4->GetNAddresses(iface); ++addrIndex) {
                Ipv4InterfaceAddress ifAddr = ipv4->GetAddress(iface, addrIndex);
                if (ifAddr.GetLocal() != Ipv4Address::GetLoopback()) {
                    return ifAddr.GetLocal();
                }
            }
        }
        return Ipv4Address("0.0.0.0");
    }
    
    uint32_t GetNodeIdFromAddress(const Ipv4Address& addr)
    {
        return ResolveNodeIdFromIpv4(addr);
    }

    std::set<uint32_t> BuildTwoHopNeighborhood() const
    {
        std::set<uint32_t> neighborhood;
        if (m_neighbors.count(m_nodeId) == 0) {
            std::cout << "  Node " << m_nodeId << " has no recorded neighbors from OLSR" << std::endl;
            return neighborhood;
        }

        std::queue<std::pair<uint32_t, uint32_t>> frontier;
        std::set<uint32_t> visited;

        frontier.push({m_nodeId, 0});
        visited.insert(m_nodeId);

        while (!frontier.empty()) {
            auto current = frontier.front();
            frontier.pop();

            uint32_t nodeId = current.first;
            uint32_t depth = current.second;
            neighborhood.insert(nodeId);

            if (depth >= 2) {
                continue;
            }

            auto it = m_neighbors.find(nodeId);
            if (it == m_neighbors.end()) {
                continue;
            }

            for (uint32_t neighborId : it->second) {
                if (visited.insert(neighborId).second) {
                    frontier.push({neighborId, depth + 1});
                }
            }
        }

        return neighborhood;
    }
};

class NetworkCodingApp : public Application
{
public:
    static TypeId GetTypeId(void)
    {
        static TypeId tid = TypeId("ns3::NetworkCodingApp")
            .SetParent<Application>()
            .AddConstructor<NetworkCodingApp>();
        return tid;
    }

    NetworkCodingApp() 
        : m_socket(nullptr), m_running(false), m_nodeId(0), m_packetsSent(0),
          m_isSource(false), m_isDestination(false), m_packetsReceived(0),
          m_detector(nullptr)
    {
    }

    void Setup(uint32_t nodeId, bool isSource, bool isDestination, 
               Ipv4Address destAddr, uint16_t port, uint32_t packetSize,
               uint16_t generationSize, uint32_t totalPackets, bool enableNetworkCoding = true)
    {
        m_nodeId = nodeId;
        m_isSource = isSource;
        m_isDestination = isDestination;
        m_destAddr = destAddr;
        m_destNodeId = ResolveNodeIdFromIpv4(destAddr);
        m_port = port;
            m_packetSize = packetSize;
        m_generationSize = generationSize;
        m_totalPackets = totalPackets;
        m_networkCodingEnabled = enableNetworkCoding;
        m_butterflyXorDecisions = 0;
        
        if (m_networkCodingEnabled) {
            m_detector = new ButterflyDetector(nodeId);
            std::cout << "Node " << nodeId << " setup with NETWORK CODING enabled" << std::endl;
        } else {
            m_detector = nullptr;
            std::cout << "Node " << nodeId << " setup with TRADITIONAL routing" << std::endl;
        }
        m_hasPreferredButterfly = false;
        m_preferredButterfly = ButterflyInfo();
        m_preferredButterflyExitNode = kInvalidNodeId;
        m_preferredButterflyExitDistance = 0;
        m_traditionalRouteDistance = 0;
    }

    void UpdateTopology(const std::map<uint32_t, std::set<uint32_t>>& neighbors)
    {
        if (m_detector) {
            m_detector->UpdateTopology(neighbors);
        }
    }
    
    void UpdateTopologyFromOLSR()
    {
        if (!m_detector || !m_running) {
            return;
        }

        std::cout << "[" << Simulator::Now().GetSeconds()
                  << "s] Node " << m_nodeId << " updating OLSR-derived topology view..." << std::endl;
        m_detector->UpdateTopologyFromOLSR(GetNode());
    }
    
    void PerformStartupButterflyDetection()
    {
        if (!m_detector || !m_running) return;
        
        std::cout << "[" << Simulator::Now().GetSeconds()
                  << "s] Node " << m_nodeId << " performing OLSR-driven butterfly detection..." << std::endl;

        m_detector->UpdateTopologyFromOLSR(GetNode());
        const auto& butterflies = m_detector->GetButterflies();

        m_butterflyDetectionComplete = true;
        m_canUseNetworkCoding = !butterflies.empty();
        m_detectedButterflies = butterflies;
        m_nextButterflyIndex = 0;

        if (m_canUseNetworkCoding) {
            RegisterButterflyPatterns(m_nodeId, butterflies);
        }

        if (m_canUseNetworkCoding) {
            std::cout << "*** Node " << m_nodeId << " discovered " << butterflies.size()
                      << " butterfly pattern(s); enabling network coding ***" << std::endl;
        } else {
            std::cout << "  Node " << m_nodeId << " found no butterfly opportunities; keeping network coding disabled" << std::endl;
        }
        
        std::cout << "  Butterfly detection complete. NC enabled: " << m_canUseNetworkCoding << std::endl;
    }

    uint32_t GetPacketsSent() const { return m_packetsSent + m_forwardingTransmissions; }
    uint32_t GetPacketsReceived() const { return m_packetsReceived; }
    uint32_t GetNCTransmissions() const { return m_ncTransmissions; }
    uint32_t GetButterflyXorActivations() const { return m_butterflyXorDecisions; }
    bool IsDecoded() const { return m_decoded; }

private:
    Ptr<Socket> m_socket;
    Ptr<Socket> m_tcpServerSocket;  // Listening socket for TCP destinations
    bool m_running;
    uint32_t m_nodeId;
    uint32_t m_packetsSent = 0;
    bool m_isSource;
    bool m_isDestination;
    uint32_t m_packetsReceived = 0;
    Ipv4Address m_destAddr;
    uint16_t m_port;
    uint32_t m_packetSize;
    uint16_t m_generationSize;
    uint32_t m_totalPackets;
    uint32_t m_ncTransmissions = 0;
    bool m_decoded = false;
    bool m_networkCodingEnabled = true;  // Flag to enable/disable NC
    uint32_t m_forwardingTransmissions = 0;
    bool m_forwardingTracingEnabled = false;
    uint32_t m_destNodeId = kInvalidNodeId;
    uint32_t m_butterflyXorDecisions = 0;
    
    // TCP specific
    std::set<uint32_t> m_receivedPacketIds;  // Track received packet IDs for destination
    bool m_connectionClosed = false;  // Track if we've already closed the connection
    uint32_t m_totalBytesReceived = 0;  // Track total bytes received in TCP mode
    bool m_tcpConnected = false;
    bool m_tcpConnectPending = false;
    bool m_tcpDataSent = false;  // Guard against duplicate transmissions
    std::vector<uint8_t> m_tcpRxBuffer;  // Accumulates TCP bytes until full message available
    static constexpr uint32_t kTcpHeaderSize = sizeof(uint32_t) * 2;
    
    // Butterfly detection state (computed once at startup)
    bool m_butterflyDetectionComplete = false;
    bool m_canUseNetworkCoding = false;
    
    // Loop prevention
    std::set<uint32_t> m_processedPackets;  // Track packet IDs we've seen
    uint32_t m_forwardCount = 0;
    uint32_t m_maxForwards = 20;  // Limit total forwards per node
    
    ButterflyDetector* m_detector;
    std::vector<ButterflyInfo> m_detectedButterflies;
    uint32_t m_nextButterflyIndex = 0;
    bool m_hasPreferredButterfly = false;
    ButterflyInfo m_preferredButterfly;
    uint32_t m_preferredButterflyExitNode = kInvalidNodeId;
    uint32_t m_preferredButterflyExitDistance = 0;
    uint32_t m_traditionalRouteDistance = 0;
    
    struct PendingPacketInfo {
        Ptr<Packet> packet;
        std::vector<uint8_t> coeffs;
        Ipv4Address finalDestination;
        uint32_t generationId = 0;
    };

    struct ButterflyBufferState {
        Ptr<Packet> fromR1;
        std::vector<uint8_t> coeffsR1;
        Ptr<Packet> fromR2;
        std::vector<uint8_t> coeffsR2;
        NetworkCodingHeader referenceHeader;
        bool hasReference = false;
    };

    std::vector<PendingPacketInfo> m_pendingPackets;
    std::vector<std::vector<uint8_t>> m_receivedCoeffs;
    std::vector<std::vector<uint8_t>> m_receivedPayloads;
    std::unordered_map<uint32_t, ButterflyBufferState> m_butterflyBuffers;
    void EnableForwardingTxTracking();
    bool GetNextButterflyForTransmission(ButterflyInfo& info);
    void LogButterflyUsage(const std::string& context, const ButterflyInfo& info) const;
    void LogPacketFlow(const std::string& context, Ptr<Packet> packet) const;
    bool ResolveNodeIdFromAddress(const Address& address, uint32_t& nodeId) const;
    bool ComputeNextHop(const Ipv4Address& finalDest, Ipv4Address& nextHop) const;
    bool SendPacketToNextHop(Ptr<Packet> packet, const Ipv4Address& finalDest, const std::string& context);
    Ipv4Address GetPrimaryIpv4() const;
    Ipv4Address GetPrimaryIpv4(uint32_t nodeId) const;
    bool NodeOwnsAddress(const Ipv4Address& addr) const;
    bool NodeOwnsAddress(uint32_t nodeId, const Ipv4Address& addr) const;
    bool ComputeHopDistance(uint32_t startNodeId, const Ipv4Address& destAddr, uint32_t& distance) const;
    uint32_t ComputePacketHash(Ptr<Packet> packet) const;
    void QueuePendingPacket(const PendingPacketInfo& info, const std::string& context);
    void EvaluatePendingPackets(const std::string& context);
    void FlushPendingPacketsUncoded(const std::string& context);
    uint32_t HashButterfly(const ButterflyInfo& info) const;
    bool ExecuteButterflyPlan(const ButterflyInfo& info, uint32_t exitNode);
    bool HandleButterflyPacket(const NetworkCodingHeader& header, Ptr<Packet> packet, const Address& from);
    void HandleButterflyRoleR1(const NetworkCodingHeader& header, Ptr<Packet> packet);
    void HandleButterflyRoleR2(const NetworkCodingHeader& header, Ptr<Packet> packet);
    void HandleButterflyRoleR3(const NetworkCodingHeader& header, Ptr<Packet> packet);
    void HandleButterflyRoleR4(const NetworkCodingHeader& header, Ptr<Packet> packet);
    void HandleButterflyExitNode(const NetworkCodingHeader& header, Ptr<Packet> packet);
    void ForwardButterflyPacket(Ptr<Packet> packet,
                                const NetworkCodingHeader& baseHeader,
                                uint32_t targetNodeId,
                                ButterflyHopRole hopRole,
                                const std::string& context);
    void SendButterflySourcePacket(const PendingPacketInfo& info,
                                   const ButterflyHeaderInfo& meta,
                                   uint32_t targetNodeId,
                                   ButterflyHopRole hopRole,
                                   const std::string& context);
    Ptr<Packet> XorPackets(Ptr<Packet> first, Ptr<Packet> second) const;
    std::vector<uint8_t> CombineCoefficients(const std::vector<uint8_t>& a,
                                             const std::vector<uint8_t>& b) const;
    Ptr<Packet> ClonePacketPayload(Ptr<Packet> packet) const;
    
    virtual void StartApplication(void) override
    {
        m_running = true;
        m_tcpDataSent = false;
        EnableForwardingTxTracking();
        
        if (m_networkCodingEnabled) {
            m_socket = Socket::CreateSocket(GetNode(), UdpSocketFactory::GetTypeId());
            InetSocketAddress local = InetSocketAddress(Ipv4Address::GetAny(), m_port);
            if (m_socket->Bind(local) == -1) {
                std::cout << "[NC] Node " << m_nodeId << " failed to bind UDP socket on port " << m_port << std::endl;
            } else {
                m_socket->SetRecvCallback(MakeCallback(&NetworkCodingApp::HandleRead, this));
                std::cout << "[NC] Node " << m_nodeId << " bound UDP socket on port " << m_port
                          << (m_isSource ? " (SOURCE)" : (m_isDestination ? " (DESTINATION)" : " (INTERMEDIATE)"))
                          << std::endl;
            }
        } else {
            // Configure destination behavior in TCP mode
            if (m_isDestination) {
                std::cout << "[TCP] Destination node " << m_nodeId << " listening on port " << m_port << std::endl;
                m_tcpServerSocket = Socket::CreateSocket(GetNode(), TcpSocketFactory::GetTypeId());
                if (!m_tcpServerSocket) {
                    std::cout << "  ERROR: Failed to create TCP listen socket" << std::endl;
                } else {
                    InetSocketAddress local = InetSocketAddress(Ipv4Address("0.0.0.0"), m_port);
                    if (m_tcpServerSocket->Bind(local) == -1) {
                        std::cout << "  ERROR: TCP bind failed on port " << m_port << std::endl;
                        m_tcpServerSocket = nullptr;
                    } else {
                        m_tcpServerSocket->Listen();
                        m_tcpServerSocket->SetAcceptCallback(
                            MakeCallback(&NetworkCodingApp::HandleTcpAcceptRequest, this),
                            MakeCallback(&NetworkCodingApp::HandleTcpAccepted, this));
                        std::cout << "  TCP listener ready for connections" << std::endl;
                    }
                }
            }

            // Configure source behavior in TCP mode
            if (m_isSource) {
                std::cout << "[TCP] Source node " << m_nodeId << " preparing TCP socket" << std::endl;
                m_socket = Socket::CreateSocket(GetNode(), TcpSocketFactory::GetTypeId());
                m_socket->SetConnectCallback(
                    MakeCallback(&NetworkCodingApp::HandleTcpConnectSuccess, this),
                    MakeCallback(&NetworkCodingApp::HandleTcpConnectFail, this));
            }
        }
        
        // Only do butterfly detection if network coding is enabled
        if (m_detector && m_networkCodingEnabled) {
            std::cout << "[STARTUP] Node " << m_nodeId << " scheduling butterfly detection for t=20.0s" << std::endl;
            Simulator::Schedule(Seconds(20.0), &NetworkCodingApp::PerformStartupButterflyDetection, this);
        } else {
            // In TCP mode, skip butterfly detection entirely
            m_butterflyDetectionComplete = true;  // Mark as complete but with NC disabled
            m_canUseNetworkCoding = false;
        }
        
        if (m_isSource) {
            Simulator::Schedule(Seconds(30.0), &NetworkCodingApp::SendPackets, this);
        }
    }

    virtual void StopApplication(void) override
    {
        m_running = false;
        if (m_socket) {
            m_socket->Close();
            m_socket = nullptr;
        }
        if (m_tcpServerSocket) {
            m_tcpServerSocket->Close();
            m_tcpServerSocket = nullptr;
        }
        m_tcpConnected = false;
        m_tcpConnectPending = false;
        if (m_detector) {
            delete m_detector;
            m_detector = nullptr;
        }
    }

    void SendPackets()
    {
        if (!m_isSource || !m_running) return;
        
        std::cout << "\n=== SOURCE NODE " << m_nodeId << " STARTING TRANSMISSION ===" << std::endl;
        std::cout << "Total packets to send: " << m_totalPackets << std::endl;
        std::cout << "Packet size: " << m_packetSize << " bytes" << std::endl;
        std::cout << "Mode: " << (m_networkCodingEnabled ? "NETWORK CODING" : "TRADITIONAL TCP") << std::endl;
        if (m_networkCodingEnabled) {
            std::cout << "Generation size: " << m_generationSize << std::endl;
        }
        std::cout << "Destination: " << m_destAddr << ":" << m_port << std::endl;
        
        if (m_networkCodingEnabled) {
            // Network coding mode: use UDP with custom header handling
            SendDataOverNC();
        } else {
            // Traditional TCP mode: establish/confirm TCP connection then send manually
            if (!m_socket) {
                std::cout << "[TCP] ERROR: Source socket not initialized" << std::endl;
                return;
            }

            if (!m_tcpConnected && !m_tcpConnectPending) {
                std::cout << "[TCP] Source node " << m_nodeId << " initiating connection to "
                          << m_destAddr << ":" << m_port << std::endl;
                int result = m_socket->Connect(InetSocketAddress(m_destAddr, m_port));
                if (result == 0) {
                    std::cout << "[TCP] Immediate connect success" << std::endl;
                    m_tcpConnected = true;
                    SendDataOverTCP();
                } else {
                    std::cout << "[TCP] Connect pending (result=" << result << ")" << std::endl;
                    m_tcpConnectPending = true;
                }
            } else if (m_tcpConnected) {
                SendDataOverTCP();
            } else {
                std::cout << "[TCP] Connection already pending, waiting for callback" << std::endl;
            }
        }
    }
    
    void SendDataOverTCP()
    {
        if (!m_tcpConnected || !m_socket) {
            std::cout << "[TCP] Cannot send yet - connection not established" << std::endl;
            return;
        }

        if (m_tcpDataSent) {
            std::cout << "[TCP] Data already transmitted for this connection, skipping duplicate send" << std::endl;
            return;
        }

        std::cout << "[" << Simulator::Now().GetSeconds() 
                 << "s] SOURCE Node " << m_nodeId << " sending TCP data manually" << std::endl;
        std::cout << "TCP destination: " << m_destAddr << ":" << m_port << std::endl;

        m_packetsSent = 0;
        for (uint32_t i = 0; i < m_totalPackets; ++i) {
            uint32_t packetId = i + 1;
            std::vector<uint8_t> payload(m_packetSize, 0);
            // Stamp payload with a recognizable pattern for easier debugging
            std::fill(payload.begin(), payload.end(), static_cast<uint8_t>(packetId));

            uint32_t payloadLen = payload.size();
            std::vector<uint8_t> framedMessage(kTcpHeaderSize + payloadLen, 0);
            std::memcpy(framedMessage.data(), &packetId, sizeof(packetId));
            std::memcpy(framedMessage.data() + sizeof(packetId), &payloadLen, sizeof(payloadLen));
            std::memcpy(framedMessage.data() + kTcpHeaderSize, payload.data(), payloadLen);

            Ptr<Packet> packet = Create<Packet>(framedMessage.data(), framedMessage.size());

            int result = m_socket->Send(packet);
            if (result >= 0) {
                m_packetsSent++;
                std::cout << "  [TCP] Sent framed packet " << packetId << " (" << result << " bytes)" << std::endl;
            } else {
                std::cout << "  [TCP] Send failed for packet " << packetId << " (result=" << result << ")" << std::endl;
            }
        }
        
        std::cout << "[TCP] Source transmitted " << m_packetsSent << " packets" << std::endl;
        m_tcpDataSent = true;
    }
    
    void SendDataOverNC()
    {
        std::cout << "[" << Simulator::Now().GetSeconds() 
                 << "s] SOURCE Node " << m_nodeId << " starting NC data transmission" << std::endl;
        
        for (uint32_t i = 1; i <= m_totalPackets; i++) {
            // Network coding mode: add NC header
            std::vector<uint8_t> data(m_packetSize, (uint8_t)i);
            Ptr<Packet> packet = Create<Packet>(data.data(), m_packetSize);
            uint32_t payloadHash = ComputePacketHash(packet);
            std::cout << "  Source packet " << i
                      << " hash=0x" << std::hex << payloadHash << std::dec << std::endl;

            PendingPacketInfo info;
            info.packet = packet;
            info.finalDestination = m_destAddr;
            info.generationId = i;
            info.coeffs.assign(m_generationSize, 0);
            if (!info.coeffs.empty()) {
                info.coeffs[(i - 1) % m_generationSize] = 1;
            }

            QueuePendingPacket(info, "SOURCE");
        }

        if (!m_pendingPackets.empty()) {
            std::cout << "  SOURCE still holds " << m_pendingPackets.size()
                      << " packet(s) < generation size; forwarding uncoded" << std::endl;
            FlushPendingPacketsUncoded("SOURCE DRAIN");
        }

        std::cout << "Source queued " << m_totalPackets << " packets via Network Coding" << std::endl;
    }

    void HandleRead(Ptr<Socket> socket)
    {
        while (true) {
            Address from;
            Ptr<Packet> packet = m_networkCodingEnabled ? socket->RecvFrom(from) : socket->Recv();
            if (!packet) {
                break;
            }

            if (!m_running) {
                break;
            }

            m_packetsReceived++;

            if (m_packetsReceived > 100) {
                std::cout << "Node " << m_nodeId << ": Rate limiting - dropping packet" << std::endl;
                break;
            }

            bool isNetworkCoded = false;
            std::vector<uint8_t> coeffs;
            uint32_t generationId = 0;
            Ipv4Address finalDestination = m_destAddr;
            NetworkCodingHeader removedHeader;

            if (m_networkCodingEnabled) {
                packet->RemoveHeader(removedHeader);
                coeffs = removedHeader.GetCoefficients();
                generationId = removedHeader.GetGenerationId();
                if (removedHeader.HasFinalDestination()) {
                    finalDestination = removedHeader.GetFinalDestination();
                }

                if (!coeffs.empty()) {
                    int nonZeroCount = 0;
                    for (auto coeff : coeffs) {
                        if (coeff != 0) {
                            nonZeroCount++;
                        }
                    }
                    isNetworkCoded = (nonZeroCount > 1);
                }
            } else {
                uint32_t chunkSize = packet->GetSize();
                std::vector<uint8_t> chunk(chunkSize);
                packet->CopyData(chunk.data(), chunkSize);
                std::cout << "  [TCP] Received chunk of " << chunkSize << " bytes" << std::endl;
                if (m_isDestination) {
                    m_tcpRxBuffer.insert(m_tcpRxBuffer.end(), chunk.begin(), chunk.end());
                    std::cout << "  [TCP] Reassembly buffer now " << m_tcpRxBuffer.size() << " bytes" << std::endl;
                }
            }

            std::string peerStr;
            uint32_t peerNodeId = kInvalidNodeId;
            bool resolvedPeer = false;

            if (m_networkCodingEnabled) {
                peerStr = AddressToString(from);
                resolvedPeer = ResolveNodeIdFromAddress(from, peerNodeId);
            } else {
                Address peerAddr;
                socket->GetPeerName(peerAddr);
                peerStr = AddressToString(peerAddr);
                resolvedPeer = ResolveNodeIdFromAddress(peerAddr, peerNodeId);
            }

            std::cout << "[" << Simulator::Now().GetSeconds()
                      << "s] Node " << m_nodeId << " RECEIVED "
                      << (m_networkCodingEnabled ? "" : "TCP ") << "packet from " << peerStr
                      << " (packet #" << m_packetsReceived << ")" << std::endl;
            if (resolvedPeer) {
                std::cout << "  Previous hop node: " << peerNodeId << std::endl;
            } else {
                std::cout << "  Previous hop node unresolved" << std::endl;
            }

            if (m_networkCodingEnabled) {
                if (isNetworkCoded) {
                    std::cout << "  *** NETWORK CODED PACKET RECEIVED ***" << std::endl;
                    std::cout << "  Coded coeffs: [";
                    for (size_t i = 0; i < coeffs.size(); i++) {
                        std::cout << (int)coeffs[i];
                        if (i < coeffs.size() - 1) {
                            std::cout << ",";
                        }
                    }
                    std::cout << "]" << std::endl;
                } else {
                    std::cout << "  Original packet (not network coded)" << std::endl;
                }

                TrackPacketFlowHop(m_nodeId, packet);
                LogPacketFlow("PATH", packet);
                if (generationId != 0) {
                    std::cout << "  Generation ID: " << generationId << std::endl;
                }
            } else {
                std::cout << "  Path: " << peerStr << " -> Node " << m_nodeId << std::endl;
            }

            bool isUltimateDestination = NodeOwnsAddress(finalDestination) ||
                                          (m_isDestination && finalDestination == m_destAddr);

            if (m_networkCodingEnabled && !isUltimateDestination && removedHeader.HasButterflyInfo()) {
                if (HandleButterflyPacket(removedHeader, packet, from)) {
                    continue;
                }
            }

            if (m_networkCodingEnabled) {
                if (isUltimateDestination) {
                    HandleDestinationPacketNC(removedHeader, packet);
                } else {
                    HandleIntermediatePacketNC(removedHeader, packet, from);
                }
            } else {
                if (m_isDestination) {
                    ProcessTcpReceiveBuffer();
                    if (m_decoded && !m_connectionClosed) {
                        m_connectionClosed = true;
                        m_tcpRxBuffer.clear();
                        socket->SetRecvCallback(MakeNullCallback<void, Ptr<Socket>>());
                        socket->Close();
                        std::cout << "[" << Simulator::Now().GetSeconds()
                                  << "s] DESTINATION Node " << m_nodeId
                                  << " closed connection (reception complete)" << std::endl;
                        return;
                    }
                } else {
                    std::cout << "  Intermediate node - TCP handles end-to-end delivery" << std::endl;
                }
            }
        }
    }

    void HandleDestinationPacketNC(const NetworkCodingHeader& header, Ptr<Packet> packet)
    {
        const std::vector<uint8_t>& coeffs = header.GetCoefficients();
        uint32_t payloadHash = ComputePacketHash(packet);
        std::vector<uint8_t> payload(packet->GetSize());
        if (!payload.empty()) {
            packet->CopyData(payload.data(), payload.size());
        }
        uint32_t nonZero = 0;
        for (uint8_t coeff : coeffs) {
            if (coeff != 0) {
                nonZero++;
            }
        }
        bool wasCoded = nonZero > 1;

        m_receivedCoeffs.push_back(coeffs);
    m_receivedPayloads.push_back(payload);
        
        std::cout << "[" << Simulator::Now().GetSeconds() 
                 << "s] DESTINATION Node " << m_nodeId << " received NC packet (" 
                 << m_receivedCoeffs.size() << "/" << m_generationSize << ")" << std::endl;
        
        std::cout << "  Packet coeffs: [";
        for (size_t i = 0; i < coeffs.size(); i++) {
            std::cout << (int)coeffs[i];
            if (i < coeffs.size() - 1) std::cout << ",";
        }
        std::cout << "]" << std::endl;
        std::cout << "  Payload hash: 0x" << std::hex << payloadHash << std::dec
                  << (wasCoded ? " (coded snapshot)" : " (original payload)") << std::endl;
        AttemptDestinationDecode();
    }

    bool AttemptDestinationDecode()
    {
        if (m_decoded) {
            return true;
        }

        if (m_generationSize == 0) {
            return false;
        }

        if (m_receivedCoeffs.size() < m_generationSize ||
            m_receivedPayloads.size() < m_generationSize) {
            return false;
        }

        struct Equation
        {
            std::vector<uint8_t> coeffs;
            std::vector<uint8_t> data;
        };

        std::vector<Equation> equations;
        equations.reserve(m_receivedCoeffs.size());
        for (size_t i = 0; i < m_receivedCoeffs.size(); ++i) {
            if (m_receivedCoeffs[i].size() != m_generationSize) {
                continue;
            }
            if (m_receivedPayloads[i].size() != m_packetSize) {
                continue;
            }
            equations.push_back({m_receivedCoeffs[i], m_receivedPayloads[i]});
        }

        if (equations.size() < m_generationSize) {
            return false;
        }

        size_t rows = equations.size();
        size_t cols = m_generationSize;
        size_t row = 0;
        std::vector<int> pivotRow(cols, -1);

        // Forward elimination over GF(2)
        for (size_t col = 0; col < cols && row < rows; ++col) {
            size_t pivot = row;
            while (pivot < rows && equations[pivot].coeffs[col] == 0) {
                pivot++;
            }
            if (pivot == rows) {
                continue;
            }
            std::swap(equations[row], equations[pivot]);
            pivotRow[col] = static_cast<int>(row);

            for (size_t r = row + 1; r < rows; ++r) {
                if (equations[r].coeffs[col]) {
                    for (size_t c = col; c < cols; ++c) {
                        equations[r].coeffs[c] ^= equations[row].coeffs[c];
                    }
                    for (uint32_t b = 0; b < m_packetSize; ++b) {
                        equations[r].data[b] ^= equations[row].data[b];
                    }
                }
            }
            row++;
        }

        std::vector<std::vector<uint8_t>> decoded(cols, std::vector<uint8_t>(m_packetSize, 0));
        std::vector<bool> hasDecoded(cols, false);

        for (int col = static_cast<int>(cols) - 1; col >= 0; --col) {
            int prow = pivotRow[col];
            if (prow == -1) {
                continue;
            }
            std::vector<uint8_t> value = equations[prow].data;
            bool solvable = true;
            for (size_t c = col + 1; c < cols; ++c) {
                if (equations[prow].coeffs[c]) {
                    if (!hasDecoded[c]) {
                        solvable = false;
                        break;
                    }
                    for (uint32_t b = 0; b < m_packetSize; ++b) {
                        value[b] ^= decoded[c][b];
                    }
                }
            }
            if (!solvable) {
                continue;
            }
            decoded[col] = value;
            hasDecoded[col] = true;
        }

        for (bool solved : hasDecoded) {
            if (!solved) {
                return false;
            }
        }

        m_decoded = true;
        std::cout << "\n*** DESTINATION " << m_nodeId << " SUCCESSFULLY DECODED ALL MESSAGES! ***" << std::endl;
        std::cout << "Total packets received: " << m_receivedCoeffs.size() << std::endl;
        std::cout << "Generation size: " << m_generationSize << std::endl;
        std::cout << "Decoding complete at time " << Simulator::Now().GetSeconds() << "s" << std::endl;

        for (size_t i = 0; i < decoded.size(); ++i) {
            Ptr<Packet> decodedPacket = Create<Packet>(decoded[i].data(), decoded[i].size());
            uint32_t decodedHash = ComputePacketHash(decodedPacket);
            std::cout << "  Decoded packet " << (i + 1)
                      << " hash=0x" << std::hex << decodedHash << std::dec << std::endl;
        }
        return true;
    }

    void ProcessTcpReceiveBuffer()
    {
        if (m_tcpRxBuffer.size() < kTcpHeaderSize) {
            return;
        }
        while (m_tcpRxBuffer.size() >= kTcpHeaderSize) {
            uint32_t packetId = 0;
            uint32_t payloadBytes = 0;
            std::memcpy(&packetId, m_tcpRxBuffer.data(), sizeof(uint32_t));
            std::memcpy(&payloadBytes, m_tcpRxBuffer.data() + sizeof(uint32_t), sizeof(uint32_t));
            
            if (payloadBytes == 0 || payloadBytes > m_packetSize * 4) {
                std::cout << "  [TCP] Warning: invalid payload length " << payloadBytes
                          << ". Dropping buffer." << std::endl;
                m_tcpRxBuffer.clear();
                break;
            }
            uint64_t totalNeeded = static_cast<uint64_t>(kTcpHeaderSize) + payloadBytes;
            if (m_tcpRxBuffer.size() < totalNeeded) {
                // Wait for more bytes
                break;
            }

            std::cout << "  [TCP] Reassembled framed packet id=" << packetId
                      << " payload=" << payloadBytes << " bytes" << std::endl;
            HandleDestinationPacketTCP(packetId, payloadBytes);
            m_tcpRxBuffer.erase(m_tcpRxBuffer.begin(), m_tcpRxBuffer.begin() + totalNeeded);
        }
    }

    void HandleDestinationPacketTCP(uint32_t packetId, uint32_t payloadBytes)
    {
        // Stop processing if already received all packets or connection closed
        if (m_decoded || m_connectionClosed) {
            std::cout << "[" << Simulator::Now().GetSeconds() 
                     << "s] DESTINATION Node " << m_nodeId 
                     << " ignoring packet (already received all messages)" << std::endl;
            return;
        }
        
        m_receivedPacketIds.insert(packetId);
        m_totalBytesReceived += payloadBytes;
        uint32_t expectedTotalBytes = m_totalPackets * m_packetSize;
        
        std::cout << "[" << Simulator::Now().GetSeconds() 
                 << "s] DESTINATION Node " << m_nodeId << " received TCP packet " << packetId 
                 << " (unique packets: " << m_receivedPacketIds.size() << "/" << m_totalPackets 
                 << ", bytes: " << m_totalBytesReceived << "/" << expectedTotalBytes << ")" << std::endl;
        
        if (m_receivedPacketIds.size() >= m_totalPackets) {
            m_decoded = true;
            std::cout << "\n*** DESTINATION " << m_nodeId << " SUCCESSFULLY RECEIVED ALL MESSAGES! ***" << std::endl;
            std::cout << "Total unique packets received: " << m_receivedPacketIds.size() << std::endl;
            std::cout << "Reception complete at time " << Simulator::Now().GetSeconds() << "s" << std::endl;
        }
    }
    
    // PacketSink callback for TCP mode (like butterfly example)
    void OnTcpPacketReceived(Ptr<const Packet> packet, const Address& from)
    {
        m_packetsReceived++;
        uint32_t totalBytes = packet->GetSize();
        
        std::cout << "[TCP] *** DESTINATION " << m_nodeId << " RECEIVED PACKET *** size=" 
                 << totalBytes << " from " << AddressToString(from)
                 << " (packet #" << m_packetsReceived << ")" << std::endl;
        
        // Calculate expected total bytes (use instance variable instead of static)
        uint32_t expectedTotalBytes = m_totalPackets * m_packetSize;
        m_totalBytesReceived += totalBytes;  // Use member variable
        
        std::cout << "  Node " << m_nodeId << " total received: " << m_totalBytesReceived 
                 << "/" << expectedTotalBytes << " bytes" << std::endl;
        
        if (m_totalBytesReceived >= expectedTotalBytes) {
            if (!m_decoded) {
                m_decoded = true;
                std::cout << "\n*** TCP DESTINATION " << m_nodeId 
                         << " RECEIVED COMPLETE DATA! ***" << std::endl;
                std::cout << "Total bytes received: " << m_totalBytesReceived << std::endl;
                std::cout << "Reception complete at time " << Simulator::Now().GetSeconds() << "s" << std::endl;
            }
        }
    }

    bool HandleTcpAcceptRequest(Ptr<Socket> socket, const Address& from)
    {
        std::cout << "[TCP] Node " << m_nodeId << " received connection request from "
                  << AddressToString(from) << std::endl;
        return true;  // Accept all incoming connections
    }

    void HandleTcpAccepted(Ptr<Socket> socket, const Address& from)
    {
        std::cout << "[TCP] Node " << m_nodeId << " accepted connection from "
                  << AddressToString(from) << std::endl;
        m_socket = socket;
        m_socket->SetRecvCallback(MakeCallback(&NetworkCodingApp::HandleRead, this));
    }

    void HandleTcpConnectSuccess(Ptr<Socket> socket)
    {
        std::cout << "[TCP] Node " << m_nodeId << " connection established" << std::endl;
        m_tcpConnected = true;
        m_tcpConnectPending = false;
        SendDataOverTCP();
    }

    void HandleTcpConnectFail(Ptr<Socket> socket)
    {
        std::cout << "[TCP] Node " << m_nodeId << " connection FAILED" << std::endl;
        m_tcpConnected = false;
        m_tcpConnectPending = false;
    }

    std::string AddressToString(const Address& address) const
    {
        std::ostringstream oss;
        if (InetSocketAddress::IsMatchingType(address)) {
            InetSocketAddress ipv4 = InetSocketAddress::ConvertFrom(address);
            oss << ipv4.GetIpv4() << ":" << ipv4.GetPort();
        } else if (Inet6SocketAddress::IsMatchingType(address)) {
            Inet6SocketAddress ipv6 = Inet6SocketAddress::ConvertFrom(address);
            oss << ipv6.GetIpv6() << ":" << ipv6.GetPort();
        } else {
            oss << "[unknown address type]";
        }
        return oss.str();
    }

    void HandleIntermediatePacketNC(const NetworkCodingHeader& header, Ptr<Packet> packet, const Address& from)
    {
        std::cout << "[" << Simulator::Now().GetSeconds() 
                 << "s] INTERMEDIATE Node " << m_nodeId << " received NC packet from " << from << std::endl;
        uint32_t prevHop = kInvalidNodeId;
        if (ResolveNodeIdFromAddress(from, prevHop)) {
            std::cout << "  Previous hop node: " << prevHop << std::endl;
        }
        
        PendingPacketInfo info;
        info.packet = packet->Copy();
        info.coeffs = header.GetCoefficients();
        info.finalDestination = header.HasFinalDestination() ? header.GetFinalDestination() : m_destAddr;
        info.generationId = header.GetGenerationId();

        QueuePendingPacket(info, "INTERMEDIATE");
    }
    void HandleIntermediatePacketTCP(uint32_t packetId, Ptr<Packet> packet)
    {
        std::cout << "[" << Simulator::Now().GetSeconds() 
                 << "s] INTERMEDIATE Node " << m_nodeId << " received TCP packet " << packetId << std::endl;
        
        std::cout << "  TCP mode" << std::endl;
        std::cout << "  No application-level forwarding needed" << std::endl;
    }

    bool ShouldUseNetworkCoding()
    {
        // First check if network coding is enabled at all
        if (!m_networkCodingEnabled) {
            return false;  // Traditional mode - never use NC
        }
        
        // Use cached butterfly detection results from startup
        if (!m_butterflyDetectionComplete) {
            std::cout << "[" << Simulator::Now().GetSeconds() 
                     << "s] Node " << m_nodeId << " NC DECISION: NO - butterfly detection not yet complete" << std::endl;
            return false;
        }
        
        if (!m_canUseNetworkCoding || m_detectedButterflies.empty()) {
            std::cout << "[" << Simulator::Now().GetSeconds()
                      << "s] Node " << m_nodeId << " NC DECISION: NO - no butterfly opportunities" << std::endl;
            return false;
        }

        Ipv4Address nextHopAddr;
        uint32_t nextHopNodeId = kInvalidNodeId;
        bool hasNextHop = ComputeNextHop(m_destAddr, nextHopAddr);
        if (hasNextHop) {
            nextHopNodeId = ResolveNodeIdFromIpv4(nextHopAddr);
        }

        uint32_t traditionalDistance = 0;
        if (!ComputeHopDistance(m_nodeId, m_destAddr, traditionalDistance)) {
            std::cout << "[" << Simulator::Now().GetSeconds()
                      << "s] Node " << m_nodeId << " NC DECISION: NO - unable to compute traditional route distance" << std::endl;
            return false;
        }

        if (traditionalDistance <= 1) {
            std::cout << "[" << Simulator::Now().GetSeconds()
                      << "s] Node " << m_nodeId
                      << " NC DECISION: NO - already directly connected to destination ("
                      << traditionalDistance << " hop)" << std::endl;
            return false;
        }

        uint32_t bestDistance = std::numeric_limits<uint32_t>::max();
        ButterflyInfo bestButterfly;
        uint32_t bestExitNode = kInvalidNodeId;

        for (const auto& butterfly : m_detectedButterflies) {
            uint32_t d1Distance = 0;
            bool hasD1 = ComputeHopDistance(butterfly.d1, m_destAddr, d1Distance);
            uint32_t d2Distance = 0;
            bool hasD2 = ComputeHopDistance(butterfly.d2, m_destAddr, d2Distance);

            if (!hasD1 && !hasD2) {
                continue;
            }

            uint32_t candidateExit = kInvalidNodeId;
            uint32_t candidateDistance = std::numeric_limits<uint32_t>::max();

            if (hasD1) {
                candidateExit = butterfly.d1;
                candidateDistance = d1Distance;
            }

            if (hasD2 && d2Distance < candidateDistance) {
                candidateExit = butterfly.d2;
                candidateDistance = d2Distance;
            }

            if (candidateExit == kInvalidNodeId) {
                continue;
            }

            if (candidateDistance < bestDistance && candidateDistance < traditionalDistance) {
                bestDistance = candidateDistance;
                bestButterfly = butterfly;
                bestExitNode = candidateExit;
            }
        }

        if (bestExitNode == kInvalidNodeId) {
            std::cout << "[" << Simulator::Now().GetSeconds()
                      << "s] Node " << m_nodeId
                      << " NC DECISION: NO - butterfly exits are not closer than traditional path ("
                      << traditionalDistance << " hops)" << std::endl;
            return false;
        }

        uint32_t preferredIngress = (bestExitNode == bestButterfly.d1) ? bestButterfly.r1 : bestButterfly.r2;
        uint32_t centralNode = bestButterfly.r3;

        if (hasNextHop && nextHopNodeId != kInvalidNodeId &&
            nextHopNodeId != preferredIngress &&
            nextHopNodeId != centralNode &&
            nextHopNodeId != bestExitNode) {
            std::cout << "[" << Simulator::Now().GetSeconds()
                      << "s] Node " << m_nodeId
                      << " NC DECISION: NO - next hop " << nextHopNodeId
                      << " is outside permitted butterfly branch" << std::endl;
            return false;
        }

        m_hasPreferredButterfly = true;
        m_preferredButterfly = bestButterfly;
        m_preferredButterflyExitNode = bestExitNode;
        m_preferredButterflyExitDistance = bestDistance;
        m_traditionalRouteDistance = traditionalDistance;

        std::cout << "[" << Simulator::Now().GetSeconds()
                  << "s] Node " << m_nodeId << " NC DECISION: YES - butterfly exit node "
                  << bestExitNode << " (" << bestDistance << " hops) beats traditional route ("
                  << traditionalDistance << " hops)" << std::endl;
        return true;
    }

    void PerformNetworkCoding()
    {
        if (m_pendingPackets.empty()) {
            return;
        }
        ButterflyInfo selectedButterfly;
        bool hasButterfly = false;
        uint32_t exitNode = kInvalidNodeId;

        if (m_hasPreferredButterfly) {
            selectedButterfly = m_preferredButterfly;
            exitNode = m_preferredButterflyExitNode;
            hasButterfly = true;
            m_hasPreferredButterfly = false;
        } else {
            hasButterfly = GetNextButterflyForTransmission(selectedButterfly);
        }
        if (hasButterfly) {
            LogButterflyUsage("[NC SELECT]", selectedButterfly);
            m_butterflyXorDecisions++;
            std::cout << "  XOR activation count (node " << m_nodeId << "): "
                      << m_butterflyXorDecisions << std::endl;
            if (exitNode != kInvalidNodeId) {
                std::cout << "  Selected butterfly exit node " << exitNode
                          << " (distance " << m_preferredButterflyExitDistance
                          << ") vs traditional (distance " << m_traditionalRouteDistance << ")" << std::endl;
            }
        } else {
            std::cout << "[NC SELECT] Node " << m_nodeId << " has no butterfly metadata available" << std::endl;
        }

        if (hasButterfly && m_isSource) {
            uint32_t resolvedExit = (exitNode != kInvalidNodeId) ? exitNode : selectedButterfly.d1;
            if (ExecuteButterflyPlan(selectedButterfly, resolvedExit)) {
                return;
            }
        }

        size_t batchSize = std::min(static_cast<size_t>(m_generationSize), m_pendingPackets.size());
        if (batchSize == 0) {
            std::cout << "  No packets available for coding" << std::endl;
            return;
        }

        size_t batchStart = 0;

        std::cout << "\n=== NETWORK CODING OPERATION (Node " << m_nodeId << ") ===" << std::endl;
        std::cout << "Encoding " << batchSize << " packets..." << std::endl;
        if (exitNode != kInvalidNodeId) {
            uint32_t branchIngress = (exitNode == selectedButterfly.d1) ? selectedButterfly.r1 : selectedButterfly.r2;
            Ipv4Address branchNextHop;
            if (ComputeNextHop(m_destAddr, branchNextHop)) {
                uint32_t branchNextHopId = ResolveNodeIdFromIpv4(branchNextHop);
                if (branchNextHopId != kInvalidNodeId &&
                    branchNextHopId != branchIngress &&
                    branchNextHopId != selectedButterfly.r3 &&
                    branchNextHopId != exitNode) {
                    std::cout << "  Route drift detected: next hop " << branchNextHopId
                              << " is outside butterfly branch, forwarding pending packets uncoded" << std::endl;
                    while (!m_pendingPackets.empty()) {
                        PendingPacketInfo info = m_pendingPackets.back();
                        m_pendingPackets.pop_back();
                        ForwardPacketNC(info);
                    }
                    return;
                }
            }
        }
        
        // Simple encoding: if we have multiple packets, XOR them
        // If we have only one packet, add a simple coding header
        std::vector<uint8_t> codedData(m_packetSize, 0);
        std::vector<uint8_t> combinedCoeffs(m_generationSize, 0);
        Ipv4Address finalDest = m_pendingPackets[batchStart].finalDestination;

        if (batchSize >= 2) {
            for (size_t i = 0; i < batchSize; i++) {
                PendingPacketInfo& pending = m_pendingPackets[batchStart + i];
                std::vector<uint8_t> data(m_packetSize);
                pending.packet->CopyData(data.data(), m_packetSize);
                for (uint32_t j = 0; j < m_packetSize; j++) {
                    codedData[j] ^= data[j];
                }
                for (size_t c = 0; c < std::min(combinedCoeffs.size(), pending.coeffs.size()); ++c) {
                    combinedCoeffs[c] ^= pending.coeffs[c];
                }
                std::cout << "  XOR-ed packet " << (i + 1) << std::endl;
            }
        } else {
            PendingPacketInfo& pending = m_pendingPackets[batchStart];
            pending.packet->CopyData(codedData.data(), m_packetSize);
            combinedCoeffs = pending.coeffs;
            std::cout << "  Single packet coding (added coefficients)" << std::endl;
        }
        
        // Create coded packet
        Ptr<Packet> codedPacket = Create<Packet>(codedData.data(), m_packetSize);
        TrackPacketFlowHop(m_nodeId, codedPacket);
        LogPacketFlow("XOR START", codedPacket);
        
        NetworkCodingHeader header;
        header.SetGenerationId(0);
        header.SetGenerationSize(m_generationSize);
        header.SetCoefficients(combinedCoeffs);
        header.SetFinalDestination(finalDest);
        codedPacket->AddHeader(header);
        
        if (SendPacketToNextHop(codedPacket, finalDest, "NC XOR")) {
            m_packetsSent++;
            m_ncTransmissions++;
            std::cout << "[" << Simulator::Now().GetSeconds() 
                     << "s] *** NETWORK CODING SUCCESS *** Node " << m_nodeId << " forwarded coded packet" << std::endl;
            std::cout << "  Combined " << m_pendingPackets.size() << " packets into 1 coded packet" << std::endl;
            std::cout << "  NC transmission #" << m_ncTransmissions << std::endl;
            if (hasButterfly) {
                LogButterflyUsage("[NC SENT]", selectedButterfly);
            }
        }
        
        // Clear all processed packets
        m_pendingPackets.erase(m_pendingPackets.begin(), m_pendingPackets.begin() + batchSize);
        std::cout << "Network coding complete. Processed " << batchSize << " packets." << std::endl;
    }

    void ForwardPacketNC(const PendingPacketInfo& packetInfo, bool allowButterflyMetadata = true)
    {
        bool countAsForward = !m_isSource;
        if (countAsForward) {
            if (m_forwardCount >= m_maxForwards) {
                std::cout << "  Node " << m_nodeId << ": Forward limit reached (" << m_maxForwards
                          << ") - dropping packet" << std::endl;
                return;
            }
            m_forwardCount++;
        }

        ButterflyInfo selectedButterfly;
        bool hasButterfly = false;
        if (allowButterflyMetadata) {
            hasButterfly = GetNextButterflyForTransmission(selectedButterfly);
            if (hasButterfly) {
                LogButterflyUsage("[NC FORWARD SELECT]", selectedButterfly);
            } else {
                std::cout << "[NC FORWARD SELECT] Node " << m_nodeId
                          << " has no butterfly metadata available" << std::endl;
            }
        }
        
        // Re-add the NetworkCoding header
        NetworkCodingHeader header;
        header.SetGenerationId(packetInfo.generationId);
        header.SetGenerationSize(m_generationSize);
        header.SetCoefficients(packetInfo.coeffs);
        header.SetFinalDestination(packetInfo.finalDestination);
        Ptr<Packet> packetCopy = packetInfo.packet->Copy();
        packetCopy->AddHeader(header);

        double now = Simulator::Now().GetSeconds();
        if (countAsForward) {
            std::cout << "[" << now << "s] Node " << m_nodeId
                      << " FORWARDING NC packet (forward #" << m_forwardCount << ")" << std::endl;
            LogPacketFlow("FORWARD", packetCopy);
        } else {
            std::cout << "[" << now << "s] SOURCE Node " << m_nodeId
                      << " sending NC packet without butterfly optimizations" << std::endl;
            LogPacketFlow("SEND", packetCopy);
        }
        
        if (SendPacketToNextHop(packetCopy, packetInfo.finalDestination, "FORWARD")) {
            m_packetsSent++;
            if (countAsForward && !m_isSource) {
                m_forwardingTransmissions++;
            }
            std::cout << "  NC Forward successful" << std::endl;
            if (hasButterfly) {
                LogButterflyUsage("[NC FORWARD SENT]", selectedButterfly);
            }
        }
    }

};

bool NetworkCodingApp::GetNextButterflyForTransmission(ButterflyInfo& info)
{
    if (m_detectedButterflies.empty()) {
        return false;
    }

    if (m_nextButterflyIndex >= m_detectedButterflies.size()) {
        m_nextButterflyIndex = 0;
    }

    info = m_detectedButterflies[m_nextButterflyIndex];
    m_nextButterflyIndex = (m_nextButterflyIndex + 1) % m_detectedButterflies.size();
    return true;
}

void NetworkCodingApp::LogButterflyUsage(const std::string& context, const ButterflyInfo& info) const
{
    std::cout << context << " Node " << m_nodeId
              << " using butterfly (s=" << info.source
              << " r1=" << info.r1
              << " r2=" << info.r2
              << " r3=" << info.r3
              << " r4=" << info.r4
              << " d1=" << info.d1
              << " d2=" << info.d2 << ")" << std::endl;
}

void NetworkCodingApp::LogPacketFlow(const std::string& context, Ptr<Packet> packet) const
{
    if (!packet) {
        return;
    }

    uint64_t uid = packet->GetUid();
    std::cout << "  [FLOW] " << context << " packetUID=" << uid
              << " path: " << DescribePacketFlow(uid) << std::endl;
}

uint32_t NetworkCodingApp::HashButterfly(const ButterflyInfo& info) const
{
    uint32_t hash = 2166136261u;
    auto mix = [&](uint32_t value) {
        hash ^= value + 0x9e3779b9u + (hash << 6) + (hash >> 2);
    };

    mix(info.source);
    mix(info.r1);
    mix(info.r2);
    mix(info.r3);
    mix(info.r4);
    mix(info.d1);
    mix(info.d2);
    return hash;
}

bool NetworkCodingApp::ExecuteButterflyPlan(const ButterflyInfo& info, uint32_t exitNode)
{
    if (!m_isSource || m_pendingPackets.size() < 2) {
        return false;
    }

    ButterflyHeaderInfo meta;
    meta.butterflyId = HashButterfly(info);
    meta.sourceNode = info.source;
    meta.r1 = info.r1;
    meta.r2 = info.r2;
    meta.r3 = info.r3;
    meta.r4 = info.r4;
    meta.d1 = info.d1;
    meta.d2 = info.d2;
    meta.targetExitNode = (exitNode != kInvalidNodeId) ? exitNode : info.d1;

    PendingPacketInfo first = m_pendingPackets[0];
    PendingPacketInfo second = m_pendingPackets[1];

    std::cout << "  [BUTTERFLY] Source executing plan via exit node "
              << meta.targetExitNode << std::endl;

    SendButterflySourcePacket(first, meta, info.r1,
                              BUTTERFLY_HOP_SOURCE_TO_R1,
                              "BUTTERFLY S->R1");
    SendButterflySourcePacket(second, meta, info.r2,
                              BUTTERFLY_HOP_SOURCE_TO_R2,
                              "BUTTERFLY S->R2");

    m_pendingPackets.erase(m_pendingPackets.begin(), m_pendingPackets.begin() + 2);
    return true;
}

bool NetworkCodingApp::HandleButterflyPacket(const NetworkCodingHeader& header,
                                             Ptr<Packet> packet,
                                             const Address& from)
{
    (void)from;
    if (!header.HasButterflyInfo()) {
        return false;
    }

    const ButterflyHeaderInfo& meta = header.GetButterflyInfo();
    ButterflyHopRole role = static_cast<ButterflyHopRole>(header.GetButterflyHopRole());

    switch (role) {
        case BUTTERFLY_HOP_SOURCE_TO_R1:
            if (m_nodeId == meta.r1) {
                HandleButterflyRoleR1(header, packet);
                return true;
            }
            break;
        case BUTTERFLY_HOP_SOURCE_TO_R2:
            if (m_nodeId == meta.r2) {
                HandleButterflyRoleR2(header, packet);
                return true;
            }
            break;
        case BUTTERFLY_HOP_R1_TO_R3:
        case BUTTERFLY_HOP_R2_TO_R3:
            if (m_nodeId == meta.r3) {
                HandleButterflyRoleR3(header, packet);
                return true;
            }
            break;
        case BUTTERFLY_HOP_R3_TO_R4:
            if (m_nodeId == meta.r4) {
                HandleButterflyRoleR4(header, packet);
                return true;
            }
            break;
        case BUTTERFLY_HOP_R1_TO_D1:
        case BUTTERFLY_HOP_R2_TO_D2:
        case BUTTERFLY_HOP_R4_TO_D1:
        case BUTTERFLY_HOP_R4_TO_D2:
            if (m_nodeId == meta.d1 || m_nodeId == meta.d2) {
                HandleButterflyExitNode(header, packet);
                return true;
            }
            break;
        default:
            break;
    }

    return false;
}

void NetworkCodingApp::HandleButterflyRoleR1(const NetworkCodingHeader& header, Ptr<Packet> packet)
{
    const ButterflyHeaderInfo& meta = header.GetButterflyInfo();
    std::cout << "  [BUTTERFLY] Node " << m_nodeId << " handling R1 duties" << std::endl;

    if (meta.targetExitNode == meta.d1) {
        ForwardButterflyPacket(packet, header, meta.d1, BUTTERFLY_HOP_R1_TO_D1, "BUTTERFLY R1->D1");
    }
    ForwardButterflyPacket(packet, header, meta.r3, BUTTERFLY_HOP_R1_TO_R3, "BUTTERFLY R1->R3");
}

void NetworkCodingApp::HandleButterflyRoleR2(const NetworkCodingHeader& header, Ptr<Packet> packet)
{
    const ButterflyHeaderInfo& meta = header.GetButterflyInfo();
    std::cout << "  [BUTTERFLY] Node " << m_nodeId << " handling R2 duties" << std::endl;

    if (meta.targetExitNode == meta.d2) {
        ForwardButterflyPacket(packet, header, meta.d2, BUTTERFLY_HOP_R2_TO_D2, "BUTTERFLY R2->D2");
    }
    ForwardButterflyPacket(packet, header, meta.r3, BUTTERFLY_HOP_R2_TO_R3, "BUTTERFLY R2->R3");
}

void NetworkCodingApp::HandleButterflyRoleR3(const NetworkCodingHeader& header, Ptr<Packet> packet)
{
    const ButterflyHeaderInfo& meta = header.GetButterflyInfo();
    ButterflyHopRole role = static_cast<ButterflyHopRole>(header.GetButterflyHopRole());
    uint32_t bufferId = meta.butterflyId;
    ButterflyBufferState& state = m_butterflyBuffers[bufferId];

    std::cout << "  [BUTTERFLY] Node " << m_nodeId << " buffering input at R3 (role="
              << static_cast<uint32_t>(role) << ")" << std::endl;

    if (!state.hasReference) {
        state.referenceHeader = header;
        state.hasReference = true;
    }

    if (role == BUTTERFLY_HOP_R1_TO_R3) {
        state.fromR1 = packet->Copy();
        state.coeffsR1 = header.GetCoefficients();
        std::cout << "    Stored branch from R1" << std::endl;
    } else if (role == BUTTERFLY_HOP_R2_TO_R3) {
        state.fromR2 = packet->Copy();
        state.coeffsR2 = header.GetCoefficients();
        std::cout << "    Stored branch from R2" << std::endl;
    }

    if (state.fromR1 && state.fromR2) {
        Ptr<Packet> coded = XorPackets(state.fromR1, state.fromR2);
        std::vector<uint8_t> coeffs = CombineCoefficients(state.coeffsR1, state.coeffsR2);
        NetworkCodingHeader forwardHeader = state.referenceHeader;
        forwardHeader.SetCoefficients(coeffs);
        std::cout << "    Both inputs available; forwarding coded packet toward R4" << std::endl;
        ForwardButterflyPacket(coded, forwardHeader, meta.r4, BUTTERFLY_HOP_R3_TO_R4, "BUTTERFLY R3->R4");
        m_butterflyBuffers.erase(bufferId);
    }
}

void NetworkCodingApp::HandleButterflyRoleR4(const NetworkCodingHeader& header, Ptr<Packet> packet)
{
    const ButterflyHeaderInfo& meta = header.GetButterflyInfo();
    uint32_t targetExit = meta.targetExitNode != 0 ? meta.targetExitNode : meta.d1;
    ButterflyHopRole hop = (targetExit == meta.d1) ? BUTTERFLY_HOP_R4_TO_D1 : BUTTERFLY_HOP_R4_TO_D2;

    std::cout << "  [BUTTERFLY] Node " << m_nodeId << " forwarding coded packet toward exit "
              << targetExit << std::endl;
    ForwardButterflyPacket(packet, header, targetExit, hop, "BUTTERFLY R4->EXIT");
}

void NetworkCodingApp::HandleButterflyExitNode(const NetworkCodingHeader& header, Ptr<Packet> packet)
{
    const ButterflyHeaderInfo& meta = header.GetButterflyInfo();
    uint32_t targetExit = meta.targetExitNode != 0 ? meta.targetExitNode : meta.d1;

    if (m_nodeId != targetExit) {
        std::cout << "  [BUTTERFLY] Node " << m_nodeId
                  << " ignoring butterfly packet for exit " << targetExit << std::endl;
        return;
    }

    NetworkCodingHeader forwardHeader = header;
    forwardHeader.SetButterflyHopRole(BUTTERFLY_HOP_NONE);
    Ptr<Packet> forwardPacket = packet->Copy();
    forwardPacket->AddHeader(forwardHeader);

    std::cout << "  [BUTTERFLY] Exit node " << m_nodeId
              << " relaying packet toward original destination "
              << forwardHeader.GetFinalDestination() << std::endl;

    if (SendPacketToNextHop(forwardPacket, forwardHeader.GetFinalDestination(), "BUTTERFLY EXIT")) {
        m_packetsSent++;
        m_ncTransmissions++;
        if (!m_isSource) {
            m_forwardingTransmissions++;
        }
    }
}

void NetworkCodingApp::ForwardButterflyPacket(Ptr<Packet> packet,
                                              const NetworkCodingHeader& baseHeader,
                                              uint32_t targetNodeId,
                                              ButterflyHopRole hopRole,
                                              const std::string& context)
{
    if (!packet) {
        return;
    }

    Ipv4Address hopAddr = GetPrimaryIpv4(targetNodeId);
    if (hopAddr == Ipv4Address("0.0.0.0")) {
        std::cout << "  [BUTTERFLY] Unable to resolve IPv4 for node " << targetNodeId << std::endl;
        return;
    }

    NetworkCodingHeader header = baseHeader;
    header.SetButterflyHopRole(static_cast<uint8_t>(hopRole));

    Ptr<Packet> outbound = ClonePacketPayload(packet);
    outbound->AddHeader(header);
    if (SendPacketToNextHop(outbound, hopAddr, context)) {
        m_packetsSent++;
        m_ncTransmissions++;
        if (!m_isSource) {
            m_forwardingTransmissions++;
        }
    }
}

void NetworkCodingApp::SendButterflySourcePacket(const PendingPacketInfo& info,
                                                 const ButterflyHeaderInfo& meta,
                                                 uint32_t targetNodeId,
                                                 ButterflyHopRole hopRole,
                                                 const std::string& context)
{
    if (!info.packet) {
        return;
    }

    NetworkCodingHeader header;
    header.SetGenerationId(info.generationId);
    header.SetGenerationSize(m_generationSize);
    header.SetCoefficients(info.coeffs);
    header.SetFinalDestination(info.finalDestination);
    header.SetButterflyInfo(meta);
    header.SetButterflyHopRole(static_cast<uint8_t>(hopRole));
    ForwardButterflyPacket(info.packet, header, targetNodeId, hopRole, context);
}

Ptr<Packet> NetworkCodingApp::XorPackets(Ptr<Packet> first, Ptr<Packet> second) const
{
    if (!first || !second) {
        return nullptr;
    }

    uint32_t size = std::min(first->GetSize(), second->GetSize());
    if (size == 0) {
        return Create<Packet>();
    }

    std::vector<uint8_t> buffer(size);
    std::vector<uint8_t> temp(size);
    first->CopyData(buffer.data(), size);
    second->CopyData(temp.data(), size);
    for (uint32_t i = 0; i < size; ++i) {
        buffer[i] ^= temp[i];
    }
    return Create<Packet>(buffer.data(), size);
}

std::vector<uint8_t> NetworkCodingApp::CombineCoefficients(const std::vector<uint8_t>& a,
                                                           const std::vector<uint8_t>& b) const
{
    size_t maxLen = std::max(a.size(), b.size());
    std::vector<uint8_t> result(maxLen, 0);
    for (size_t i = 0; i < maxLen; ++i) {
        uint8_t first = (i < a.size()) ? a[i] : 0;
        uint8_t second = (i < b.size()) ? b[i] : 0;
        result[i] = first ^ second;
    }
    return result;
}

Ptr<Packet> NetworkCodingApp::ClonePacketPayload(Ptr<Packet> packet) const
{
    if (!packet) {
        return Create<Packet>();
    }

    uint32_t size = packet->GetSize();
    if (size == 0) {
        return Create<Packet>();
    }

    std::vector<uint8_t> buffer(size);
    packet->CopyData(buffer.data(), size);
    return Create<Packet>(buffer.data(), size);
}

uint32_t NetworkCodingApp::ComputePacketHash(Ptr<Packet> packet) const
{
    if (!packet) {
        return 0;
    }

    uint32_t hash = 2166136261u;
    uint32_t prime = 16777619u;
    uint32_t size = packet->GetSize();
    if (size == 0) {
        return hash;
    }

    std::vector<uint8_t> buffer(size);
    packet->CopyData(buffer.data(), size);
    for (uint8_t byte : buffer) {
        hash ^= byte;
        hash *= prime;
    }
    return hash;
}

void NetworkCodingApp::QueuePendingPacket(const PendingPacketInfo& info, const std::string& context)
{
    m_pendingPackets.push_back(info);
    std::cout << "  [" << context << "] Node " << m_nodeId
              << " pending packets = " << m_pendingPackets.size()
              << "/" << m_generationSize << std::endl;
    EvaluatePendingPackets(context);
}

void NetworkCodingApp::EvaluatePendingPackets(const std::string& context)
{
    if (!m_networkCodingEnabled || m_pendingPackets.empty()) {
        if (!m_networkCodingEnabled) {
            FlushPendingPacketsUncoded(context);
        }
        return;
    }

    while (!m_pendingPackets.empty()) {
        if (m_generationSize == 0) {
            std::cout << "  Generation size is zero; forwarding pending packets uncoded" << std::endl;
            FlushPendingPacketsUncoded(context);
            return;
        }

        bool useNC = ShouldUseNetworkCoding();
        bool hasFullGeneration = m_pendingPackets.size() >= m_generationSize;

        std::cout << "  [NC DECISION] Node " << m_nodeId
                  << " context=" << context
                  << " useNC=" << useNC
                  << " pending=" << m_pendingPackets.size()
                  << "/" << m_generationSize << std::endl;

        if (useNC) {
            if (!hasFullGeneration) {
                std::cout << "  Waiting for full generation before coding" << std::endl;
                return;
            }
            PerformNetworkCoding();
            continue;
        }

        std::cout << "  NC guard disabled coding; forwarding pending packets uncoded" << std::endl;
        FlushPendingPacketsUncoded(context);
        return;
    }
}

void NetworkCodingApp::FlushPendingPacketsUncoded(const std::string& context)
{
    while (!m_pendingPackets.empty()) {
        PendingPacketInfo forwardInfo = m_pendingPackets.back();
        m_pendingPackets.pop_back();
        std::cout << "  [" << context << "] Forwarding pending packet uncoded" << std::endl;
        ForwardPacketNC(forwardInfo, false);
    }
}

bool NetworkCodingApp::ResolveNodeIdFromAddress(const Address& address, uint32_t& nodeId) const
{
    nodeId = kInvalidNodeId;

    if (InetSocketAddress::IsMatchingType(address)) {
        InetSocketAddress ipv4 = InetSocketAddress::ConvertFrom(address);
        nodeId = ResolveNodeIdFromIpv4(ipv4.GetIpv4());
    } else if (Inet6SocketAddress::IsMatchingType(address)) {
        return false;  // IPv6 not used in this scenario
    } else {
        return false;
    }

    return nodeId != kInvalidNodeId;
}

void NetworkCodingApp::EnableForwardingTxTracking()
{
    if (m_forwardingTracingEnabled || !m_networkCodingEnabled || m_isSource || m_isDestination) {
        return;
    }

    Ptr<Node> node = GetNode();
    if (!node) {
        return;
    }

    for (uint32_t i = 0; i < node->GetNDevices(); ++i) {
        Ptr<PointToPointNetDevice> device = DynamicCast<PointToPointNetDevice>(node->GetDevice(i));
        if (!device) {
            continue;
        }
        // No longer tracing MacTx/MacRx; retain hook for future extensions if needed
    }

    m_forwardingTracingEnabled = true;
}

Ipv4Address NetworkCodingApp::GetPrimaryIpv4() const
{
    return GetPrimaryIpv4(m_nodeId);
}

Ipv4Address NetworkCodingApp::GetPrimaryIpv4(uint32_t nodeId) const
{
    if (nodeId == kInvalidNodeId) {
        return Ipv4Address("0.0.0.0");
    }

    Ptr<Node> node = NodeList::GetNode(nodeId);
    if (!node) {
        return Ipv4Address("0.0.0.0");
    }

    Ptr<Ipv4> ipv4 = node->GetObject<Ipv4>();
    if (!ipv4) {
        return Ipv4Address("0.0.0.0");
    }

    for (uint32_t iface = 0; iface < ipv4->GetNInterfaces(); ++iface) {
        for (uint32_t addrIdx = 0; addrIdx < ipv4->GetNAddresses(iface); ++addrIdx) {
            Ipv4Address addr = ipv4->GetAddress(iface, addrIdx).GetLocal();
            if (addr != Ipv4Address::GetLoopback() && addr != Ipv4Address("0.0.0.0")) {
                return addr;
            }
        }
    }

    return Ipv4Address("0.0.0.0");
}

bool NetworkCodingApp::NodeOwnsAddress(const Ipv4Address& addr) const
{
    return NodeOwnsAddress(m_nodeId, addr);
}

bool NetworkCodingApp::NodeOwnsAddress(uint32_t nodeId, const Ipv4Address& addr) const
{
    if (nodeId == kInvalidNodeId || addr == Ipv4Address("0.0.0.0")) {
        return false;
    }

    Ptr<Node> node = NodeList::GetNode(nodeId);
    if (!node) {
        return false;
    }

    Ptr<Ipv4> ipv4 = node->GetObject<Ipv4>();
    if (!ipv4) {
        return false;
    }

    for (uint32_t iface = 0; iface < ipv4->GetNInterfaces(); ++iface) {
        for (uint32_t addrIdx = 0; addrIdx < ipv4->GetNAddresses(iface); ++addrIdx) {
            if (ipv4->GetAddress(iface, addrIdx).GetLocal() == addr) {
                return true;
            }
        }
    }

    return false;
}

bool NetworkCodingApp::ComputeHopDistance(uint32_t startNodeId, const Ipv4Address& destAddr, uint32_t& distance) const
{
    distance = 0;

    if (startNodeId == kInvalidNodeId || destAddr == Ipv4Address("0.0.0.0")) {
        return false;
    }

    if (NodeOwnsAddress(startNodeId, destAddr)) {
        return true;
    }

    std::set<uint32_t> visited;
    visited.insert(startNodeId);
    uint32_t currentNodeId = startNodeId;
    static const uint32_t kMaxHops = 128;

    for (uint32_t hop = 0; hop < kMaxHops; ++hop) {
        Ptr<Node> node = NodeList::GetNode(currentNodeId);
        if (!node) {
            return false;
        }

        Ptr<Ipv4> ipv4 = node->GetObject<Ipv4>();
        if (!ipv4) {
            return false;
        }

        Ptr<Ipv4RoutingProtocol> routing = ipv4->GetRoutingProtocol();
        if (!routing) {
            return false;
        }

        Ipv4Address sourceAddr = GetPrimaryIpv4(currentNodeId);
        if (sourceAddr == Ipv4Address("0.0.0.0")) {
            return false;
        }

        Ptr<Packet> probe = Create<Packet>(0);
        Ipv4Header header;
        header.SetSource(sourceAddr);
        header.SetDestination(destAddr);
        header.SetProtocol(17);  // UDP to match data plane

        Socket::SocketErrno err = Socket::ERROR_NOTERROR;
        Ptr<Ipv4Route> route = routing->RouteOutput(probe, header, nullptr, err);
        if (!route) {
            return false;
        }

        Ipv4Address nextHop = route->GetGateway();
        if (nextHop == Ipv4Address("0.0.0.0")) {
            nextHop = destAddr;
        }

        uint32_t nextNodeId = ResolveNodeIdFromIpv4(nextHop);
        if (nextNodeId == kInvalidNodeId) {
            nextNodeId = ResolveNodeIdFromIpv4(destAddr);
        }

        distance = hop + 1;

        if (nextNodeId == kInvalidNodeId) {
            return false;
        }

        if (!visited.insert(nextNodeId).second) {
            return false;
        }

        if (NodeOwnsAddress(nextNodeId, destAddr)) {
            return true;
        }

        currentNodeId = nextNodeId;
    }

    return false;
}

bool NetworkCodingApp::ComputeNextHop(const Ipv4Address& finalDest, Ipv4Address& nextHop) const
{
    nextHop = Ipv4Address("0.0.0.0");

    if (finalDest == Ipv4Address("0.0.0.0")) {
        return false;
    }

    Ptr<Node> node = GetNode();
    if (!node) {
        return false;
    }

    Ptr<Ipv4> ipv4 = node->GetObject<Ipv4>();
    if (!ipv4) {
        return false;
    }

    Ptr<Ipv4RoutingProtocol> routing = ipv4->GetRoutingProtocol();
    if (!routing) {
        return false;
    }

    Ipv4Header header;
    header.SetDestination(finalDest);
    header.SetSource(GetPrimaryIpv4());
    header.SetProtocol(17); // UDP

    Ptr<Packet> probe = Create<Packet>();
    Socket::SocketErrno err = Socket::ERROR_NOTERROR;
    Ptr<Ipv4Route> route = routing->RouteOutput(probe, header, nullptr, err);

    if (!route) {
        return false;
    }

    Ipv4Address gateway = route->GetGateway();
    if (gateway == Ipv4Address("0.0.0.0")) {
        nextHop = route->GetDestination();
        if (nextHop == Ipv4Address("0.0.0.0")) {
            nextHop = finalDest;
        }
    } else {
        nextHop = gateway;
    }

    return true;
}

bool NetworkCodingApp::SendPacketToNextHop(Ptr<Packet> packet, const Ipv4Address& finalDest, const std::string& context)
{
    if (!packet || !m_socket) {
        std::cout << "  Node " << m_nodeId << " cannot send packet - missing resources" << std::endl;
        return false;
    }

    if (NodeOwnsAddress(finalDest) || (m_isDestination && finalDest == m_destAddr)) {
        std::cout << "  Node " << m_nodeId << " already owns destination " << finalDest << " (" << context << ")" << std::endl;
        return false;
    }

    Ipv4Address nextHop;
    if (!ComputeNextHop(finalDest, nextHop)) {
        std::cout << "  Node " << m_nodeId << " failed to compute next hop for " << finalDest
                  << " (" << context << ")" << std::endl;
        return false;
    }

    TrackPacketFlowHop(m_nodeId, packet);

    int result = m_socket->SendTo(packet, 0, InetSocketAddress(nextHop, m_port));
    if (result < 0) {
        std::cout << "  Node " << m_nodeId << " send failure to " << nextHop
                  << " (" << context << ")" << std::endl;
        return false;
    }

    std::cout << "  Node " << m_nodeId << " sent packet toward " << finalDest
              << " via next hop " << nextHop << " (" << context << ")" << std::endl;
    return true;
}

class RandomGraphGenerator
{
public:
    static std::vector<std::pair<uint32_t, uint32_t>> GenerateRandomGraph(uint32_t numNodes, double edgeProb, uint32_t seed)
    {
        std::mt19937 rng(seed);
        std::uniform_real_distribution<double> dist(0.0, 1.0);
        
        std::vector<std::pair<uint32_t, uint32_t>> edges;
        
        // First, ensure connectivity with a spanning tree
        std::cout << "  Ensuring connectivity with spanning tree..." << std::endl;
        for (uint32_t i = 1; i < numNodes; i++) {
            uint32_t parent = rng() % i;  // Connect to random previous node
            edges.push_back({parent, i});
            std::cout << "    Spanning edge: " << parent << " <-> " << i << std::endl;
        }
        
        // Then add random edges
        std::cout << "  Adding random edges with probability " << edgeProb << "..." << std::endl;
        for (uint32_t i = 0; i < numNodes; i++) {
            for (uint32_t j = i + 1; j < numNodes; j++) {
                // Skip if already connected by spanning tree
                bool alreadyConnected = false;
                for (const auto& edge : edges) {
                    if ((edge.first == i && edge.second == j) || (edge.first == j && edge.second == i)) {
                        alreadyConnected = true;
                        break;
                    }
                }
                
                if (!alreadyConnected && dist(rng) < edgeProb) {
                    edges.push_back({i, j});
                    std::cout << "    Random edge: " << i << " <-> " << j << std::endl;
                }
            }
        }
        
        std::cout << "  Generated connected graph with " << edges.size() << " edges" << std::endl;
        return edges;
    }

    static std::pair<uint32_t, uint32_t> SelectRandomSourceDest(uint32_t numNodes, uint32_t seed)
    {
        std::mt19937 rng(seed + 1000);
        std::uniform_int_distribution<uint32_t> dist(0, numNodes - 1);
        
        uint32_t source = dist(rng);
        uint32_t dest;
        do {
            dest = dist(rng);
        } while (dest == source);
        
        return {source, dest};
    }
};

struct GraphScenario
{
    std::vector<std::pair<uint32_t, uint32_t>> edges;
    uint32_t sourceNode = 0;
    uint32_t destNode = 0;
    bool valid = false;
};

GraphScenario BuildGraphScenario(uint32_t numNodes, double edgeProb, uint32_t graphSeed)
{
    GraphScenario scenario;
    scenario.edges = RandomGraphGenerator::GenerateRandomGraph(numNodes, edgeProb, graphSeed);
    if (scenario.edges.empty()) {
        std::cout << "Warning: Generated graph has no edges" << std::endl;
        return scenario;
    }

    auto pair = RandomGraphGenerator::SelectRandomSourceDest(numNodes, graphSeed);
    scenario.sourceNode = pair.first;
    scenario.destNode = pair.second;

    if (!IsReachable(scenario.sourceNode, scenario.destNode, scenario.edges, numNodes)) {
        std::cout << "[CONNECTIVITY] Initial source " << scenario.sourceNode << " and destination "
                  << scenario.destNode << " are DISCONNECTED. Reselecting destination..." << std::endl;
        uint32_t newDest = SelectReachableNode(scenario.sourceNode, scenario.edges, numNodes, graphSeed);
        if (newDest == scenario.sourceNode) {
            std::cout << "[CONNECTIVITY] Failed to find reachable distinct destination. Aborting scenario." << std::endl;
            return scenario;
        }
        scenario.destNode = newDest;
        std::cout << "[CONNECTIVITY] New reachable destination selected: " << scenario.destNode << std::endl;
    } else {
        std::cout << "[CONNECTIVITY] Source " << scenario.sourceNode << " and destination "
                  << scenario.destNode << " are connected." << std::endl;
    }

    scenario.valid = true;
    return scenario;
}

NodeContainer CreateRandomTopology(const std::vector<std::pair<uint32_t, uint32_t>>& edges, 
                                  uint32_t numNodes, const SimulationParameters& params,
                                  NetDeviceContainer& devices, Ipv4InterfaceContainer& interfaces,
                                  uint32_t graphSeed, bool useNetworkCoding)
{
    std::cout << "\n=== Creating Random Graph Topology with OLSR ===" << std::endl;
    std::cout << "Nodes: " << numNodes << ", Edges: " << edges.size() << std::endl;
    std::cout << "Edge Probability: " << params.edgeProbability << std::endl;
    std::cout << "Data Rate: " << params.dataRate << "Mbps, Delay: " << params.linkDelay << "ms" << std::endl;
    
    NodeContainer nodes;
    nodes.Create(numNodes);
    
    std::cout << "\nNodes created (0-" << (numNodes-1) << "):" << std::endl;
    
    // First create all P2P links without internet stack
    PointToPointHelper p2p;
    p2p.SetDeviceAttribute("DataRate", StringValue(std::to_string(params.dataRate) + "Mbps"));
    p2p.SetChannelAttribute("Delay", StringValue(std::to_string(params.linkDelay) + "ms"));
    
    std::cout << "\nCreating point-to-point links:" << std::endl;
    
    // Install internet stack BEFORE creating network devices
    std::cout << "\nSetting up OLSR routing protocol..." << std::endl;
    OlsrHelper olsr;
    
    // Configure OLSR intervals: start reasonably fast, but not so aggressive that control traffic dominates
    olsr.Set("HelloInterval", TimeValue(Seconds(2.0)));
    olsr.Set("TcInterval", TimeValue(Seconds(4.0)));
    olsr.Set("MidInterval", TimeValue(Seconds(6.0)));
    
    Ipv4StaticRoutingHelper staticRouting;
    Ipv4ListRoutingHelper list;
    list.Add(staticRouting, 0);
    list.Add(olsr, 10);
    
    InternetStackHelper internet;
    internet.SetIpv6StackInstall(false); // Explicitly disable IPv6
    internet.SetRoutingHelper(list);
    internet.Install(nodes);
    std::cout << "OLSR routing installed on all nodes with aggressive convergence (IPv6 disabled)" << std::endl;
    
    // Ensure all nodes have at least one IP address for OLSR
    std::vector<bool> nodeHasInterface(nodes.GetN(), false);
    
    Ipv4AddressHelper ipv4;
    uint32_t subnetCounter = 1;
    
    // Create devices and assign IP addresses in one go
    for (const auto& edge : edges) {
        NodeContainer pair = NodeContainer(nodes.Get(edge.first), nodes.Get(edge.second));
        NetDeviceContainer link = p2p.Install(pair);
        devices.Add(link);
        
        std::string base = "10." + std::to_string(subnetCounter / 256 + 1) + "." + 
                          std::to_string(subnetCounter % 256) + ".0";
        ipv4.SetBase(base.c_str(), "255.255.255.252");
        
        Ipv4InterfaceContainer linkInterfaces = ipv4.Assign(link);
        interfaces.Add(linkInterfaces);
        
        // Mark these nodes as having interfaces
        nodeHasInterface[edge.first] = true;
        nodeHasInterface[edge.second] = true;
        
        std::cout << "  Node " << edge.first << " <-> Node " << edge.second 
                  << ": " << linkInterfaces.GetAddress(0) << " <-> " 
                  << linkInterfaces.GetAddress(1) << std::endl;
        
        subnetCounter++;
    }
    
    // Assign dummy loopback addresses to disconnected nodes for OLSR
    for (uint32_t i = 0; i < nodes.GetN(); i++) {
        if (!nodeHasInterface[i]) {
            // Create a dummy point-to-point link to itself (loopback-like)
            PointToPointHelper dummyP2p;
            dummyP2p.SetDeviceAttribute("DataRate", StringValue("1Mbps"));
            dummyP2p.SetChannelAttribute("Delay", StringValue("1ms"));
            
            NodeContainer selfPair = NodeContainer(nodes.Get(i), nodes.Get(i));
            NetDeviceContainer dummyLink = dummyP2p.Install(selfPair);
            devices.Add(dummyLink);
            
            std::string base = "192.168." + std::to_string(i + 1) + ".0";
            ipv4.SetBase(base.c_str(), "255.255.255.252");
            
            Ipv4InterfaceContainer dummyInterfaces = ipv4.Assign(dummyLink);
            interfaces.Add(dummyInterfaces);
            
            std::cout << "  Node " << i << " (disconnected): assigned dummy IP " 
                      << dummyInterfaces.GetAddress(0) << std::endl;
        }
    }
    
    std::cout << "Total links created: " << edges.size() << std::endl;
    
    // Create graph visualization file
    std::string methodTag = useNetworkCoding ? "nc" : "tcp";
    std::ostringstream graphFileBuilder;
    graphFileBuilder << "random_graph_n" << numNodes
                     << "_p" << static_cast<int>(params.edgeProbability * 100)
                     << "_seed" << graphSeed
                     << "_" << methodTag << ".dot";
    std::string graphFile = graphFileBuilder.str();
    CreateGraphVisualization(edges, numNodes, graphFile);
    std::cout << "Graph visualization saved to: " << graphFile << std::endl;
    
    return nodes;
}

std::map<uint32_t, std::set<uint32_t>> BuildNeighborMap(const std::vector<std::pair<uint32_t, uint32_t>>& edges, uint32_t numNodes)
{
    std::map<uint32_t, std::set<uint32_t>> neighbors;
    
    for (uint32_t i = 0; i < numNodes; i++) {
        neighbors[i] = std::set<uint32_t>();
    }
    
    for (const auto& edge : edges) {
        neighbors[edge.first].insert(edge.second);
        neighbors[edge.second].insert(edge.first);
    }
    
    return neighbors;
}

bool IsReachable(uint32_t source, uint32_t dest, const std::vector<std::pair<uint32_t,uint32_t>>& edges, uint32_t numNodes)
{
    if (source == dest) return true;
    std::vector<std::vector<uint32_t>> adj(numNodes);
    for (auto &e : edges) {
        adj[e.first].push_back(e.second);
        adj[e.second].push_back(e.first);
    }
    std::vector<bool> visited(numNodes,false);
    std::vector<uint32_t> queue;
    queue.push_back(source);
    visited[source] = true;
    for (size_t i=0;i<queue.size();++i) {
        uint32_t u = queue[i];
        for (auto v : adj[u]) {
            if (!visited[v]) {
                visited[v] = true;
                queue.push_back(v);
                if (v == dest) return true;
            }
        }
    }
    return visited[dest];
}

uint32_t SelectReachableNode(uint32_t source, const std::vector<std::pair<uint32_t,uint32_t>>& edges, uint32_t numNodes, uint32_t seed)
{
    std::mt19937 rng(seed + 7919); // offset so different from original selection
    std::uniform_int_distribution<uint32_t> dist(0, numNodes-1);
    for (uint32_t attempts=0; attempts < 100; ++attempts) {
        uint32_t candidate = dist(rng);
        if (candidate != source && IsReachable(source, candidate, edges, numNodes)) {
            return candidate;
        }
    }
    // fallback: return original if nothing reachable found
    return source;
}

void CreateGraphVisualization(const std::vector<std::pair<uint32_t, uint32_t>>& edges, 
                             uint32_t numNodes, const std::string& filename)
{
    std::ofstream dotFile(filename);
    if (!dotFile.is_open()) {
        std::cerr << "Error: Could not create graph visualization file: " << filename << std::endl;
        return;
    }
    
    dotFile << "graph RandomGraph {" << std::endl;
    dotFile << "  rankdir=LR;" << std::endl;
    dotFile << "  node [shape=circle, style=filled];" << std::endl;
    
    // Add all nodes
    for (uint32_t i = 0; i < numNodes; i++) {
        dotFile << "  " << i << " [label=\"" << i << "\"];" << std::endl;
    }
    
    // Add edges
    dotFile << "  // Edges" << std::endl;
    for (const auto& edge : edges) {
        dotFile << "  " << edge.first << " -- " << edge.second 
               << " [label=\"" << edge.first << "-" << edge.second << "\"];" << std::endl;
    }
    
    dotFile << "}" << std::endl;
    dotFile.close();
    
    std::cout << "To view the graph, use: dot -Tpng " << filename << " -o " 
             << filename.substr(0, filename.find_last_of('.')) << ".png" << std::endl;
}

void RegisterButterflyPatterns(uint32_t ownerNodeId, const std::vector<ButterflyInfo>& butterflies)
{
    (void)ownerNodeId; // owner already encoded in ButterflyInfo
    for (const auto& info : butterflies) {
        g_butterflySourceDestEdges.emplace(info.source, info.d1);
        g_butterflySourceDestEdges.emplace(info.source, info.d2);
    }
}

void ResetButterflyVisualizationState()
{
    g_butterflySourceDestEdges.clear();
}

void WriteCombinedButterflyGraph(uint32_t numNodes, uint32_t graphSeed)
{
    if (g_butterflySourceDestEdges.empty()) {
        std::cout << "No butterfly source/destination edges recorded; skipping combined graph." << std::endl;
        return;
    }

    std::ostringstream filename;
    filename << "butterflies_combined_n" << numNodes << "_seed" << graphSeed << ".dot";
    std::ofstream dotFile(filename.str());

    if (!dotFile.is_open()) {
        std::cout << "Failed to create combined butterfly graph file " << filename.str() << std::endl;
        return;
    }

    dotFile << "digraph ButterfliesCombined {" << std::endl;
    dotFile << "  rankdir=LR;" << std::endl;
    dotFile << "  node [shape=circle];" << std::endl;

    for (uint32_t i = 0; i < numNodes; ++i) {
        dotFile << "  n" << i << " [label=\"" << i << "\"];" << std::endl;
    }

    dotFile << "  // Butterfly source -> destination edges" << std::endl;
    for (const auto& edge : g_butterflySourceDestEdges) {
        dotFile << "  n" << edge.first << " -> n" << edge.second << ";" << std::endl;
    }

    dotFile << "}" << std::endl;
    dotFile.close();

    std::cout << "Combined butterfly visualization saved to " << filename.str() << std::endl;
}

NetworkStats RunSimulation(uint32_t numNodes, uint32_t graphSeed,
                          const SimulationParameters& params, bool useNetworkCoding,
                          const std::vector<std::pair<uint32_t, uint32_t>>& edges,
                          uint32_t sourceNode, uint32_t destNode)
{
    NetworkStats stats(useNetworkCoding ? "Network Coding" : "Traditional");
    stats.nodeCount = numNodes;
    stats.edgeCount = edges.size();

    if (useNetworkCoding) {
        ResetButterflyVisualizationState();
    }

    if (useNetworkCoding) {
        g_packetFlowPaths.clear();
    }

    if (edges.empty()) {
        std::cout << "Warning: Provided scenario has no edges" << std::endl;
        return stats;
    }
    
    std::cout << "\n=== SIMULATION SETUP ===" << std::endl;
    std::cout << "Running " << stats.method << " simulation" << std::endl;
    std::cout << "Nodes: " << numNodes << ", Edges: " << edges.size() << std::endl;
    std::cout << "SOURCE Node: " << sourceNode << std::endl;
    std::cout << "DESTINATION Node: " << destNode << std::endl;
    std::cout << "Random seed: " << graphSeed << std::endl;
    std::cout << "Network coding enabled: " << (useNetworkCoding ? "YES" : "NO") << std::endl;
    std::cout << std::string(50, '=') << std::endl;
    
    NetDeviceContainer devices;
    Ipv4InterfaceContainer interfaces;
    NodeContainer nodes = CreateRandomTopology(edges, numNodes, params, devices, interfaces, graphSeed, useNetworkCoding);
    
    // Setup applications
    std::vector<Ptr<NetworkCodingApp>> apps(numNodes);
    
    // Find the actual destination node's IPv4 address
    Ipv4Address destAddr;
    bool foundDestAddr = false;
    
    std::cout << "\nFinding destination address for Node " << destNode << "..." << std::endl;
    
    // Get the destination node's IPv4 interface directly
    Ptr<Node> destNodePtr = nodes.Get(destNode);
    Ptr<Ipv4> destIpv4 = destNodePtr->GetObject<Ipv4>();
    
    if (destIpv4) {
        std::cout << "Destination node " << destNode << " has " << destIpv4->GetNInterfaces() << " interfaces" << std::endl;
        // Find first non-loopback interface with valid IPv4 address
        for (uint32_t i = 1; i < destIpv4->GetNInterfaces(); i++) {
            if (destIpv4->IsUp(i) && destIpv4->GetNAddresses(i) > 0) {
                Ipv4Address addr = destIpv4->GetAddress(i, 0).GetLocal();
                // Ensure it's a valid IPv4 address, not loopback
                if (addr != Ipv4Address("127.0.0.1") && addr != Ipv4Address("0.0.0.0")) {
                    destAddr = addr;
                    foundDestAddr = true;
                    std::cout << "Found valid IPv4 destination Node " << destNode << " at address: " << destAddr << " (interface " << i << ")" << std::endl;
                    break;
                }
            }
        }
    }
    
    // Fallback: try to find any working address from interfaces container
    if (!foundDestAddr) {
        std::cout << "Direct node lookup failed, trying interfaces container..." << std::endl;
        for (uint32_t i = 0; i < interfaces.GetN(); i++) {
            Ipv4Address addr = interfaces.GetAddress(i);
            if (addr != Ipv4Address("127.0.0.1") && addr != Ipv4Address("0.0.0.0")) {
                destAddr = addr;
                foundDestAddr = true;
                std::cout << "Found address from interfaces: " << destAddr << " (index " << i << ")" << std::endl;
                break;
            }
        }
    }
    
    // Final fallback: use a working IPv4 address
    if (!foundDestAddr) {
        destAddr = Ipv4Address("192.168.1.2");  // Use a different default
        std::cout << "WARNING: Using hardcoded fallback destination address: " << destAddr << std::endl;
    }
    
    std::cout << "Final destination address to be used: " << destAddr << std::endl;

    if (g_logRoutingTables) {
        // Debug: print routing tables for source and destination shortly before transmission
        auto PrintRouting = [sourceNode, destNode]() {
            std::cout << "\n[ROUTING] Dumping routing tables for source=" << sourceNode << " and dest=" << destNode << " at " << Simulator::Now().GetSeconds() << "s" << std::endl;
            
            auto DumpFor = [](uint32_t nodeId, const std::string& label) {
                Ptr<Node> node = NodeList::GetNode(nodeId);
                if (!node) {
                    std::cout << "[ROUTING] Could not find node " << nodeId << std::endl;
                    return;
                }
                
                Ptr<Ipv4> ipv4 = node->GetObject<Ipv4>();
                if (!ipv4) {
                    std::cout << "[ROUTING] Node " << label << " does not have an IPv4 object." << std::endl;
                    return;
                }
                
                Ptr<Ipv4RoutingProtocol> rp = ipv4->GetRoutingProtocol();
                if (rp) {
                    // Use a Ptr<OutputStreamWrapper> to print the routing table
                    Ptr<OutputStreamWrapper> routingStream = Create<OutputStreamWrapper>(&std::cout);
                    std::cout << "--- Routing table for " << label << " (Node " << nodeId << ") ---" << std::endl;
                    rp->PrintRoutingTable(routingStream);
                    std::cout << "------------------------------------------" << std::endl;
                } else {
                    std::cout << "[ROUTING] Node " << label << " has no routing protocol." << std::endl;
                }
            };
            
            DumpFor(sourceNode, "SOURCE");
        };
        Simulator::Schedule(Seconds(55.0), PrintRouting);
    }
    
    for (uint32_t i = 0; i < numNodes; i++) {
        apps[i] = CreateObject<NetworkCodingApp>();
        apps[i]->Setup(i, i == sourceNode, i == destNode, destAddr, 9999,
                      params.packetSize, params.generationSize, params.totalPackets, useNetworkCoding);
        
        nodes.Get(i)->AddApplication(apps[i]);
        apps[i]->SetStartTime(Seconds(25.0));  // Much longer delay for OLSR convergence
        apps[i]->SetStopTime(Seconds(params.simulationTime));
    }
    
    // Enable tracing
    AsciiTraceHelper ascii;
    std::string traceFile = params.outputPrefix + "_" + stats.method + "_n" + 
                           std::to_string(numNodes) + "_s" + std::to_string(graphSeed) + ".tr";
    PointToPointHelper p2p;
    p2p.EnableAsciiAll(ascii.CreateFileStream(traceFile));
    
    FlowMonitorHelper flowHelper;
    Ptr<FlowMonitor> flowMonitor = flowHelper.InstallAll();
    
    Time startTime = Simulator::Now();
    
    Simulator::Stop(Seconds(params.simulationTime));
    Simulator::Run();
    
    Time endTime = Simulator::Now();
    stats.totalTime = (endTime - startTime).GetSeconds();
    
    // Collect statistics
    std::cout << "\n=== COLLECTING SIMULATION STATISTICS ===" << std::endl;
    
    uint32_t totalAppTransmissions = 0;
    uint32_t totalNCTransmissions = 0;
    uint32_t totalButterflyXorStarts = 0;
    bool destinationDecoded = false;
    
    for (uint32_t i = 0; i < numNodes; i++) {
        uint32_t nodeTx = apps[i]->GetPacketsSent();
        uint32_t nodeRx = apps[i]->GetPacketsReceived();
        totalAppTransmissions += nodeTx;
        
        if (useNetworkCoding) {
            uint32_t nodeNC = apps[i]->GetNCTransmissions();
            uint32_t nodeXorStarts = apps[i]->GetButterflyXorActivations();
            totalNCTransmissions += nodeNC;
            totalButterflyXorStarts += nodeXorStarts;
            std::cout << "  Node " << i << ": TX=" << nodeTx << ", RX=" << nodeRx 
                     << ", NC=" << nodeNC
                     << ", XORstarts=" << nodeXorStarts << std::endl;
        } else {
            std::cout << "  Node " << i << ": TX=" << nodeTx << ", RX=" << nodeRx << std::endl;
        }
        
        if (i == destNode && apps[i]->IsDecoded()) {
            destinationDecoded = true;
        }
    }

    stats.totalTransmissions = totalAppTransmissions;
    if (useNetworkCoding) {
        stats.ncTransmissions = totalNCTransmissions;
        stats.butterflyXorStarts = totalButterflyXorStarts;
    }
    
    if (destinationDecoded) {
        stats.successfulDecodings = 1;
        std::cout << "\n*** SUCCESS: Destination node " << destNode << " decoded successfully! ***" << std::endl;
    } else {
        std::cout << "\n*** WARNING: Destination node " << destNode << " did not decode ***" << std::endl;
    }
    
    std::cout << "Total application transmissions: " << totalAppTransmissions << std::endl;
    if (useNetworkCoding) {
        std::cout << "Network coding transmissions: " << totalNCTransmissions << std::endl;
    }
    
    flowMonitor->CheckForLostPackets();
    auto flowStats = flowMonitor->GetFlowStats();
    
    uint32_t totalTx = 0, totalRx = 0, totalLost = 0;
    double totalDelay = 0;
    uint32_t delayCount = 0;
    Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier>(flowHelper.GetClassifier());
    uint64_t appPortTx = 0;
    uint64_t appPortRx = 0;
    uint64_t appPortRxBytes = 0;
    
    for (auto& flow : flowStats) {
        totalTx += flow.second.txPackets;
        totalRx += flow.second.rxPackets;
        totalLost += flow.second.lostPackets;

        if (flow.second.rxPackets > 0) {
            totalDelay += flow.second.delaySum.GetSeconds();
            delayCount += flow.second.rxPackets;
        }

        if (classifier) {
            Ipv4FlowClassifier::FiveTuple tuple = classifier->FindFlow(flow.first);
            bool matchesPort = tuple.destinationPort == kNcAppPort;
            bool matchesProtocol = useNetworkCoding ? (tuple.protocol == 17) : (tuple.protocol == 6);
            if (matchesPort && matchesProtocol) {
                appPortTx += flow.second.txPackets;
                appPortRx += flow.second.rxPackets;
                appPortRxBytes += flow.second.rxBytes;
            }
        }
    }
    
    stats.packetLossRate = totalTx > 0 ? (double)totalLost / totalTx : 0;
    stats.averageDelay = delayCount > 0 ? totalDelay / delayCount : 0;
    stats.throughput = stats.totalTime > 0 ? (totalRx * params.packetSize * 8) / stats.totalTime : 0;
    
    if (!useNetworkCoding) {
        if (appPortTx > 0) {
            stats.totalTransmissions = appPortTx;
            std::cout << "FlowMonitor TCP transmissions (port " << kNcAppPort << "): TX="
                      << appPortTx << ", RX=" << appPortRx << std::endl;
        } else {
            std::cout << "FlowMonitor did not observe TCP packets on port " << kNcAppPort
                      << "; using application counters." << std::endl;
        }
    }

    if (useNetworkCoding) {
        stats.butterflyUsageCount = stats.ncTransmissions;
        stats.bottleneckUsage = stats.butterflyUsageCount;
        std::cout << "Butterfly XOR activations: " << stats.butterflyXorStarts << std::endl;
    } else {
        stats.bottleneckUsage = 0;
    }

    uint64_t deliveredPackets = appPortRx > 0 ? appPortRx : (destinationDecoded ? params.totalPackets : 0);
    uint64_t deliveredBytes = appPortRxBytes > 0 ? appPortRxBytes : (deliveredPackets * params.packetSize);

    if (params.totalPackets > 0) {
        stats.successRate = std::min<double>(1.0, static_cast<double>(deliveredPackets) / params.totalPackets);
    }
    stats.goodput = stats.totalTime > 0 ? (deliveredBytes * 8) / stats.totalTime : 0;
    
    // Print packet flow summary
    std::cout << "\n=== PACKET FLOW SUMMARY ===" << std::endl;
    std::cout << "Source node " << sourceNode << " -> Destination node " << destNode << std::endl;
    std::cout << "Total network transmissions: " << stats.totalTransmissions << std::endl;
    if (useNetworkCoding) {
        std::cout << "Network coding transmissions: " << stats.ncTransmissions << std::endl;
        std::cout << "Regular transmissions: " << (stats.totalTransmissions - stats.ncTransmissions) << std::endl;
        std::cout << "Butterfly patterns utilized: " << stats.butterflyUsageCount << std::endl;
        if (stats.ncTransmissions > 0) {
            std::cout << "Network coding efficiency: " << std::fixed << std::setprecision(1) 
                     << (100.0 * stats.ncTransmissions / stats.totalTransmissions) << "% of transmissions" << std::endl;
        }
        std::cout << "Butterfly XOR activations: " << stats.butterflyXorStarts << std::endl;
    }
    std::cout << "Destination decode status: " << (destinationDecoded ? "SUCCESS" : "FAILED") << std::endl;
    std::cout << std::string(50, '=') << std::endl;
    
    if (useNetworkCoding) {
        if (!g_packetFlowPaths.empty()) {
            std::cout << "\n=== OBSERVED PACKET FLOW PATHS ===" << std::endl;
            for (const auto& entry : g_packetFlowPaths) {
                std::cout << "  PacketUID " << entry.first << ": ";
                for (size_t idx = 0; idx < entry.second.size(); ++idx) {
                    if (idx > 0) {
                        std::cout << " -> ";
                    }
                    std::cout << "Node " << entry.second[idx];
                }
                std::cout << std::endl;
            }
        }

        WriteCombinedButterflyGraph(numNodes, graphSeed);
    }

    Simulator::Destroy();
    return stats;
}

void WriteCsvResults(const std::string& filename,
                     const SimulationParameters& params,
                     const std::vector<std::tuple<uint32_t, uint32_t, NetworkStats, NetworkStats>>& results)
{
    if (filename.empty() || results.empty()) {
        return;
    }

    bool fileExists = std::ifstream(filename).good();
    std::ofstream outFile(filename, std::ios_base::app);
    if (!outFile.is_open()) {
        std::cerr << "Error: Could not open CSV file: " << filename << std::endl;
        return;
    }

    if (!fileExists) {
        outFile << "nodeCount,edgeCount,graphSeed,packetSize,generationSize,totalPackets,"
                << "edgeProbability,dataRateMbps,linkDelayMs,"
                << "tcpTime,ncTime,tcpTx,ncTx,tcpBottleneck,ncBottleneck,"
                << "tcpSuccessRate,ncSuccessRate,tcpAvgDelay,ncAvgDelay,"
                << "tcpThroughput,ncThroughput,tcpGoodput,ncGoodput,"
                << "tcpLossRate,ncLossRate,tcpSuccesses,ncSuccesses,"
                << "ncButterflyUsage,ncXorStarts\n";
    }

    for (const auto& [nodeCount, graphSeed, tcpStats, ncStats] : results) {
        outFile << nodeCount << ","
                << tcpStats.edgeCount << ","
                << graphSeed << ","
                << params.packetSize << ","
                << params.generationSize << ","
                << params.totalPackets << ","
                << params.edgeProbability << ","
                << params.dataRate << ","
                << params.linkDelay << ","
                << tcpStats.totalTime << ","
                << ncStats.totalTime << ","
                << tcpStats.totalTransmissions << ","
                << ncStats.totalTransmissions << ","
                << tcpStats.bottleneckUsage << ","
                << ncStats.bottleneckUsage << ","
                << tcpStats.successRate << ","
                << ncStats.successRate << ","
                << tcpStats.averageDelay << ","
                << ncStats.averageDelay << ","
                << tcpStats.throughput << ","
                << ncStats.throughput << ","
                << tcpStats.goodput << ","
                << ncStats.goodput << ","
                << tcpStats.packetLossRate << ","
                << ncStats.packetLossRate << ","
                << tcpStats.successfulDecodings << ","
                << ncStats.successfulDecodings << ","
                << ncStats.butterflyUsageCount << ","
                << ncStats.butterflyXorStarts << "\n";
    }

    outFile.close();
    std::cout << "Results written to " << filename << std::endl;
}

void PrintSummary(const std::vector<std::tuple<uint32_t, uint32_t, NetworkStats, NetworkStats>>& results)
{
    std::cout << "\n" << std::string(80, '=') << std::endl;
    std::cout << "SIMULATION SUMMARY" << std::endl;
    std::cout << std::string(80, '=') << std::endl;
    
    std::cout << std::left << std::setw(8) << "Nodes"
              << std::setw(8) << "Edges" 
              << std::setw(12) << "TCP Tx"
              << std::setw(12) << "NC Tx"
              << std::setw(12) << "CodingGain"
              << std::setw(12) << "Butterflies"
              << std::setw(12) << "XORStarts" << std::endl;
    std::cout << std::string(76, '-') << std::endl;
    
    for (const auto& [nodeCount, graphSeed, tcpStats, ncStats] : results) {
        double codingGain = (ncStats.totalTransmissions > 0)
            ? static_cast<double>(tcpStats.totalTransmissions) / ncStats.totalTransmissions
            : 1.0;
        
        std::cout << std::left << std::setw(8) << nodeCount
                  << std::setw(8) << tcpStats.edgeCount
                  << std::setw(12) << tcpStats.totalTransmissions
                  << std::setw(12) << ncStats.totalTransmissions
                  << std::setw(12) << std::fixed << std::setprecision(2) << codingGain
                  << std::setw(12) << ncStats.butterflyUsageCount
                  << std::setw(12) << ncStats.butterflyXorStarts << std::endl;
    }
}

int main(int argc, char* argv[])
{
    SimulationParameters params;
    
    CommandLine cmd(__FILE__);
    cmd.AddValue("minNodes", "Minimum number of nodes", params.minNodes);
    cmd.AddValue("maxNodes", "Maximum number of nodes", params.maxNodes);
    cmd.AddValue("edgeProb", "Edge probability for random graph", params.edgeProbability);
    cmd.AddValue("seed", "Random seed", params.randomSeed);
    cmd.AddValue("numGraphs", "Number of graphs per node size", params.numGraphsPerSize);
    cmd.AddValue("verbose", "Enable verbose output", params.verbose);
    cmd.Parse(argc, argv);
    
    std::cout << "Random Graph Butterfly Network Coding Simulation" << std::endl;
    std::cout << "Seed: " << params.randomSeed << std::endl;
    std::cout << "Node range: " << params.minNodes << "-" << params.maxNodes << std::endl;
    std::cout << "Edge probability: " << params.edgeProbability << std::endl;
    
    std::vector<std::tuple<uint32_t, uint32_t, NetworkStats, NetworkStats>> results;
    
    for (uint32_t nodeCount = params.minNodes; nodeCount <= params.maxNodes; nodeCount += 5) {
        for (uint32_t graphIdx = 0; graphIdx < params.numGraphsPerSize; graphIdx++) {
            uint32_t graphSeed = params.randomSeed + nodeCount * 1000 + graphIdx;
            
            GraphScenario scenario = BuildGraphScenario(nodeCount, params.edgeProbability, graphSeed);
            if (!scenario.valid) {
                std::cout << "Skipping graph seed " << graphSeed << " for node count " << nodeCount << " (invalid scenario)" << std::endl;
                continue;
            }

            std::cout << "\n[SCENARIO] Reusing generated graph for both TCP and NC runs (seed=" << graphSeed
                      << ", nodes=" << nodeCount << ")" << std::endl;

            NetworkStats tcpStats = RunSimulation(nodeCount, graphSeed, params, false,
                                                 scenario.edges, scenario.sourceNode, scenario.destNode);
            NetworkStats ncStats = RunSimulation(nodeCount, graphSeed, params, true,
                                                scenario.edges, scenario.sourceNode, scenario.destNode);
            
            results.emplace_back(nodeCount, graphSeed, tcpStats, ncStats);

            double codingGain = (ncStats.totalTransmissions > 0)
                ? static_cast<double>(tcpStats.totalTransmissions) / ncStats.totalTransmissions
                : 1.0;
            std::cout << "[SCENARIO] Coding gain (TCP/NC transmissions): "
                      << std::fixed << std::setprecision(2) << codingGain << std::endl;
            
            if (params.verbose) {
                std::cout << "Node " << nodeCount << ", Graph " << graphIdx 
                         << " - TCP: " << tcpStats.totalTransmissions 
                         << ", NC: " << ncStats.totalTransmissions 
                         << ", Butterflies: " << ncStats.butterflyUsageCount << std::endl;
            }
        }
    }
    
    PrintSummary(results);
    WriteCsvResults("random_graph_results.csv", params, results);
    return 0;
}