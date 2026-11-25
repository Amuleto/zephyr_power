#ifndef BRINGUP_HOST_PROTOCOL_H_
#define BRINGUP_HOST_PROTOCOL_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** 协议允许的 Data 最大长度，单位为字节。 */
#define HOST_PROTOCOL_DATA_MAX 512u
/** 半帧接收超时，联调值，单位为毫秒。 */
#define HOST_PROTOCOL_TIMEOUT_MS 100u

/** 线协议固定帧头，各成员按字节布局。 */
typedef struct
{
    uint8_t sync[2];      /**< 固定 0D 0A。 */
    uint8_t checksum;     /**< Length 至 Data 末尾的 ADD8。 */
    uint8_t length_be[2]; /**< 整帧长度，大端。 */
    uint8_t direction;    /**< 55 主控发送，AA 预充板发送。 */
    uint8_t type;         /**< 报文类型。 */
    uint8_t command;      /**< 功能码。 */
} host_link_header_t;

/** 完整线协议帧，只有 length 指定的 Data 字节有效。 */
typedef struct
{
    host_link_header_t header;            /**< 固定帧头。 */
    uint8_t data[HOST_PROTOCOL_DATA_MAX]; /**< 可变长度载荷。 */
} host_protocol_frame_t;

/** 单条串口字节流的解析上下文，由调用线程独占。 */
typedef struct
{
    host_protocol_frame_t frame; /**< 当前帧缓存。 */
    size_t used;                 /**< 缓存中有效字节数。 */
    size_t expected;      /**< 已通过长度检查的目标帧长，零表示等待帧头。 */
    bool ready;           /**< 当前 frame 含已交付的完整帧。 */
    int64_t last_byte_ms; /**< 最后一次输入字节的本地时间。 */
} host_protocol_parser_t;

/**
 ******************************************************************************
 * @brief      输入字节并尝试输出完整帧。
 * @param[in,out] parser:初始化为零的解析上下文。
 * @param[in]  byte:新收到的字节。
 * @param[in]  now_ms:本地接收时间，单位为毫秒。
 * @retval     true:缓存中得到有效完整帧。
 * @retval     false:等待更多字节或本次输入无效。
 * @details    返回 true 后应处理 frame，并用 host_protocol_next 排空完整帧。
 ******************************************************************************
 */
bool host_protocol_receive(host_protocol_parser_t *parser, uint8_t byte, int64_t now_ms);

/**
 ******************************************************************************
 * @brief      消费已交付帧，并尝试交付缓存中的下一帧。
 * @param[in,out] parser:解析上下文。
 * @retval     true:frame 含下一条有效完整帧。
 * @retval     false:没有完整帧。
 * @details    每处理完一帧后调用，直到返回 false，无需等待新的串口字节。
 ******************************************************************************
 */
bool host_protocol_next(host_protocol_parser_t *parser);

/**
 ******************************************************************************
 * @brief      在半帧接收超时后清理缓存。
 * @param[in,out] parser:解析上下文。
 * @param[in]  now_ms:本地时间，单位为毫秒。
 * @retval     None
 ******************************************************************************
 */
void host_protocol_expire(host_protocol_parser_t *parser, int64_t now_ms);

/**
 ******************************************************************************
 * @brief      封装固定帧头及 ADD8 校验。
 * @param[out] buffer:输出缓冲区。
 * @param[in]  capacity:输出缓冲区容量。
 * @param[in]  header:方向、类型和功能码，其他字段由函数填写。
 * @param[in]  data:载荷，长度为零时允许为空。
 * @param[in]  length:载荷字节数。
 * @retval     正值:生成的整帧字节数。
 * @retval     -EINVAL:参数无效或容量不足。
 ******************************************************************************
 */
int host_protocol_encode(void *buffer, size_t capacity, const host_link_header_t *header,
                         const void *data, size_t length);

#endif /* BRINGUP_HOST_PROTOCOL_H_ */
