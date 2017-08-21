#ifndef BRPC_ESP_HEAD_H
#define BRPC_ESP_HEAD_H


namespace brpc {

#pragma pack(push, r1, 1)
/// ESPAddress structure
union EspAddress {
    uint64_t addr;        ///< 地址
    struct {
        uint16_t stub;    ///< ESP中的stub
        uint16_t port;    ///< 端口号
        uint32_t ip;      ///< IP
    };
};

/// ESPHead structure
struct EspHead {
    EspAddress from;   ///< 来源ESP地址
    EspAddress to;     ///< 目的ESP地址
    uint32_t msg;        ///< 消息标号
    uint64_t msg_id;      ///< 消息ID
    int body_len;           ///< 数据区字节数
};
#pragma pack(pop, r1)

} // namespace brpc


#endif 

