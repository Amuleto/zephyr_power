#ifndef BRINGUP_LED_H_
#define BRINGUP_LED_H_

/* Initialize the board LED. Return zero on success or a negative errno. */
int app_led_init(void);

/* Advance blinking once; steady modes remain unchanged. */
int app_led_update(void);

#endif /* BRINGUP_LED_H_ */
