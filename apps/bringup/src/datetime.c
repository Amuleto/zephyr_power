#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/timeutil.h>
#include <time.h>
#include <stdio.h>
#include <errno.h>
#include <zephyr/drivers/rtc.h>
static const struct device *const rtc_dev = DEVICE_DT_GET(DT_ALIAS(rtc0));

/* UTC hardware RTC. Require explicit date setting after every boot. */
static K_MUTEX_DEFINE(clock_lock);



static int cmd_date(const struct shell *sh, size_t argc, char **argv)
{
    struct tm tm = {0};
    if (!device_is_ready(rtc_dev)) {
        shell_error(sh, "RTC device not ready");
        return -ENODEV;
    }
    if (argc == 3) {
        int year, month, day, hour, minute, second;
        char extra;
        if (sscanf(argv[1], "%d-%d-%d%c", &year, &month, &day, &extra) != 3 ||
            sscanf(argv[2], "%d:%d:%d%c", &hour, &minute, &second, &extra) != 3 ||
            year < 2000 || year > 2099 || month < 1 || month > 12 ||
            hour < 0 || hour > 23 || minute < 0 || minute > 59 ||
            second < 0 || second > 59) {
            shell_error(sh, "Use: date YYYY-MM-DD HH:MM:SS (UTC, 2000-2099)");
            return -EINVAL;
        }
        const int days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
        int max_day = days[month - 1] + (month == 2 && year % 4 == 0);
        if (day < 1 || day > max_day) {
            shell_error(sh, "Invalid calendar date");
            return -EINVAL;
        }
        tm.tm_year = year - 1900;
        tm.tm_mon = month - 1;
        tm.tm_mday = day;
        tm.tm_hour = hour;
        tm.tm_min = minute;
        tm.tm_sec = second;
        k_mutex_lock(&clock_lock, K_FOREVER);
        struct rtc_time value = {0};
        *rtc_time_to_tm(&value) = tm;
        int err = rtc_set_time(rtc_dev, &value);
        k_mutex_unlock(&clock_lock);
        if (err) {
            shell_error(sh, "RTC write failed: %d", err);
            return err;
        }
    } else if (argc != 1) {
        shell_error(sh, "Use: date [YYYY-MM-DD HH:MM:SS]");
        return -EINVAL;
    }
    k_mutex_lock(&clock_lock, K_FOREVER);
    struct rtc_time current = {0};
    int err = rtc_get_time(rtc_dev, &current);
    k_mutex_unlock(&clock_lock);
    if (err) {
        shell_error(sh, "RTC read failed: %d (set date after boot)", err);
        return err;
    }
    tm = *rtc_time_to_tm(&current);
    shell_print(sh, "%04d-%02d-%02d %02d:%02d:%02d UTC",
        tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
        tm.tm_hour, tm.tm_min, tm.tm_sec);
    return 0;
}

SHELL_CMD_ARG_REGISTER(date, NULL, "UTC RTC time: date [YYYY-MM-DD HH:MM:SS]",
                      cmd_date, 1, 2);
