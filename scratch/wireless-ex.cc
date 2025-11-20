/*
 * Template code for wireless assignment for CS 4457.
 *
 * This code is based on the examples/wireless/wifi-simple-adhoc.cc in NS-3.
 *
 */

#include "ns3/command-line.h"
#include "ns3/applications-module.h"
#include "ns3/config.h"
#include "ns3/double.h"
#include "ns3/flow-monitor-helper.h"
#include "ns3/internet-stack-helper.h"
#include "ns3/ipv4-address-helper.h"
#include "ns3/ipv4-flow-classifier.h"
#include "ns3/log.h"
#include "ns3/mobility-helper.h"
#include "ns3/mobility-model.h"
#include "ns3/neighbor-cache-helper.h"
#include "ns3/on-off-helper.h"
#include "ns3/string.h"
#include "ns3/uinteger.h"
#include "ns3/wifi-tx-stats-helper.h"
#include "ns3/yans-wifi-channel.h"
#include "ns3/yans-wifi-helper.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("WirelessEx");

struct Settings {
    std::string physicalMode;
    std::string sendRate;
    bool rtsCts;
    dBm_u txPower;
    int retryCount;
    bool useTcp;
};

static const dBm_u DEFAULT_TX_POWER{16.0206};

// minimum RSSI (received signal strength indicator) for the simulation to accept
// a premable (indicating the start of a frame) and minimum signal-to-noise ratio
// for the same. We set these lower than the default so we can more easily have
// signals in the range where the error model predicts significant levels of corruption
// without interference.
static const dBm_u minimumRssi{-101.};
static const dBm_u snrThreshold{3.};

static Settings currentSettings = {
    .physicalMode = "DsssRate1Mbps",
    .sendRate = "0.45Mbps",
    .rtsCts = false,
    .txPower = dBm_u{16.0206},
    .retryCount = 7,
    .useTcp = false,
};

std::ostream& operator<<(std::ostream& out, Settings const& settings) {
    out << "--physicalMode=" << settings.physicalMode
        << " --sendRate=" << settings.sendRate
        << " --rtsCts=" << (settings.rtsCts ? "true" : "false")
        << " --txPower=" << settings.txPower
        << " --retryCount=" << settings.retryCount
        << " --useTcp=" << (settings.useTcp ? "true": "false");
    return out;
}

struct Scenario {
    int id;
    double ratio;
    struct Settings high, low;
};

static Scenario scenarios[] = {
    {
        .id = 1,
        .ratio = 1.5,
        .high = {
            .physicalMode = "DsssRate1Mbps",
            .sendRate = "0.9Mbps",
            .rtsCts = false,
            .txPower = dBm_u{16},
            .retryCount = 7,
            .useTcp = false,
        },
        .low = {
            .physicalMode = "DsssRate1Mbps",
            .sendRate = "0.9Mbps",
            .rtsCts = false,
            .txPower = dBm_u{3},
            .retryCount = 7,
            .useTcp = false,
        },
    },
    {
        .id = 2,
        .ratio = 1.5,
        .high = {
            .physicalMode = "DsssRate1Mbps",
            .sendRate = "0.9Mbps",
            .rtsCts = false,
            .txPower = dBm_u{3},
            .retryCount = 7,
            .useTcp = false,
        },
        .low = {
            .physicalMode = "DsssRate1Mbps",
            .sendRate = "0.9Mbps",
            .rtsCts = false,
            .txPower = dBm_u{16},
            .retryCount = 7,
            .useTcp = false,
        },
    },
    {
        .id = 3,
        .ratio = 1.5,
        .high = {
            .physicalMode = "DsssRate1Mbps",
            .sendRate = "0.9Mbps",
            .rtsCts = true,
            .txPower = DEFAULT_TX_POWER,
            .retryCount = 7,
            .useTcp = false,
        },
        .low = {
            .physicalMode = "DsssRate5_5Mbps",
            .sendRate = "0.9Mbps",
            .rtsCts = true,
            .txPower = DEFAULT_TX_POWER,
            .retryCount = 7,
            .useTcp = false,
        },
    },
    {
        .id = 4,
        .ratio = 1.8,
        .high = {
            .physicalMode = "DsssRate1Mbps",
            .sendRate = "0.45Mbps",
            .rtsCts = true,
            .txPower = DEFAULT_TX_POWER,
            .retryCount = 7,
            .useTcp = false,
        },
        .low = {
            .physicalMode = "DsssRate1Mbps",
            .sendRate = "0.45Mbps",
            .rtsCts = false,
            .txPower = DEFAULT_TX_POWER,
            .retryCount = 7,
            .useTcp = false,
        },
    },
    {
        .id = 5,
        .ratio = 1.3,
        .high = {
            .physicalMode = "DsssRate1Mbps",
            .sendRate = "0.9Mbps",
            .rtsCts = false,
            .txPower = DEFAULT_TX_POWER,
            .retryCount = 7,
            .useTcp = false,
        },
        .low = {
            .physicalMode = "DsssRate1Mbps",
            .sendRate = "0.9Mbps",
            .rtsCts = true,
            .txPower = DEFAULT_TX_POWER,
            .retryCount = 7,
            .useTcp = false,
        },
    },
    {
        .id = 6,
        .ratio = 1.5,
        .high = {
            .physicalMode = "DsssRate1Mbps",
            .sendRate = "0.25Mbps",
            .rtsCts = false,
            .txPower = DEFAULT_TX_POWER,
            .retryCount = 7,
            .useTcp = true,
        },
        .low = {
            .physicalMode = "DsssRate1Mbps",
            .sendRate = "0.25Mbps",
            .rtsCts = false,
            .txPower = DEFAULT_TX_POWER,
            .retryCount = 1,
            .useTcp = true,
        },
    },
};

