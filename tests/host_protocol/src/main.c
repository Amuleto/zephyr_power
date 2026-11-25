#include <errno.h>
#include <string.h>

#include <zephyr/ztest.h>

#include "host_protocol.h"

/* 按协议手算的启动帧与心跳帧，避免仅用编码器验证解码器。 */
static const uint8_t start_frame[] = {0x0d, 0x0a, 0x61, 0x00, 0x08, 0x55, 0x03, 0x01};
static const uint8_t heartbeat_frame[] =
{
    0x0d, 0x0a, 0x68, 0x00, 0x0c, 0x55, 0x02, 0x01, 0x01, 0x02, 0x00, 0x01,
};

/**
 ******************************************************************************
 * @brief      逐字节输入测试数据，并处理所有已缓存的完整帧。
 * @param[in,out] parser:测试持有的解析状态。
 * @param[in]  bytes:待输入字节。
 * @param[in]  length:输入长度。
 * @retval     已交付的完整帧数量。
 ******************************************************************************
 */
static size_t feed(host_protocol_parser_t *parser, const uint8_t *bytes, size_t length)
{
    size_t frames = 0;

    for (size_t i = 0; i < length; i++)
    {
        if (host_protocol_receive(parser, bytes[i], 1))
        {
            do
            {
                frames++;
            } while (host_protocol_next(parser));
        }
    }
    return frames;
}

/* 半帧必须保留，接到剩余字节后才交付；同一串口片段可包含多帧。 */
ZTEST(host_protocol, test_fragmented_and_consecutive_frames)
{
    host_protocol_parser_t parser = {0};

    zassert_equal(feed(&parser, start_frame, 3), 0);
    zassert_equal(feed(&parser, start_frame + 3, sizeof(start_frame) - 3), 1);
    zassert_equal(feed(&parser, heartbeat_frame, sizeof(heartbeat_frame)), 1);
    zassert_equal(feed(&parser, start_frame, sizeof(start_frame)), 1);
}

/* 错误帧头和长度不允许卡住后续的有效启动命令。 */
ZTEST(host_protocol, test_noise_and_invalid_lengths)
{
    host_protocol_parser_t parser = {0};
    const uint8_t noise[] =
    {
        0xff, 0x0d, 0xff, 0x0d, 0x0a, 0, 0, 7, 0, 0, 0,
        0x0d, 0x0a, 0, 0xff, 0xff, 0, 0, 0,
    };

    zassert_equal(feed(&parser, noise, sizeof(noise)), 0);
    zassert_equal(feed(&parser, start_frame, sizeof(start_frame)), 1);
}

/* 坏校验外层帧内包含两个有效帧时，应立即交付两帧，无需再输入字节。 */
ZTEST(host_protocol, test_checksum_recovery_drains_cached_frames)
{
    host_protocol_parser_t parser = {0};
    uint8_t bytes[8 + 2 * sizeof(start_frame)] = {0x0d, 0x0a, 0xff, 0, sizeof(bytes)};

    memcpy(bytes + 8, start_frame, sizeof(start_frame));
    memcpy(bytes + 8 + sizeof(start_frame), start_frame, sizeof(start_frame));
    zassert_equal(feed(&parser, bytes, sizeof(bytes)), 2);
    zassert_equal(parser.used, 0);
}

/* 载荷中的 0D 0A 不是新帧，必须按整帧长度接收。 */
ZTEST(host_protocol, test_sync_bytes_in_payload)
{
    host_protocol_parser_t parser = {0};
    const uint8_t bytes[] =
    {
        0x0d, 0x0a, 0x7b, 0, 0x0c, 0x55, 2, 1, 0x0d, 0x0a, 0, 0,
    };

    for (size_t i = 0; i < sizeof(bytes); i++)
    {
        zassert_equal(host_protocol_receive(&parser, bytes[i], i), i == sizeof(bytes) - 1);
    }
    zassert_mem_equal(parser.frame.data, bytes + 8, 4);
}

/* 半帧超时后下一条帧能正常接收，已交付完整帧不被超时清除。 */
ZTEST(host_protocol, test_partial_timeout)
{
    host_protocol_parser_t parser = {0};

    zassert_equal(feed(&parser, heartbeat_frame, 9), 0);
    host_protocol_expire(&parser, HOST_PROTOCOL_TIMEOUT_MS + 1);
    zassert_equal(parser.used, 0);
    for (size_t i = 0; i < sizeof(start_frame); i++)
    {
        host_protocol_receive(&parser, start_frame[i], 200);
    }
    host_protocol_expire(&parser, 1000);
    zassert_true(parser.ready);
    zassert_mem_equal(&parser.frame, start_frame, sizeof(start_frame));
}

/* 组帧结果与手工例帧一致，容量不足或载荷越界时拒绝写入。 */
ZTEST(host_protocol, test_encode_and_bounds)
{
    const host_link_header_t header = {.direction = 0x55, .type = 3, .command = 1};
    uint8_t bytes[sizeof(start_frame)] = {0};

    zassert_equal(host_protocol_encode(bytes, sizeof(bytes), &header, NULL, 0), sizeof(bytes));
    zassert_mem_equal(bytes, start_frame, sizeof(bytes));
    zassert_equal(host_protocol_encode(bytes, sizeof(bytes) - 1, &header, NULL, 0), -EINVAL);
    zassert_equal(host_protocol_encode(bytes, sizeof(bytes), &header, NULL, 1), -EINVAL);
    zassert_equal(host_protocol_encode(bytes, sizeof(bytes), &header, bytes,
                                      HOST_PROTOCOL_DATA_MAX + 1), -EINVAL);
}

ZTEST_SUITE(host_protocol, NULL, NULL, NULL, NULL, NULL);
