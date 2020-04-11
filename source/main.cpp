#include "wiring_digital.h"
#include "wiring_time.h"
#include "variant.h"
#include "board.h"

// Force init to be called *first*, i.e. before static object allocation.
// Otherwise, statically allocated objects that need HAL may fail.
__attribute__((constructor(101))) void premain()
{
  init();
}

int main()
{
    pinMode(LED1, OUTPUT);
    pinMode(LED2, OUTPUT);
    int pin_state = 0;

    while (1) {
        pin_state = !pin_state;
        if (pin_state) {
            digitalWrite(LED1, HIGH);
            digitalWrite(LED2, LOW);
        } else {
            digitalWrite(LED1, LOW);
            digitalWrite(LED2, HIGH);
        }
        delay(500);
    }
}