static bool enableWifiLogging{false};
static bool flowDump{false};
static bool retransmitCountDump{false};
static bool enablePcap{false};
static int scenarioId{0};
static Time duration{Seconds(60.0)};

static Ptr<PacketSink> SetupSendRecv(Ptr<Node> from, Ptr<Node> to, Time start) {
    const uint16_t port = 8888;
    Ptr<Ipv4> sinkIpv4 = to->GetObject<Ipv4>();
    // interface 0 is loopback; use interface 1, first address (index 0)
    Ipv4Address sinkAddress = sinkIpv4->GetAddress(1, 0).GetLocal();
    InetSocketAddress sinkSocketAddress{
        InetSocketAddress(sinkAddress, port)
    };

    std::string socketType = currentSettings.useTcp ? "ns3::TcpSocketFactory" : "ns3::UdpSocketFactory";

    PacketSinkHelper packetSinkHelper(socketType, sinkSocketAddress);
    ApplicationContainer sinkApp = packetSinkHelper.Install(to);

    OnOffHelper onOffHelper(socketType, sinkSocketAddress);
    onOffHelper.SetAttribute("PacketSize", UintegerValue(1000));
    onOffHelper.SetAttribute("OnTime", StringValue("ns3::ExponentialRandomVariable[Mean=1.0]"));
    onOffHelper.SetAttribute("OffTime", StringValue("ns3::ExponentialRandomVariable[Mean=0.0]"));
    onOffHelper.SetAttribute("DataRate", StringValue(currentSettings.sendRate));
    onOffHelper.SetAttribute("StartTime", TimeValue(start));
    onOffHelper.Install(from);

    return DynamicCast<PacketSink>(sinkApp.Get(0));

}

static void FlowDump(FlowMonitorHelper& flowmon, Ptr<FlowMonitor> monitor)
{
    monitor->CheckForLostPackets();
    Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier>(flowmon.GetClassifier());
    FlowMonitor::FlowStatsContainer stats = monitor->GetFlowStats();
    for (auto i = stats.begin(); i != stats.end(); ++i)
    {
        Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow(i->first);
        std::cout << "Flow " << i->first << " (" << t.sourceAddress << " -> "
                  << t.destinationAddress << ")\n";
        std::cout << "  Tx Packets: " << i->second.txPackets << "\n";
        std::cout << "  Tx Bytes:   " << i->second.txBytes << "\n";
        std::cout << "  Rx Packets: " << i->second.rxPackets << "\n";
        std::cout << "  Rx Bytes:   " << i->second.rxBytes << "\n";
    }
}

/* Function for you to modify. Set the positions (in x, y, z coordinates in simualated meters.
  for the wireless node. */
void SetupPositions(Ptr<ListPositionAllocator> positionAlloc) {
    if (scenarioId == 0) {
        positionAlloc->Add(Vector(0.0, 0.0, 0.0));
        positionAlloc->Add(Vector(1., 0.0, 0.0));
        positionAlloc->Add(Vector(1., 1., 0.0));
        positionAlloc->Add(Vector(0.0, 1., 0.0));
    } else {
        NS_ASSERT_MSG(false, "Not implemented");
    }
}

