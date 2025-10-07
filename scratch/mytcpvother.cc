
#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/internet-module.h"
#include "ns3/ipv4-global-routing-helper.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/config-store.h"
#include "ns3/mytcp.h"

#include "ns3/file-helper.h"

#include <cassert>
#include <fstream>
#include <iostream>
#include <string>

/*
    Topology:

    n1----n0---n2
          \
           \---n3

    n1 is the source of the MyTcp flow and n2 is the source of the other TCP flow
*/
using namespace ns3;

NS_LOG_COMPONENT_DEFINE("MyTcpVFixed");

namespace {

const int MAX_NODES = 4;

Time lastReceivedTime[MAX_NODES];
ssize_t remainingBytes[MAX_NODES] = {};
ssize_t seenBytes[MAX_NODES] = {};
int flowsNotComplete = 0;
NodeContainer nodes;
int maxTime = 120;
bool tracingEnabled = false;
int seenSendSocketsFor[MAX_NODES] = {};
int seenRecvSocketsFor[MAX_NODES] = {};
Ipv4Address addressesFor[MAX_NODES] = {};
std::ofstream cwndOut[MAX_NODES];
std::ofstream ssthreshOut[MAX_NODES];
std::string linkBandwidth = "5Mbps";

void
MyOutputValue(std::ostream *out, int socketIndex, uint32_t oldval, uint32_t newval) {
    *out << (Simulator::Now().GetNanoSeconds() - 1) << "," << socketIndex << "," << oldval << "\n";
    *out << Simulator::Now().GetNanoSeconds() << "," << socketIndex << "," << newval << "\n";
    out->flush();
}

void SetupTcpTracersFor(int nodeIndex, int socketIndex)
{
    NS_LOG_INFO("Setting up TCP tracers for node " << nodeIndex << " socket " << socketIndex);
    cwndOut[nodeIndex].open("mytcpvother-cwnd-" + std::to_string(nodeIndex) + ".csv");
    ssthreshOut[nodeIndex].open("mytcpvother-ssthresh-" + std::to_string(nodeIndex) + ".csv");
    Config::ConnectWithoutContext("/NodeList/" + std::to_string(nodeIndex) +
        "/$ns3::TcpL4Protocol/SocketList/" + std::to_string(socketIndex) + "/CongestionWindow",
        MakeBoundCallback(&MyOutputValue, &cwndOut[nodeIndex], socketIndex)
    );
    Config::ConnectWithoutContext("/NodeList/" + std::to_string(nodeIndex) +
        "/$ns3::TcpL4Protocol/SocketList/" + std::to_string(socketIndex) + "/SlowStartThreshold",
        MakeBoundCallback(&MyOutputValue, &ssthreshOut[nodeIndex], socketIndex)
    );
}

void SetupTopology() {
    NS_LOG_INFO("Create nodes.");
    nodes.Create(4);
    NodeContainer n01;
    n01.Add(nodes.Get(0));
    n01.Add(nodes.Get(1));
    NodeContainer n02;
    n02.Add(nodes.Get(0));
    n02.Add(nodes.Get(2));
    NodeContainer n03;
    n03.Add(nodes.Get(0));
    n03.Add(nodes.Get(3));

    NS_LOG_INFO("Create channels.");

    // 10-packet deep queues
    Config::SetDefault(
        "ns3::DropTailQueue<Packet>::MaxSize",
        QueueSizeValue(QueueSize(QueueSizeUnit::PACKETS, 10))
    );

    //
    // Install the internet stack on the nodes
    //
    InternetStackHelper internet;
    internet.Install(nodes);

    //
    // Explicitly create the point-to-point link required by the topology (shown above).
    //
    PointToPointHelper pointToPoint;
    NetDeviceContainer devices01;
    pointToPoint.SetDeviceAttribute("DataRate", StringValue(linkBandwidth));
    pointToPoint.SetChannelAttribute("Delay", StringValue("5ms"));
    devices01 = pointToPoint.Install(n01);
    NetDeviceContainer devices02;
    pointToPoint.SetDeviceAttribute("DataRate", StringValue(linkBandwidth));
    pointToPoint.SetChannelAttribute("Delay", StringValue("5ms"));
    devices02 = pointToPoint.Install(n02);
    NetDeviceContainer devices03;
    pointToPoint.SetDeviceAttribute("DataRate", StringValue(linkBandwidth));
    pointToPoint.SetChannelAttribute("Delay", StringValue("5ms"));
    devices03 = pointToPoint.Install(n03);

    Ipv4AddressHelper ipv4;
    ipv4.SetBase("10.1.1.0", "255.255.255.0");
    Ipv4InterfaceContainer i01 = ipv4.Assign(devices01);
    addressesFor[1] = i01.GetAddress(1);
    ipv4.SetBase("10.1.2.0", "255.255.255.0");
    Ipv4InterfaceContainer i02 = ipv4.Assign(devices02);
    addressesFor[2] = i02.GetAddress(1);
    ipv4.SetBase("10.1.3.0", "255.255.255.0");
    Ipv4InterfaceContainer i03 = ipv4.Assign(devices03);
    addressesFor[3] = i03.GetAddress(1);

    Ipv4GlobalRoutingHelper::PopulateRoutingTables();

    if (tracingEnabled) {
        AsciiTraceHelper ascii;
        pointToPoint.EnableAsciiAll(ascii.CreateFileStream("mytcpvother.tr"));
        pointToPoint.EnablePcapAll("mytcpvother", false);
    }
}

void SetDefaultTcpEngine(const std::string &tcpEngine) {
    Config::Set(
        "/NodeList/*/$ns3::TcpL4Protocol/SocketType", StringValue(tcpEngine)
    );
}

void SetNodeTcpEngine(int whichNode, const std::string &tcpEngine) {
    Config::Set(
        "/NodeList/" + std::to_string(whichNode) + "/$ns3::TcpL4Protocol/SocketType",
        StringValue(tcpEngine)
    );
}

void RecordReceivedTcp(int id, const Ptr<const Packet> orig_packet, const Address &address) {
    NS_LOG_FUNCTION(id << orig_packet << address);
    lastReceivedTime[id] = Simulator::Now();
    seenBytes[id] += orig_packet->GetSize();
    if (remainingBytes[id] > 0) {
        remainingBytes[id] -= orig_packet->GetSize();
        if (remainingBytes[id] <= 0) {
            NS_LOG_INFO("Received last expected bytes for " << id);
            flowsNotComplete -= 1;
            if (flowsNotComplete == 0) {
                NS_LOG_INFO("Scheduling simulator stop at " << Simulator::Now());
                Simulator::Stop(Seconds(0.1));
            }
        }
    }
}

void RecordReceivedUdp(int id, const Ptr<const Packet> orig_packet) {
    NS_LOG_FUNCTION(id << orig_packet);
    lastReceivedTime[id] = Simulator::Now();
    seenBytes[id] += orig_packet->GetSize();
}


void CreateTcpFlow(int fromNode, int toNode, int toPort, uint32_t maxBytes) {

    // this will not work properly on destination devices with multiple IPs
    // or with no IP address assigned
    assert(1 == nodes.Get(toNode)->GetNDevices());

    Ipv4Address toNodeAddress = addressesFor[toNode];

    NS_LOG_INFO("CreateTcpFlow: " << fromNode << " -> " << toNode << "(" << toNodeAddress << ":" << toPort << ") : sending " << maxBytes);

    BulkSendHelper source("ns3::TcpSocketFactory", InetSocketAddress(toNodeAddress, toPort));
    source.SetAttribute("MaxBytes", UintegerValue(maxBytes));
    ApplicationContainer sourceApps = source.Install(nodes.Get(fromNode));
    sourceApps.Start(Seconds(0));
    sourceApps.Stop(Seconds(maxTime));

    PacketSinkHelper sink("ns3::TcpSocketFactory", InetSocketAddress(Ipv4Address::GetAny(), toPort));
    ApplicationContainer sinkApps = sink.Install(nodes.Get(toNode));
    sinkApps.Start(Seconds(0));
    sinkApps.Stop(Seconds(maxTime));

    // Setup a callback to start logging cwnd/sstresh changes after the socket is setup
    // A better way to do this would be a modified version of BulkSendHelper.
    if (tracingEnabled) {
        Simulator::Schedule(Seconds(1e-6), MakeBoundCallback(&SetupTcpTracersFor, fromNode, seenSendSocketsFor[fromNode]));
    }
    seenSendSocketsFor[fromNode] += 1;
    
    Config::ConnectWithoutContext(
        "/NodeList/" + std::to_string(toNode) + "/ApplicationList/" +
        std::to_string(seenRecvSocketsFor[toNode]) + "/Rx",
        MakeBoundCallback(&RecordReceivedTcp, fromNode)
    );

    seenRecvSocketsFor[toNode] += 1;

    remainingBytes[fromNode] = maxBytes;
    flowsNotComplete += 1;
}


void CreateUdpFlow(int fromNode, int toNode, int toPort, int bytesPerSec) {
    NS_LOG_INFO("Setup UDP flow " << fromNode << " -> " << toNode << " @ " << bytesPerSec);
    UdpServerHelper server(toPort);
    ApplicationContainer serverApps = server.Install(nodes.Get(toNode));
    serverApps.Start(Seconds(0));
    serverApps.Stop(Seconds(maxTime));

    UdpClientHelper client(addressesFor[toNode], toPort);
    const uint64_t packetSize = 1024;
    // 32 byte estimated overhead for ethernet + ip + udp headers
    float packetsPerSec = (float) bytesPerSec / (packetSize + 32);
    Time packetInterval{Seconds(std::min(1.0 / packetsPerSec, (double) maxTime))};
    uint64_t packetCount = ((uint64_t) packetsPerSec) * maxTime;
    client.SetAttribute("MaxPackets", UintegerValue(packetCount));
    client.SetAttribute("Interval", TimeValue(packetInterval));
    client.SetAttribute("PacketSize", UintegerValue(packetSize));
    
    ApplicationContainer clientApps = client.Install(nodes.Get(fromNode));
    clientApps.Start(Seconds(0));
    clientApps.Stop(Seconds(maxTime));
    
    Config::ConnectWithoutContext(
        "/NodeList/" + std::to_string(toNode) + "/ApplicationList/" +
        std::to_string(seenRecvSocketsFor[toNode]) + "/Rx",
        MakeBoundCallback(&RecordReceivedUdp, fromNode)
    );

    seenRecvSocketsFor[toNode] += 1;
    remainingBytes[fromNode] = -1;
}

}  // end of anonymous namespace


