#include "hal_gpio.h"
#include "hal_platform_gd32l23x.h"
#include "gd32l23x.h"

typedef struct {
    uint32_t gpio_port;
    uint32_t gpio_clk;
    uint32_t pin;
    uint8_t  active_low;
    uint8_t  is_led;
} pin_map_t;

typedef struct {
    pin_map_t pin;
    uint8_t   initialized;
} pin_runtime_t;

static const pin_map_t g_led_map[HAL_LED_ID_MAX] = {
    [HAL_LED_ID_0] = { .gpio_port = GPIOA, .gpio_clk = RCU_GPIOA, .pin = GPIO_PIN_7, .active_low = 1, .is_led = 1 },
    [HAL_LED_ID_1] = { .gpio_port = GPIOA, .gpio_clk = RCU_GPIOA, .pin = GPIO_PIN_8, .active_low = 1, .is_led = 1 },
};

static const pin_map_t g_user_pin_map[] = {
    [HAL_PIN_MD0] = { .gpio_port = GPIOB, .gpio_clk = RCU_GPIOB, .pin = GPIO_PIN_8, .active_low = 0, .is_led = 0 },
    [HAL_PIN_AUX] = { .gpio_port = GPIOB, .gpio_clk = RCU_GPIOB, .pin = GPIO_PIN_9, .active_low = 0, .is_led = 0 },
};

#define USER_PIN_COUNT (sizeof(g_user_pin_map) / sizeof(g_user_pin_map[0]))

static pin_runtime_t g_led_rt[HAL_LED_ID_MAX];
static pin_runtime_t g_pin_rt[16];

static const pin_map_t *find_pin_map(hal_pin_id_t pin_id, uint8_t *is_led)
{
    if (pin_id < HAL_LED_ID_MAX) {
        if (is_led) *is_led = 1;
        return &g_led_map[pin_id];
    }
    if (pin_id < USER_PIN_COUNT) {
        if (is_led) *is_led = 0;
        return &g_user_pin_map[pin_id];
    }
    return NULL;
}

static pin_runtime_t *get_pin_rt(hal_pin_id_t pin_id)
{
    uint8_t is_led;
    const pin_map_t *map = find_pin_map(pin_id, &is_led);
    if (!map) return NULL;
    if (is_led) return &g_led_rt[pin_id];

    if (pin_id < 16) return &g_pin_rt[pin_id];
    return NULL;
}

static int gpio_clock_and_setup(const pin_map_t *pin, uint32_t mode, uint32_t pud)
{
    rcu_periph_clock_enable(pin->gpio_clk);
    gpio_mode_set(pin->gpio_port, mode, pud, pin->pin);
    if (mode == GPIO_MODE_OUTPUT) {
        gpio_output_options_set(pin->gpio_port, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, pin->pin);
        if (pin->active_low) {
            gpio_bit_set(pin->gpio_port, pin->pin);
        } else {
            gpio_bit_reset(pin->gpio_port, pin->pin);
        }
    }
    return HAL_OK;
}

int hal_gpio_output_init(hal_pin_id_t pin_id, hal_gpio_pull_t pull)
{
    uint32_t pud;
    switch (pull) {
        case HAL_GPIO_PULL_UP:   pud = GPIO_PUPD_PULLUP; break;
        case HAL_GPIO_PULL_DOWN: pud = GPIO_PUPD_PULLDOWN; break;
        default:                 pud = GPIO_PUPD_NONE; break;
    }

    uint8_t is_led;
    const pin_map_t *map = find_pin_map(pin_id, &is_led);
    if (!map) return HAL_ERR_INVAL;

    pin_runtime_t *rt = get_pin_rt(pin_id);
    if (rt) {
        rt->pin = *map;
        rt->initialized = 1;
    }

    return gpio_clock_and_setup(map, GPIO_MODE_OUTPUT, pud);
}

int hal_gpio_input_init(hal_pin_id_t pin_id, hal_gpio_pull_t pull)
{
    uint32_t pud;
    switch (pull) {
        case HAL_GPIO_PULL_UP:   pud = GPIO_PUPD_PULLUP; break;
        case HAL_GPIO_PULL_DOWN: pud = GPIO_PUPD_PULLDOWN; break;
        default:                 pud = GPIO_PUPD_NONE; break;
    }

    uint8_t is_led;
    const pin_map_t *map = find_pin_map(pin_id, &is_led);
    if (!map) return HAL_ERR_INVAL;

    pin_runtime_t *rt = get_pin_rt(pin_id);
    if (rt) {
        rt->pin = *map;
        rt->initialized = 1;
    }

    return gpio_clock_and_setup(map, GPIO_MODE_INPUT, pud);
}

int hal_gpio_output_set(hal_pin_id_t pin_id, uint8_t level)
{
    uint8_t is_led;
    const pin_map_t *map = find_pin_map(pin_id, &is_led);
    if (!map) return HAL_ERR_INVAL;

    uint8_t actual = map->active_low ? !level : level;
    if (actual) {
        gpio_bit_set(map->gpio_port, map->pin);
    } else {
        gpio_bit_reset(map->gpio_port, map->pin);
    }
    return HAL_OK;
}

