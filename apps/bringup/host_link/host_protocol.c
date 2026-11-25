#include <errno.h>
#include <string.h>

#include <zephyr/sys/byteorder.h>

#include "host_protocol.h"

_Static_assert(sizeof(host_link_header_t) == 8u, "Protocol header layout");
_Static_assert(offsetof(host_protocol_frame_t, data) == 8u, "Protocol data offset");

/**
 ******************************************************************************
 * @brief      计算整帧的 ADD8。
 * @param[in]  bytes:完整帧字节。
 * @param[in]  length:整帧长度。
 * @retval     累加和低八位。
 ******************************************************************************
 */
static uint8_t frame_checksum(const uint8_t *bytes, size_t length)
{
    uint8_t sum = 0;

    for (size_t i = offsetof(host_link_header_t, length_be); i < length; i++)
    {
        sum = (uint8_t)(sum + bytes[i]);
    }
    return sum;
}

/**
 ******************************************************************************
 * @brief      消费缓存前缀，并保留后续字节用于重新寻找帧头。
 * @param[in,out] parser:解析上下文。
 * @param[in]  count:消费长度，不大于 used。
 * @retval     None
 ******************************************************************************
 */
static void consume(host_protocol_parser_t *parser, size_t count)
{
    uint8_t *bytes = (uint8_t *)&parser->frame;

    parser->used -= count;
    memmove(bytes, bytes + count, parser->used);
    parser->expected = 0;
}

/**
 ******************************************************************************
 * @brief      在半帧接收超时后清理缓存。
 * @param[in,out] parser:解析上下文。
 * @param[in]  now_ms:本地时间，单位为毫秒。
 * @retval     None
 ******************************************************************************
 */
void host_protocol_expire(host_protocol_parser_t *parser, int64_t now_ms)
{
    if (parser != NULL && !parser->ready && parser->used
        && now_ms - parser->last_byte_ms >= HOST_PROTOCOL_TIMEOUT_MS)
    {
        parser->used = 0;
        parser->expected = 0;
    }
}

/**
 ******************************************************************************
 * @brief      从缓存中寻找尚未交付的完整帧。
 * @param[in,out] parser:解析上下文，指针已由入口校验。
 * @retval     true:缓存中得到有效完整帧。
 * @retval     false:等待更多字节。
 ******************************************************************************
 */
static bool find_frame(host_protocol_parser_t *parser)
{
    uint8_t *bytes = (uint8_t *)&parser->frame;
    host_link_header_t *header = &parser->frame.header;

    while (parser->used)
    {
        if (bytes[0] != 0x0du || (parser->used > 1u && bytes[1] != 0x0au))
        {
            consume(parser, 1u);
            continue;
        }
        if (parser->used < sizeof(*header))
        {
            return false;
        }
        if (!parser->expected)
        {
            size_t length = sys_get_be16(header->length_be);

            if (length < sizeof(*header) || length > sizeof(parser->frame))
            {
                consume(parser, 1u);
                continue;
            }
            parser->expected = length;
        }
        if (parser->used < parser->expected)
        {
            return false;
        }
        if (header->checksum == frame_checksum(bytes, parser->expected))
        {
            parser->ready = true;
            return true;
        }
        consume(parser, 1u);
    }
    return false;
}

/**
 ******************************************************************************
 * @brief      消费已交付帧，并尝试交付缓存中的下一帧。
 * @param[in,out] parser:解析上下文。
 * @retval     true:frame 含下一条有效完整帧。
 * @retval     false:没有完整帧。
 * @details    每处理完一帧后调用，直到返回 false，无需等待新的串口字节。
 ******************************************************************************
 */
bool host_protocol_next(host_protocol_parser_t *parser)
{
    if (parser == NULL)
    {
        return false;
    }
    if (parser->ready)
    {
        consume(parser, parser->expected);
        parser->ready = false;
    }
    return find_frame(parser);
}

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
bool host_protocol_receive(host_protocol_parser_t *parser, uint8_t byte, int64_t now_ms)
{
    uint8_t *bytes;

    if (parser == NULL)
    {
        return false;
    }
    host_protocol_expire(parser, now_ms);
    if (parser->ready)
    {
        consume(parser, parser->expected);
        parser->ready = false;
    }
    if (parser->used == sizeof(parser->frame))
    {
        parser->used = 0;
        parser->expected = 0;
    }
    bytes = (uint8_t *)&parser->frame;
    bytes[parser->used++] = byte;
    parser->last_byte_ms = now_ms;
    return find_frame(parser);
}

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
                         const void *data, size_t length)
{
    host_link_header_t encoded;
    uint8_t *bytes = buffer;
    size_t total = sizeof(encoded) + length;

    if (buffer == NULL || header == NULL || length > HOST_PROTOCOL_DATA_MAX || capacity < total
        || (length && data == NULL))
    {
        return -EINVAL;
    }

    encoded = *header;
    encoded.sync[0] = 0x0du;
    encoded.sync[1] = 0x0au;
    encoded.checksum = 0;
    sys_put_be16((uint16_t)total, encoded.length_be);
    memcpy(bytes, &encoded, sizeof(encoded));
    if (length)
    {
        memcpy(bytes + sizeof(encoded), data, length);
    }
    bytes[offsetof(host_link_header_t, checksum)] = frame_checksum(bytes, total);
    return (int)total;
}
