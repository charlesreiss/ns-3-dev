#include "mytcp.h"

#include "ns3/tcp-socket-state.h"
#include "ns3/log.h"

namespace ns3 {

NS_LOG_COMPONENT_DEFINE("MyTcpCongestionOps");
NS_OBJECT_ENSURE_REGISTERED(MyTcpCongestionOps);

TypeId
MyTcpCongestionOps::GetTypeId()
{
    static TypeId tid = TypeId("ns3::MyTcpCongestionOps")
                        .SetParent<TcpCongestionOps>()
                        .SetGroupName("Internet")
                        .AddAttribute("MyTcpMode",
                                      "mode parameter for MyTcpCongestionOps (see assignment)",
                                      UintegerValue(1),
                                      MakeUintegerAccessor(&MyTcpCongestionOps::m_myTcpMode),
                                      MakeUintegerChecker<uint32_t>())
                        .AddConstructor<MyTcpCongestionOps>();
    return tid;
}

MyTcpCongestionOps::MyTcpCongestionOps()
    /* this "initialization list" says what constructors to call on superclasses
       and instance variables (if any) */
    : TcpNewReno(),
      m_myTcpMode(0)
{
    NS_LOG_FUNCTION(this);
}

/* "Copy constructor"; special constructor used to create a copy of an existing TcpCongestionOps
   from another one. This is called indirectly by the Fork() method below.

   Using the initialization list, we call the superclasses's copy constructor.
*/
MyTcpCongestionOps::MyTcpCongestionOps(const MyTcpCongestionOps &ops)
    : TcpNewReno(ops),
      m_myTcpMode(ops.m_myTcpMode)
{
    NS_LOG_FUNCTION(this);
}

/* Destructor; called when a MyTcpCongestionOps goes out of scope or is otherwise freed. */
MyTcpCongestionOps::~MyTcpCongestionOps()
{
    NS_LOG_FUNCTION(this);
}

Ptr<TcpCongestionOps>
MyTcpCongestionOps::Fork()
{
    return CopyObject<MyTcpCongestionOps>(this);
}

/** Copied from src/internet/models/tcp-congestion-ops.cc */
void
MyTcpCongestionOps::IncreaseWindow(Ptr<TcpSocketState> tcb, uint32_t segmentsAcked)
{
    NS_LOG_FUNCTION(this << tcb << segmentsAcked);

    if (tcb->m_cWnd < tcb->m_ssThresh)
    {
        segmentsAcked = SlowStart(tcb, segmentsAcked);
    }

    if (tcb->m_cWnd >= tcb->m_ssThresh)
    {
        CongestionAvoidance(tcb, segmentsAcked);
    }
}

/** Copied from src/internet/models/tcp-congestion-ops.cc */
uint32_t
MyTcpCongestionOps::SlowStart(Ptr<TcpSocketState> tcb, uint32_t segmentsAcked)
{
    NS_LOG_FUNCTION(this << tcb << segmentsAcked);

    if (m_myTcpMode == 0) {
        if (segmentsAcked >= 1)
        {
            tcb->m_cWnd += tcb->m_segmentSize;
            return segmentsAcked - 1;
        }
    } else {
        NS_ASSERT_MSG(false, "unimplemented myTcpMode");
    }

    return 0;
}

/** Copied from src/internet/models/tcp-congestion-ops.cc */
void
MyTcpCongestionOps::CongestionAvoidance(Ptr<TcpSocketState> tcb, uint32_t segmentsAcked)
{
    NS_LOG_FUNCTION(this << tcb << segmentsAcked);
    
    if (m_myTcpMode == 0) {
        if (segmentsAcked > 0)
        {
            double adder =
                static_cast<double>(tcb->m_segmentSize * tcb->m_segmentSize) / tcb->m_cWnd.Get();
            adder = std::max(1.0, adder);
            tcb->m_cWnd += static_cast<uint32_t>(adder);
        }
    } else {
        NS_ASSERT_MSG(false, "unimplemented myTcpMode");
    }
}

uint32_t 
MyTcpCongestionOps::GetSsThresh(Ptr<const TcpSocketState> tcb, uint32_t bytesInFlight)
{
    NS_LOG_FUNCTION(this << tcb << bytesInFlight);

    if (m_myTcpMode == 0) {
        return std::max(2 * tcb->m_segmentSize, (uint32_t) (bytesInFlight / 2));
    } else {
        NS_ASSERT_MSG(false, "unimplemented myTcpMode");
    }
}

}
