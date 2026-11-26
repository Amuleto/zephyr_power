#include <errno.h>
#include <stdio.h>
#include <time.h>

#include <zephyr/drivers/rtc.h>
#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/timeutil.h>

#include "app_clock.h"

static const struct device *const rtc_dev = DEVICE_DT_GET(DT_ALIAS(rtc0));
static K_MUTEX_DEFINE(clock_lock);

/**
 ******************************************************************************
 * @brief      打印本次启动后的运行时间。
 * @param[in]  sh:输出结果的 Shell 实例。
 * @param[in]  argc:参数数量，由 Shell 校验。
 * @param[in]  argv:命令参数，未使用。
 * @retval     0:查询成功。
 ******************************************************************************
 */
static int cmd_uptime(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    shell_print(sh, "Uptime: %lld ms", (long long)k_uptime_get());
    return 0;
}

SHELL_CMD_ARG_REGISTER(uptime, NULL, "Show time since boot", cmd_uptime, 1, 0);

/**
 ******************************************************************************
 * @brief      解析 UTC 日期和时间并校验日历范围。
 * @param[in]  date:日期字符串。
 * @param[in]  time:时间字符串。
 * @param[out] value:解析成功时写入 RTC 时间。
 * @retval     0:解析成功。
 * @retval     -EINVAL:格式或字段范围无效。
 * @retval     -ERANGE:日期超出当月天数。
 ******************************************************************************
 */
static int parse_datetime(const char *date, const char *time, struct rtc_time *value)
{
    static const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int year;
    int month;
    int day;
    int hour;
    int minute;
    int second;
    int max_day;
    char extra;

    /* 限制数字宽度防止转换溢出，尾部转换用于检测多余字符。 */
    if (sscanf(date, "%4d-%2d-%2d%c", &year, &month, &day, &extra) != 3
        || sscanf(time, "%2d:%2d:%2d%c", &hour, &minute, &second, &extra) != 3 || year < 2000
        || year > 2099 || month < 1 || month > 12 || hour < 0 || hour > 23 || minute < 0
        || minute > 59 || second < 0 || second > 59)
    {
        return -EINVAL;
    }

    /* 驱动支持的年份限定在 2000～2099 年。 */
    max_day = days[month - 1] + (month == 2 && year % 4 == 0);
    if (day < 1 || day > max_day)
    {
        return -ERANGE;
    }

    value->tm_year = year - 1900;
    value->tm_mon = month - 1;
    value->tm_mday = day;
    value->tm_hour = hour;
    value->tm_min = minute;
    value->tm_sec = second;

    return 0;
}

/**
 ******************************************************************************
 * @brief      解析输入并写入硬件 RTC。
 * @param[in]  sh:输出诊断信息的 Shell 实例。
 * @param[in]  date:日期字符串。
 * @param[in]  time:UTC 时间字符串。
 * @retval     0:写入成功。
 * @retval     -EINVAL:输入无效。
 * @retval     其他负值:RTC 写入失败。
 ******************************************************************************
 */
static int set_datetime(const struct shell *sh, const char *date, const char *time)
{
    struct rtc_time value = {0};
    int err;

    err = parse_datetime(date, time, &value);
    if (err == -ERANGE)
    {
        shell_error(sh, "Invalid calendar date");
        return -EINVAL;
    }
    if (err)
    {
        shell_error(sh, "Use: date YYYY-MM-DD HH:MM:SS (UTC, 2000-2099)");
        return err;
    }

    k_mutex_lock(&clock_lock, K_FOREVER);
    err = rtc_set_time(rtc_dev, &value);
    k_mutex_unlock(&clock_lock);
    if (err)
    {
        shell_error(sh, "RTC write failed: %d", err);
    }

    return err;
}

/**
 ******************************************************************************
 * @brief      查询 RTC 或设置后回读 UTC 时间。
 * @param[in]  sh:输出时间或错误的 Shell 实例。
 * @param[in]  argc:命令参数数量。
 * @param[in]  argv:可选的日期和时间参数。
 * @retval     0:执行成功。
 * @retval     -ENODEV:RTC 未就绪。
 * @retval     -EINVAL:参数无效。
 * @retval     其他负值:RTC 访问失败。
 ******************************************************************************
 */
static int cmd_date(const struct shell *sh, size_t argc, char **argv)
{
    struct rtc_time current = {0};
    int err;

    if (!device_is_ready(rtc_dev))
    {
        shell_error(sh, "RTC device not ready");
        return -ENODEV;
    }

    if (argc == 3)
    {
        err = set_datetime(sh, argv[1], argv[2]);
        if (err)
        {
            return err;
        }
    }
    else if (argc != 1)
    {
        shell_error(sh, "Use: date [YYYY-MM-DD HH:MM:SS]");
        return -EINVAL;
    }

    /* HXTAL 在仅由 VBAT 供电时停振，启动后需重新设时。 */
    k_mutex_lock(&clock_lock, K_FOREVER);
    err = rtc_get_time(rtc_dev, &current);
    k_mutex_unlock(&clock_lock);
    if (err)
    {
        shell_error(sh, "RTC read failed: %d (set date after boot)", err);
        return err;
    }

    shell_print(sh, "%04d-%02d-%02d %02d:%02d:%02d UTC", current.tm_year + 1900, current.tm_mon + 1,
                current.tm_mday, current.tm_hour, current.tm_min, current.tm_sec);
    return 0;
}

SHELL_CMD_ARG_REGISTER(date, NULL, "UTC RTC time: date [YYYY-MM-DD HH:MM:SS]", cmd_date, 1, 2);

/**
 ******************************************************************************
 * @brief      在主板时间与本地 RTC 相差至少五秒时同步。
 * @param[in]  unix_seconds:主板 UTC Unix 秒数，支持 2000～2099 年。
 * @retval     1:已设置 RTC。
 * @retval     0:时差小于五秒，无需设置。
 * @retval     负值:时间无效或 RTC 操作失败。
 ******************************************************************************
 */
int app_clock_sync(uint32_t unix_seconds)
{
    time_t seconds = unix_seconds;
    struct rtc_time target = {0};
    struct rtc_time local = {0};
    int err;

    if (!gmtime_r(&seconds, rtc_time_to_tm(&target)) || target.tm_year < 100
        || target.tm_year > 199)
    {
        return -EINVAL;
    }
    if (!device_is_ready(rtc_dev))
    {
        return -ENODEV;
    }

    k_mutex_lock(&clock_lock, K_FOREVER);
    err = rtc_get_time(rtc_dev, &local);
    if (!err)
    {
        int64_t delta = (int64_t)unix_seconds - timeutil_timegm64(rtc_time_to_tm(&local));

        if (delta > -5 && delta < 5)
        {
            k_mutex_unlock(&clock_lock);
            return 0;
        }
    }
    else if (err != -ENODATA)
    {
        k_mutex_unlock(&clock_lock);
        return err;
    }
    err = rtc_set_time(rtc_dev, &target);
    k_mutex_unlock(&clock_lock);
    if (err)
    {
        return err;
    }
    return 1;
}
