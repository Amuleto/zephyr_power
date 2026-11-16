#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>

#include "app.h"
#include "board_startup.h"

static int cmd_uptime(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    shell_print(sh, "Uptime: %lld ms", (long long)k_uptime_get());
    return 0;
}

SHELL_CMD_ARG_REGISTER(uptime, NULL, "Show time since boot", cmd_uptime, 1, 0);

int main(void)
{
    if (board_init())
    {
        return 0;
    }

    return app_run();
}
