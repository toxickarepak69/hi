#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/netanim-module.h"
#include "ns3/mobility-module.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("GoBackN");

uint32_t windowSize = 4;
uint32_t maxPackets = 15;
uint32_t nextSeqNum = 0;
uint32_t baseSeqNum = 0;
uint32_t packetCount = 0;

Ptr<Socket> senderSocket;
Ptr<Socket> receiverSocket;

std::map<uint32_t, EventId> timers;

void SendPacket(uint32_t seqNum) {
    uint8_t buffer[5];
    buffer[0] = 0; // DATA packet
    memcpy(&buffer[1], &seqNum, sizeof(seqNum));
    
    Ptr<Packet> packet = Create<Packet>(buffer, 5);
    senderSocket->Send(packet);
    NS_LOG_UNCOND("Sender: Sent DATA Seq=" << seqNum << " at " << Simulator::Now().GetSeconds() << "s");
    
    // Start timer for this packet
    timers[seqNum] = Simulator::Schedule(Seconds(2.0), [seqNum]() {
        NS_LOG_UNCOND("Timeout! Retransmitting window starting from Seq=" << baseSeqNum);
        // Go-Back-N: Retransmit all packets from base
        nextSeqNum = baseSeqNum;
        for (uint32_t i = baseSeqNum; i < baseSeqNum + windowSize && i < maxPackets; i++) {
            SendPacket(i);
        }
    });
}

void SendWindow() {
    while (nextSeqNum < baseSeqNum + windowSize && nextSeqNum < maxPackets) {
        SendPacket(nextSeqNum);
        nextSeqNum++;
    }
}

void ReceivePacket(Ptr<Socket> socket) {
    Ptr<Packet> packet;
    while ((packet = socket->Recv())) {
        uint8_t buffer[5];
        packet->CopyData(buffer, 5);
        uint8_t type = buffer[0];
        
        if (type == 0) { // DATA packet
            uint32_t seqNum;
            memcpy(&seqNum, &buffer[1], sizeof(seqNum));
            NS_LOG_UNCOND("Receiver: Got DATA Seq=" << seqNum << " at " << Simulator::Now().GetSeconds() << "s");
            
            // Send ACK
            uint8_t ackBuffer[5];
            ackBuffer[0] = 1; // ACK packet
            memcpy(&ackBuffer[1], &seqNum, sizeof(seqNum));
            Ptr<Packet> ack = Create<Packet>(ackBuffer, 5);
            receiverSocket->Send(ack);
            NS_LOG_UNCOND("Receiver: Sent ACK for Seq=" << seqNum);
            
            packetCount++;
        } 
        else if (type == 1) { // ACK packet
            uint32_t ackNum;
            memcpy(&ackNum, &buffer[1], sizeof(ackNum));
            NS_LOG_UNCOND("Sender: Got ACK for Seq=" << ackNum << " at " << Simulator::Now().GetSeconds() << "s");
            
            // Cancel timer for this packet
            if (timers.find(ackNum) != timers.end()) {
                Simulator::Cancel(timers[ackNum]);
                timers.erase(ackNum);
            }
            
            // Move window forward
            if (ackNum >= baseSeqNum) {
                baseSeqNum = ackNum + 1;
                if (baseSeqNum < maxPackets) {
                    SendWindow();
                }
            }
        }
    }
}

int main() {
    NodeContainer nodes;
    nodes.Create(2);

    // Set up mobility for NetAnim
    MobilityHelper mobility;
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.Install(nodes);
    
    // Set positions for visualization
    Ptr<ConstantPositionMobilityModel> senderMobility = nodes.Get(0)->GetObject<ConstantPositionMobilityModel>();
    Ptr<ConstantPositionMobilityModel> receiverMobility = nodes.Get(1)->GetObject<ConstantPositionMobilityModel>();
    senderMobility->SetPosition(Vector(0, 0, 0));
    receiverMobility->SetPosition(Vector(100, 0, 0));

    PointToPointHelper p2p;
    p2p.SetDeviceAttribute("DataRate", StringValue("5Mbps"));
    p2p.SetChannelAttribute("Delay", StringValue("2ms"));

    NetDeviceContainer devices = p2p.Install(nodes);

    InternetStackHelper stack;
    stack.Install(nodes);

    Ipv4AddressHelper address;
    address.SetBase("10.1.1.0", "255.255.255.0");
    Ipv4InterfaceContainer interfaces = address.Assign(devices);

    senderSocket = Socket::CreateSocket(nodes.Get(0), UdpSocketFactory::GetTypeId());
    senderSocket->Bind(InetSocketAddress(Ipv4Address::GetAny(), 8080));
    senderSocket->Connect(InetSocketAddress(interfaces.GetAddress(1), 8080));
    senderSocket->SetRecvCallback(MakeCallback(&ReceivePacket));

    receiverSocket = Socket::CreateSocket(nodes.Get(1), UdpSocketFactory::GetTypeId());
    receiverSocket->Bind(InetSocketAddress(Ipv4Address::GetAny(), 8080));
    receiverSocket->Connect(InetSocketAddress(interfaces.GetAddress(0), 8080));
    receiverSocket->SetRecvCallback(MakeCallback(&ReceivePacket));

    Simulator::Schedule(Seconds(1.0), &SendWindow);

    // Create NetAnim animation
    AnimationInterface anim("go-back-n-animation.xml");
    
    // Set node properties for better visualization
    anim.SetConstantPosition(nodes.Get(0), 10, 30);  // Sender node
    anim.SetConstantPosition(nodes.Get(1), 90, 30);  // Receiver node
    
    // Set node descriptions
    anim.UpdateNodeDescription(0, "Sender");
    anim.UpdateNodeDescription(1, "Receiver");
    anim.UpdateNodeColor(0, 255, 0, 0);    // Red for sender
    anim.UpdateNodeColor(1, 0, 255, 0);    // Green for receiver
    
    // Enable packet metadata for animation
    anim.EnablePacketMetadata(true);

    Simulator::Stop(Seconds(30.0));
    Simulator::Run();
    Simulator::Destroy();
    
    NS_LOG_UNCOND("Simulation finished. Sent " << packetCount << "/" << maxPackets << " packets");
    return 0;
}