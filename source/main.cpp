#include "STM32IotNode.h"

#define TIMER_SAMPLE

#if defined(GPIO_SAMPLE)
    #include "GPIOSample.h"
#elif defined(ADC_SAMPLE)
    #include "ADCSample.h"
#elif defined(SERIAL_SAMPLE)
    #include "SerialSample.h"
#elif defined(TIMER_SAMPLE)
    #include "TimerSample.h"
#else
    #include "BlinkSample.h"
#endif

using namespace codal;

int main()
{
    SAMPLE_MAIN();
    release_fiber();
}