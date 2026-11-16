#ifndef BRINGUP_BOARD_GPIO_H_
#define BRINGUP_BOARD_GPIO_H_

#include <stdbool.h>

enum board_gpio_output_e
{
    BOARD_GPIO_OUT_JDQ1,      /**< 预充继电器 JDQ1。 */
    BOARD_GPIO_OUT_JDQ2,      /**< 预充继电器 JDQ2。 */
    BOARD_GPIO_OUT_JDQ3,      /**< 预充继电器 JDQ3。 */
    BOARD_GPIO_OUT_JDQ4,      /**< 预充继电器 JDQ4。 */
    BOARD_GPIO_OUT_LED,       /**< 板载指示灯。 */
    BOARD_GPIO_OUT_RS485_DE,  /**< RS485 方向，有效为发送。 */
    BOARD_GPIO_OUT_PE,       /**< PE 对外输出。 */
    BOARD_GPIO_OUT_COUNT,    /**< 输出数量。 */
};

/**
 ******************************************************************************
 * @brief      初始化板级 GPIO 输出，默认置为非有效态。
 * @retval     0:初始化成功。
 * @retval     负值:GPIO 未就绪或配置失败。
 *
 * @details    在线程上下文中调用一次。
 ******************************************************************************
 */
int app_board_gpio_init(void);

/**
 ******************************************************************************
 * @brief      按设备树定义的极性设置指定输出的逻辑状态。
 * @param[in]  output:输出信号。
 * @param[in]  active:true 表示有效，false 表示非有效。
 * @retval     0:设置成功。
 * @retval     -EINVAL:输出信号无效。
 * @retval     -EAGAIN:GPIO 尚未初始化。
 * @retval     其他负值:GPIO 驱动返回的错误码。
 ******************************************************************************
 */
int app_board_gpio_write(enum board_gpio_output_e output, bool active);

/**
 ******************************************************************************
 * @brief      设置指定输出的物理电平。
 * @param[in]  output:输出信号。
 * @param[in]  high:true 表示高电平，false 表示低电平。
 * @retval     0:设置成功。
 * @retval     -EINVAL:输出信号无效。
 * @retval     -EAGAIN:GPIO 尚未初始化。
 * @retval     其他负值:GPIO 驱动返回的错误码。
 ******************************************************************************
 */
int app_board_gpio_write_raw(enum board_gpio_output_e output, bool high);

/**
 ******************************************************************************
 * @brief      查找映射到 GPIOB 指定引脚的输出信号。
 * @param[in]  pin:GPIOB 引脚编号。
 * @param[out] output:匹配时写入逻辑输出编号。
 * @retval     0:找到输出。
 * @retval     -EINVAL:输出指针为空或引脚未映射。
 ******************************************************************************
 */
int app_board_gpio_find_pb(unsigned int pin, enum board_gpio_output_e *output);

#endif /* BRINGUP_BOARD_GPIO_H_ */