long RunExperiment()
{
    if (currentSettings.rtsCts) {
        Config::SetDefault("ns3::WifiRemoteStationManager::RtsCtsThreshold", UintegerValue(100));
    } else {
        Config::SetDefault("ns3::WifiRemoteStationManager::RtsCtsThreshold", UintegerValue(100000));
    }

    Config::SetDefault("ns3::WifiPhy::TxPowerStart", DoubleValue(currentSettings.txPower));
    Config::SetDefault("ns3::WifiPhy::TxPowerEnd", DoubleValue(currentSettings.txPower));
    Config::SetDefault("ns3::WifiMac::FrameRetryLimit", UintegerValue(currentSettings.retryCount));

    // The default minimum RSSI is -82dB and signal-to-noise ratio is 4, which makes reception mostly binary;
    // adjusting this lower allows the error rate scenariol to take effect.
    // See also examples/wireless/wifi-simple-infra
    Config::SetDefault("ns3::ThresholdPreambleDetectionModel::MinimumRssi", DoubleValue(minimumRssi));
    Config::SetDefault("ns3::ThresholdPreambleDetectionModel::Threshold", DoubleValue(snrThreshold));

    NodeContainer c;
    c.Create(4);

    // The below set of helpers will help us to put together the wifi NICs we want
    WifiHelper wifi;
    if (enableWifiLogging)
    {
        WifiHelper::EnableLogComponents(); // Turn on all Wifi logging
    }
    wifi.SetStandard(WIFI_STANDARD_80211b);

    YansWifiPhyHelper wifiPhy;
    wifiPhy.SetErrorRateModel("ns3::YansErrorRateModel");
    wifiPhy.SetPcapDataLinkType(WifiPhyHelper::DLT_IEEE802_11_RADIO);

    YansWifiChannelHelper wifiChannel;
    wifiChannel.SetPropagationDelay("ns3::ConstantSpeedPropagationDelayModel");
    wifiChannel.AddPropagationLoss("ns3::FriisPropagationLossModel");
    wifiPhy.SetChannel(wifiChannel.Create());

    // Add a mac and disable rate control
    WifiMacHelper wifiMac;
    wifi.SetRemoteStationManager("ns3::ConstantRateWifiManager",
                                 "DataMode",
                                 StringValue(currentSettings.physicalMode),
                                 "ControlMode",
                                 StringValue(currentSettings.physicalMode));
    // Fix non-unicast data rate (used for, e.g., ARP) to be the same as that of unicast
    Config::SetDefault("ns3::WifiRemoteStationManager::NonUnicastMode",
                       StringValue(currentSettings.physicalMode));

    // Set it to adhoc scenario (no APs)
    wifiMac.SetType("ns3::AdhocWifiMac");
    NetDeviceContainer devices = wifi.Install(wifiPhy, wifiMac, c);

    MobilityHelper mobility;
    Ptr<ListPositionAllocator> positionAlloc = CreateObject<ListPositionAllocator>();
    mobility.SetPositionAllocator(positionAlloc);
    SetupPositions(positionAlloc);
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.Install(c);

    InternetStackHelper internet;
    internet.Install(c);

    Ipv4AddressHelper ipv4;

    ipv4.SetBase("10.1.1.0", "255.255.255.0");
    Ipv4InterfaceContainer i = ipv4.Assign(devices);

    NeighborCacheHelper nCache;
    // populate ARP tables so we don't have to worry about ARP messages being lost
    nCache.PopulateNeighborCache(i);

    Ptr<PacketSink> sink1 = SetupSendRecv(c.Get(0), c.Get(1), Seconds(0.0));
    Ptr<PacketSink> sink2 = SetupSendRecv(c.Get(2), c.Get(3), Seconds(0.0));

    FlowMonitorHelper flowmon;
    Ptr<FlowMonitor> monitor = flowmon.InstallAll();

    if (enablePcap) {
        wifiPhy.EnablePcap("wireless-ex", devices);
    }

    WifiTxStatsHelper txStatsHelper;
    txStatsHelper.Enable(c);
    txStatsHelper.Start(Seconds(0));
    txStatsHelper.Stop(duration);

    Simulator::Stop(duration);
    Simulator::Run();

    if (flowDump) {
        FlowDump(flowmon, monitor);
    }

    if (retransmitCountDump) {
        for (auto item : txStatsHelper.GetRetransmissionsByNodeDevice()) {
            std::cout << "from node " << std::get<0>(item.first) << ": "
                      << item.second << " retransmissions" << std::endl;
        }
    }

    long total = sink1->GetTotalRx() + sink2->GetTotalRx();

    Simulator::Destroy();

    return total;
}