int
main(int argc, char* argv[])
{
    LogComponentEnable("MyTcpVFixed", LOG_LEVEL_INFO);

    //
    // Allow the user to override any of the defaults at
    // run-time, via command-line arguments
    //
    CommandLine cmd(__FILE__);
    cmd.AddValue("tracing", "Flag to enable/disable tracing", tracingEnabled);

    // default maxBytes = 10 simulated seconds @ 5Mbit/s
    uint32_t maxBytes = 10 * (5000/8) * 1000;
    std::string node1TcpEngine = "ns3::MyTcpCongestionOps";
    std::string node2TcpEngine = "ns3::TcpNewReno";
    uint32_t myTcpMode = 0;
    cmd.AddValue("maxBytes", "Total number of bytes for application to send", maxBytes);
    cmd.AddValue("linkBandwidth", "", linkBandwidth);
    cmd.AddValue("maxTime", "Maximum simulation time (seconds)", maxTime);
    cmd.AddValue("node1TcpEngine", "Set TCP engine for flow from node 1", node1TcpEngine);
    cmd.AddValue("node2TcpEngine", "Set TCP engine for flow from node 2", node2TcpEngine);
    cmd.AddValue("myTcpMode", "set myTcpMode parameter passed to MyTcpCongestionOps", myTcpMode);
    cmd.Parse(argc, argv);

    Config::SetDefault(
        "ns3::MyTcpCongestionOps::MyTcpMode",
        UintegerValue(myTcpMode)
    );


    SetupTopology();

    SetNodeTcpEngine(1, node1TcpEngine);
    SetNodeTcpEngine(2, node2TcpEngine);

    CreateTcpFlow(1, 3, 5555, maxBytes);
    CreateTcpFlow(2, 3, 5556, maxBytes);

    //
    // Now, do the actual simulation.
    //
    NS_LOG_INFO("Run Simulation.");

    // Make sure simulation stops eventually.
    // We shold stop earlier because
    Simulator::Stop(Seconds(maxTime));
    Simulator::Run();
    Simulator::Destroy();
    NS_LOG_INFO("Done.");
    NS_LOG_INFO("last seen message time from node 1 (" << node1TcpEngine << ") at " << lastReceivedTime[1]);
    NS_LOG_INFO("have " << remainingBytes[1] << " left to send (if not 0, increase maxTime)");
    NS_LOG_INFO("last seen message time from node 2 (" << node2TcpEngine << ") at " << lastReceivedTime[2]);
    NS_LOG_INFO("have " << remainingBytes[2] << " left to send (if not 0, increase maxTime)");
    return 0;
}
