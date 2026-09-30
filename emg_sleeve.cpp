#include <cstdio>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"

int main() {
    stdio_init_all();
    if (cyw43_arch_init()) {
        printf("cyw43 init failed\n");
        return 1;
    }
    bool on = false;
    while (true) {
        on = !on;
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, on);
        printf("led %d\n", on);
        sleep_ms(500);
    }
}