#ifndef NC_EXPERIMENT_METRICS_H
#define NC_EXPERIMENT_METRICS_H

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include <fstream>
#include <iomanip>
#include <map>
#include <string>

namespace ncmetrics {
using namespace ns3;

struct Summary {
  uint64_t frames=0, bytes=0, dataFrames=0, dataBytes=0, controlFrames=0, controlBytes=0;
  uint64_t routingFrames=0, routingBytes=0, preFrames=0, preBytes=0, drops=0, injectedDrops=0;
  uint64_t centralFrames=0, centralBytes=0, usefulBytes=0;
  uint32_t completed=0, required=0, xorOperations=0;
  double offer=-1, deadline=0, completion=-1, windowGoodput=0, completionGoodput=0;
};

// PhyTxBegin counts actual PPP frames started on each directed point-to-point
// link, including retransmissions. Summed bytes are traffic cost, not throughput.
class TransferMetrics {
public:
  TransferMetrics(uint32_t required, double deadline, const std::string& trace="", bool central=true)
    : m_required(required), m_deadline(deadline), m_central(central) {
    if (!trace.empty()) {
      m_trace.open(trace);
      NS_ABORT_MSG_IF(!m_trace, "Cannot write event trace " << trace);
      m_trace << "time_s,source,destination,bytes,kind,phase\n" << std::setprecision(17);
    }
  }
  void Install(const NodeContainer& nodes) {
    for (auto it=nodes.Begin();it!=nodes.End();++it) {
      auto node=*it;
      for (uint32_t i=0;i<node->GetNDevices();++i) {
        auto device=DynamicCast<PointToPointNetDevice>(node->GetDevice(i));
        if (!device) continue;
        auto channel=device->GetChannel();
        uint32_t peer=channel->GetDevice(0)->GetNode()->GetId();
        if (peer==node->GetId()) peer=channel->GetDevice(1)->GetNode()->GetId();
        NS_ABORT_MSG_IF(!device->TraceConnectWithoutContext("PhyTxBegin",
          MakeBoundCallback(&TransferMetrics::ObserveTx,this,node->GetId(),peer)),"Missing Tx trace");
        NS_ABORT_MSG_IF(!device->TraceConnectWithoutContext("PhyRxDrop",
          MakeCallback(&TransferMetrics::ObserveDrop,this)),"Missing drop trace");
      }
    }
  }
  void RecordXor() { ++m_counts.xorOperations; }
  void InjectDrop() { ++m_counts.injectedDrops; }
  void Offer() { if (m_offer<0) m_offer=Simulator::Now().GetSeconds(); }
  void Deliver(uint32_t destination, uint64_t uniqueBytes) {
    m_delivered[destination]=std::max(m_delivered[destination],uniqueBytes);
  }
  void Complete(uint32_t destination, uint64_t uniqueBytes) {
    Deliver(destination,uniqueBytes);
    m_completed.emplace(destination,Simulator::Now().GetSeconds());
  }
  Summary GetSummary() const {
    Summary s=m_counts; s.offer=m_offer; s.deadline=m_deadline;
    s.required=m_required; s.completed=m_completed.size();
    for (auto [id,bytes]:m_delivered) s.usefulBytes+=bytes;
    if (s.completed==m_required && m_offer>=0) {
      double latest=m_offer;
      for (auto [id,time]:m_completed) latest=std::max(latest,time);
      s.completion=latest-m_offer;
      if (s.completion>0) s.completionGoodput=8.0*s.usefulBytes/s.completion;
    }
    if (m_offer>=0 && m_deadline>m_offer) s.windowGoodput=8.0*s.usefulBytes/(m_deadline-m_offer);
    return s;
  }
  void Write(const std::string& path, const std::string& experiment, const std::string& method,
             uint32_t nodes, uint32_t edges, uint32_t graphSeed, const std::string& rate,
             double errorRate, uint32_t source, const std::string& destinations, const std::string& injection="none") const {
    const std::string header="schema,experiment,method,nodes,edges,source,destinations,graph_seed,rng_seed,rng_run,central_rate,error_rate,offer_s,deadline_s,completed,required,success,useful_bytes,completion_s,window_goodput_bps,completion_goodput_bps,transfer_frames,transfer_bytes,data_frames,data_bytes,control_frames,control_bytes,routing_frames,routing_bytes,pre_frames,pre_bytes,central_frames,central_bytes,phy_rx_drops,injected_app_drops,validation_drop,xor_operations";
    std::ifstream existing(path); std::string old;
    if (std::getline(existing,old)) NS_ABORT_MSG_IF(old!=header,"CSV schema mismatch; use a new file: " << path);
    bool empty=old.empty(); existing.close();
    std::ofstream out(path,std::ios::app);
    NS_ABORT_MSG_IF(!out,"Cannot write metrics " << path);
    if (empty) out << header << '\n';
    auto s=GetSummary(); out << std::setprecision(17);
    out << "2," << experiment << ',' << method << ',' << nodes << ',' << edges << ',' << source << ',' << destinations << ',' << graphSeed << ','
        << RngSeedManager::GetSeed() << ',' << RngSeedManager::GetRun() << ',' << rate << ',' << errorRate << ','
        << s.offer << ',' << s.deadline << ',' << s.completed << ',' << s.required << ','
        << (s.completed==s.required) << ',' << s.usefulBytes << ',';
    if (s.completion>=0) out << s.completion;
    out << ',' << s.windowGoodput << ',';
    if (s.completion>=0) out << s.completionGoodput;
    out << ',' << s.frames << ',' << s.bytes << ',' << s.dataFrames << ',' << s.dataBytes << ','
        << s.controlFrames << ',' << s.controlBytes << ',' << s.routingFrames << ',' << s.routingBytes << ','
        << s.preFrames << ',' << s.preBytes << ',';
    if (m_central) out << s.centralFrames;
    out << ','; if (m_central) out << s.centralBytes;
    out << ',' << s.drops << ',' << s.injectedDrops << ',' << injection << ',' << s.xorOperations << '\n';
  }
private:
  static void ObserveTx(TransferMetrics* self,uint32_t source,uint32_t destination,Ptr<const Packet> packet) {
    std::string kind="other";
    auto copy=packet->Copy(); PppHeader ppp;
    if (copy->RemoveHeader(ppp)>0 && ppp.GetProtocol()==0x0021) {
      Ipv4Header ip; copy->RemoveHeader(ip);
      if (ip.GetProtocol()==6) {
        TcpHeader tcp; copy->RemoveHeader(tcp);
        kind=copy->GetSize()>0 ? "data" : "control";
      } else if (ip.GetProtocol()==17) {
        UdpHeader udp; copy->RemoveHeader(udp);
        kind=(udp.GetDestinationPort()==698 || udp.GetSourcePort()==698) ? "routing" :
             (copy->GetSize()==15 ? "control" : "data");
      }
    }
    if (kind=="other") kind="control";
    bool active=self->m_offer>=0;
    auto& s=self->m_counts; uint64_t bytes=packet->GetSize();
    if (!active) { ++s.preFrames; s.preBytes+=bytes; }
    else {
      ++s.frames; s.bytes+=bytes;
      if (kind=="data") { ++s.dataFrames; s.dataBytes+=bytes; }
      else if (kind=="routing") { ++s.routingFrames; s.routingBytes+=bytes; }
      else { ++s.controlFrames; s.controlBytes+=bytes; }
      if (self->m_central && source==3 && destination==4) { ++s.centralFrames; s.centralBytes+=bytes; }
    }
    if (self->m_trace) self->m_trace << Simulator::Now().GetSeconds() << ',' << source << ',' << destination
       << ',' << bytes << ',' << kind << ',' << (active?"transfer":"pre") << '\n';
  }
  void ObserveDrop(Ptr<const Packet>) { if (m_offer>=0) ++m_counts.drops; }
  uint32_t m_required;
  double m_deadline, m_offer=-1;
  bool m_central;
  Summary m_counts;
  std::map<uint32_t,uint64_t> m_delivered;
  std::map<uint32_t,double> m_completed;
  std::ofstream m_trace;
};
inline TransferMetrics* active=nullptr;
}
#endif