int hal_gpio_output_get(hal_pin_id_t pin_id, uint8_t *level)
{
    if (!level) return HAL_ERR_INVAL;
    uint8_t is_led;
    const pin_map_t *map = find_pin_map(pin_id, &is_led);
    if (!map) return HAL_ERR_INVAL;

    uint8_t raw = (gpio_output_bit_get(map->gpio_port, map->pin) != RESET) ? 1 : 0;
    *level = map->active_low ? !raw : raw;
    return HAL_OK;
}

int hal_gpio_input_read(hal_pin_id_t pin_id, uint8_t *level)
{
    if (!level) return HAL_ERR_INVAL;
    uint8_t is_led;
    const pin_map_t *map = find_pin_map(pin_id, &is_led);
    if (!map) return HAL_ERR_INVAL;

    uint8_t raw = (gpio_input_bit_get(map->gpio_port, map->pin) != RESET) ? 1 : 0;
    *level = map->active_low ? !raw : raw;
    return HAL_OK;
}

int hal_gpio_toggle(hal_pin_id_t pin_id)
{
    uint8_t is_led;
    const pin_map_t *map = find_pin_map(pin_id, &is_led);
    if (!map) return HAL_ERR_INVAL;

    gpio_bit_toggle(map->gpio_port, map->pin);
    return HAL_OK;
}

int hal_gpio_irq_config(hal_pin_id_t pin_id, hal_gpio_irq_edge_t edge,
                        hal_gpio_irq_callback_t callback, uint8_t priority)
{
    uint8_t is_led;
    const pin_map_t *map = find_pin_map(pin_id, &is_led);
    if (!map) return HAL_ERR_INVAL;

    uint32_t exti_line = 0;
    uint32_t exti_port_source = 0;
    uint32_t exti_pin_source = 0;
    uint8_t  irq_channel = 0;
    uint32_t trig_mode;

    uint32_t pin_num = __builtin_ctz(map->pin);

    if (map->gpio_port == GPIOA) {
        exti_port_source = EXTI_SOURCE_GPIOA;
    } else if (map->gpio_port == GPIOB) {
        exti_port_source = EXTI_SOURCE_GPIOB;
    } else if (map->gpio_port == GPIOC) {
        exti_port_source = EXTI_SOURCE_GPIOC;
    } else {
        return HAL_ERR_INVAL;
    }

    switch (pin_num) {
        case 0:  exti_line = EXTI_0; exti_pin_source = EXTI_SOURCE_PIN0;  irq_channel = EXTI0_IRQn;      break;
        case 1:  exti_line = EXTI_1; exti_pin_source = EXTI_SOURCE_PIN1;  irq_channel = EXTI1_IRQn;      break;
        case 2:  exti_line = EXTI_2; exti_pin_source = EXTI_SOURCE_PIN2;  irq_channel = EXTI2_IRQn;      break;
        case 3:  exti_line = EXTI_3; exti_pin_source = EXTI_SOURCE_PIN3;  irq_channel = EXTI3_IRQn;      break;
        case 4:  exti_line = EXTI_4; exti_pin_source = EXTI_SOURCE_PIN4;  irq_channel = EXTI4_IRQn;      break;
        case 5: case 6: case 7: case 8: case 9:
            exti_line = EXTI_5 + pin_num - 5;
            exti_pin_source = EXTI_SOURCE_PIN5 + pin_num - 5;
            irq_channel = EXTI5_9_IRQn;
            break;
        default: return HAL_ERR_INVAL;
    }

    switch (edge) {
        case HAL_GPIO_IRQ_RISING:  trig_mode = EXTI_TRIG_RISING;  break;
        case HAL_GPIO_IRQ_FALLING: trig_mode = EXTI_TRIG_FALLING; break;
        case HAL_GPIO_IRQ_BOTH:    trig_mode = EXTI_TRIG_BOTH;    break;
        default: return HAL_ERR_INVAL;
    }

    rcu_periph_clock_enable(RCU_SYSCFG);
    syscfg_exti_line_config(exti_port_source, exti_pin_source);
    exti_init(exti_line, EXTI_INTERRUPT, trig_mode);

    if (callback) {
        exti_interrupt_enable(exti_line);
        exti_interrupt_flag_clear(exti_line);
        nvic_irq_enable(irq_channel, priority);
    } else {
        exti_interrupt_disable(exti_line);
    }

    (void)callback;
    return HAL_OK;
}

int hal_led_init(hal_led_id_t led_id)
{
    return hal_gpio_output_init(led_id, HAL_GPIO_PULL_NONE);
}

int hal_led_on(hal_led_id_t led_id)
{
    return hal_gpio_output_set(led_id, 1);
}

int hal_led_off(hal_led_id_t led_id)
{
    return hal_gpio_output_set(led_id, 0);
}

int hal_led_toggle(hal_led_id_t led_id)
{
    return hal_gpio_toggle(led_id);
}