std::string formatPercent(double ratio) {
    char percent[50];
    snprintf(percent, sizeof percent, "%.1f", 100.0 * ratio);
    return percent;
}

void RunScenarioFromStruct(Scenario const &scenario) {
    scenarioId = scenario.id;
    std::cout << "Running scenarioId=" << scenarioId << ", higher target throughput settings:\n"
        << "  " << scenario.high << std::endl;
    currentSettings = scenario.high;
    double highResult = RunExperiment();
    std::cout << "Running scenarioId=" << scenarioId << ", lower target throughput settings:\n"
        << "  " << scenario.low << std::endl;
    currentSettings = scenario.low;
    double lowResult = RunExperiment();

    char percent[50];
    snprintf(percent, sizeof percent, "%.1f", 100.0 * (highResult / lowResult));
    char targetPercent[50];
    snprintf(targetPercent, sizeof targetPercent,
        "%.1f", 100.0 * (highResult / lowResult));

    std::cout << "scenarioId=" << scenarioId << ": " << highResult << " versus " << lowResult
        << "\n        (first is " << formatPercent(highResult / lowResult)
        << "% of second; target " << formatPercent(scenario.ratio) << "%)"
        << std::endl;
}

void RunScenarioFromId(int id) {
    for (Scenario const &scenario: scenarios) {
        if (scenario.id == id) {
            RunScenarioFromStruct(scenario);
            return;
        }
    }
    NS_LOG_UNCOND("no scenario " << id);
}

void ListScenarios() {
    for (Scenario const &scenario: scenarios) {
        std::cout << "Scenario #" << scenario.id << std::endl;
        std::cout << "With\n  " << scenario.high << "\nsend at least "
                  << formatPercent(scenario.ratio) << "% of the bytes sent with\n  "
                  << scenario.low << "\n" << std::endl;
    }
}

int
main(int argc, char* argv[])
{
    CommandLine cmd(__FILE__);
    cmd.AddValue("enableWifiLogging", "turn on all WifiNetDevice log components", enableWifiLogging);
    cmd.AddValue("flowDump", "enable dump of flows/bytes pre flow", flowDump);
    cmd.AddValue("retransmitCountDump", "enable dump of resent packets per node", retransmitCountDump);
    cmd.AddValue("enablePcap", "enable produciton of pcap files", enablePcap);

    cmd.AddValue("duration", "simulation duration", duration);

    cmd.AddValue("scenarioId", "which scenario ID for assignment", scenarioId);

    cmd.AddValue("sendRate", "enable dump of flows/bytes pre flow", currentSettings.sendRate);
    cmd.AddValue("rtsCts", "enable RTS/CTS", currentSettings.rtsCts);
    cmd.AddValue("physicalMode", "physical layer scenario (determines data rate)", currentSettings.physicalMode);
    cmd.AddValue("txPower", "transmission power", currentSettings.physicalMode);
    cmd.AddValue("useTcp", "use TCP instead of UDP", currentSettings.useTcp);
    cmd.AddValue("retryCount", "wifi maximum retry count", currentSettings.retryCount);

    int assignmentTestScenario{0};
    cmd.AddValue("assignmentTestScenario", "test specific scenario for assignment", assignmentTestScenario);
    bool assignmentTestAll{false};
    cmd.AddValue("assignmentTestAll", "test all scenarios for assignment", assignmentTestAll);
    bool listScenarios{false};
    cmd.AddValue("listScenarios", "list all scenarios for assignment", listScenarios);

    cmd.Parse(argc, argv);


    if (listScenarios) {
        ListScenarios();
    } else if (assignmentTestAll) {
        for (int i = 1; i <= 6; i += 1) {
            RunScenarioFromId(i);
        }
    } else if (assignmentTestScenario != 0) {
        RunScenarioFromId(assignmentTestScenario);
    } else {
        std::cout << "Running experiemnt with scenarioId=" << scenarioId
                  << " and settings:\n  " << currentSettings << std::endl;
        double total = RunExperiment();
        std::cout << "Got " << total << " bytes sent in simulation.";
    }

    return 0;
}
