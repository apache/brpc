//
// Created by z00926396 on 2026/4/11.
//

#ifndef BRPC_UB_TRANSPORT_H
#define BRPC_UB_TRANSPORT_H
#if BRPC_WITH_UBRING
#include "brpc/socket.h"
#include "brpc/channel.h"
#include "brpc/transport.h"

namespace brpc {
    class UBShmTransport : public Transport {
        friend class TransportFactory;
        friend class ub::UBShmEndpoint;
        friend class ub::UBConnect;
    public:
        void Init(Socket* socket, const SocketOptions& options) override;
        void Release() override;
        int Reset(int32_t expected_nref) override;
        std::shared_ptr<AppConnect> Connect() override;
        int CutFromIOBuf(butil::IOBuf* buf) override;
        ssize_t CutFromIOBufList(butil::IOBuf** buf, size_t ndata) override;
        int WaitEpollOut(butil::atomic<int>* _epollout_butex, bool pollin, const timespec duetime) override;
        void ProcessEvent(bthread_attr_t attr) override;
        void QueueMessage(InputMessageClosure& inputMsg, int* num_bthread_created, bool last_msg) override;
        void Debug(std::ostream &os) override;
        ub::UBShmEndpoint* GetUBShmEp() {
            CHECK(_ub_ep != NULL);
            return _ub_ep;
        }
        static int ContextInitOrDie(bool serverOrNot, const void* _options);
    private:
        static bool OptionsAvailableForUB(const ChannelOptions* opt);
        static bool OptionsAvailableOverUB(const ServerOptions* opt);
    private:
        // The on/off state of UB
        enum UBState {
            UB_ON,
            UB_OFF,
            UB_UNKNOWN
        };
        // The UBShmEndpoint
        ub::UBShmEndpoint* _ub_ep = NULL;
        // Should use UB or not
        UBState _ub_state;
        std::shared_ptr<TcpTransport>  _tcp_transport;
    };
} // namespace brpc
#endif // BRPC_WITH_UBRING
#endif //BRPC_UB_TRANSPORT_H