#ifndef MYTCP_H_
#define MYTCP_H_

#include "ns3/tcp-congestion-ops.h"

#include <string>

namespace ns3 {

class MyTcpCongestionOps : public TcpNewReno {
public:
    static TypeId GetTypeId();
    MyTcpCongestionOps();
    MyTcpCongestionOps(const MyTcpCongestionOps &ops);
    ~MyTcpCongestionOps() override;
    std::string GetName();
    void IncreaseWindow(Ptr<TcpSocketState> tcb, uint32_t segmentsAcked) override;
    uint32_t GetSsThresh(Ptr<const TcpSocketState> tcb, uint32_t bytesInFlight) override;
    Ptr<TcpCongestionOps> Fork() override;
protected:
    uint32_t SlowStart(Ptr<TcpSocketState> tcb, uint32_t segmentsAcked) override;
    void CongestionAvoidance(Ptr<TcpSocketState> tcb, uint32_t segmentsAcked) override;
private:
    uint32_t m_myTcpMode;
};

}

#endif
