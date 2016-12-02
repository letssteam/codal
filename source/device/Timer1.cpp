#include "Timer1.h"
#include "DeviceEvent.h"
#include "CodalCompat.h"
#include "DeviceSystemTimer.h"
#include <avr/io.h>
#include <avr/interrupt.h>

static uint64_t current_time_us = 0;

static ClockPrescalerConfig* clock_configuration = NULL;
static uint32_t overflow_period = 0;
static uint16_t timer_id = 0;

static uint32_t clock_frequency = 0;

const static ClockPrescalerConfig Timer1Prescalers[TIMER_ONE_PRESCALER_OPTIONS] = {
    {1, _BV(CS10)},
    {8, _BV(CS11)},
    {64, _BV(CS10) | _BV(CS11)},
    {256, _BV(CS12)},
    {1024, _BV(CS10) | _BV(CS12)},
};

LIST_HEAD(event_list);

#define TIMER_1_MAX     65536  // 16 bit

void set_interrupt(uint32_t time_us)
{
    if(TIMSK1 & _BV(OCIE1A))
        return;

    if(time_us > overflow_period)
        return;

    uint16_t counter = 0;

    // calculate the timer value based on a number
    uint32_t tick_time = clock_frequency * clock_configuration->prescaleValue;

    uint16_t new_value = (time_us * 1000) / tick_time;

    counter = TCNT1;

    if(new_value < counter)
        return;

    OCR1A = new_value;

    TIMSK1 |= _BV(OCIE1A);

    return;
}

void consume_events(uint8_t compareEvent)
{
    if(list_empty(&event_list))
        return;

    ClockEvent* tmp = NULL;
    struct list_head *iter, *q = NULL;

    uint32_t period_us = overflow_period;

    uint8_t interrupt_set = 0;
    uint8_t first = 1;

    list_for_each_safe(iter, q, &event_list)
    {
        tmp = list_entry(iter, ClockEvent, list);

        // if we have received a compare event, we know we will be ripping off the top!
        if(first && compareEvent)
        {
            first = 0;

            // use a rough approximation of the time...
            period_us = (uint32_t)tmp->countUs;

            current_time_us += period_us;

            // fire our event and process the next event
            DeviceEvent(timer_id, tmp->value);

            // remove from the event list
            list_del(iter);

            // if this event is repeating, reset our timestamp
            if(tmp->period == 0)
            {
                delete tmp;
                continue;
            }
            else
            {
                // update our count, and readd to our event list
                tmp->countUs = tmp->period;
                tmp->addToList(&event_list);
            }
        }
        else
            tmp->countUs -= period_us;

        if(!interrupt_set && tmp->countUs < overflow_period)
        {
            // calculate the timer value based on timestamp us
            uint32_t tickTime = clock_frequency * clock_configuration->prescaleValue;

            OCR1A = (uint16_t)((uint32_t)(tmp->countUs * 1000) / tickTime);
            TIMSK1 |= _BV(OCIE1A);
            interrupt_set = 1;
        }
    }
}

ISR(TIMER1_OVF_vect)
{
    // update our timestamp and consume any Clock events.
    current_time_us += overflow_period;
    consume_events(0);
}

ISR(TIMER1_COMPA_vect)
{
    // unset our compare interrupt and consume any Clock events.
    TIMSK1 &= ~_BV(OCIE1A);
    __disable_irq();
    TCNT1 = 0;
    __enable_irq();
    consume_events(1);
}

/**
  * Reads the Timer1 register and updates the timestamp accordingly.
  */
void Timer1::read()
{
    uint16_t counter = 0;

    __disable_irq();
    counter = TCNT1;
    TCNT1 = 0;
    __enable_irq();

    if(clock_configuration->prescaleValue == 8)
        counter = (counter >> 1);
    else
    {
        // scale our instruction time based on our current prescaler
        uint32_t tickTime = clock_frequency * clock_configuration->prescaleValue;
        counter = (tickTime * counter) / 1000;
    }

    current_time_us += counter;
}

/**
  * Sets the clock prescaler based on a given precision.
  *
  * @param precisionUs The precision in microseconds
  */
int Timer1::setClockSelect(uint64_t precisionUs)
{
    uint32_t timePerTick = 0;

    precisionUs *= 1000;

    clock_configuration = (ClockPrescalerConfig*)&Timer1Prescalers[TIMER_ONE_PRESCALER_OPTIONS - 1];

    // calculate the nearest clock configuration to the given period.
    for(uint8_t i = TIMER_ONE_PRESCALER_OPTIONS - 1; i >= 0; i--)
    {
        // timer per tick = (prescale) * ( 1 / frequency)
        timePerTick = clock_configuration->prescaleValue * clock_frequency;

        if(timePerTick <= precisionUs)
            break;

        clock_configuration = (ClockPrescalerConfig*)&Timer1Prescalers[i];
    }

    uint32_t timerTickNs = clock_frequency * clock_configuration->prescaleValue;

    overflow_period = ((TIMER_1_MAX - 1) * timerTickNs) / 1000;

    overflow_period >>= 1;

    return DEVICE_OK;
}

/**
  * Constructor for an instance of Timer1.
  *
  * @param id The id to use for the message bus when transmitting events.
  */
Timer1::Timer1(uint16_t id)
{
    this->id = timer_id = id;
    clock_frequency = 1000000 / ((uint32_t)F_CPU / 1000);
}

/**
  * Returns the id for this timer instance
  */
int Timer1::getId()
{
    return this->id;
}

/**
  * Initialises and starts this Timer1 instance
  */
int Timer1::init()
{
    if(status & SYSTEM_CLOCK_INIT)
        return DEVICE_OK;

    if(system_timer_get_instance() == NULL)
        system_timer_set_instance(this);

    // clear control register A
    TCCR1A = 0;

    // set mode 0: normal timer
    TCCR1B = 0;

    start();

    status |= SYSTEM_CLOCK_INIT;

    return DEVICE_OK;
}

/**
  * Sets the current time tracked by this Timer1 instance
  * in milliseconds
  *
  * @param timestamp the new time for this Timer1 instance in milliseconds
  */
int Timer1::setTime(uint64_t timestamp)
{
    return setTimeUs(timestamp * 1000);
}

/**
  * Sets the current time tracked by this Timer1 instance
  * in microseconds
  *
  * @param timestamp the new time for this Timer1 instance in microseconds
  */
int Timer1::setTimeUs(uint64_t timestamp)
{
    current_time_us = timestamp;
    return DEVICE_OK;
}

/**
  * Retrieves the current time tracked by this Timer1 instance
  * in milliseconds
  *
  * @return the timestamp in milliseconds
  */
uint64_t Timer1::getTime()
{
    return getTimeUs() / 1000;
}

/**
  * Retrieves the current time tracked by this Timer1 instance
  * in microseconds
  *
  * @return the timestamp in microseconds
  */
uint64_t Timer1::getTimeUs()
{
    read();
    return current_time_us;
}

/**
  * Configures this Timer1 instance to fire an event after period
  * milliseconds.
  *
  * @param period the period to wait until an event is triggered, in milliseconds.
  *
  * @param value the value to place into the Events' value field.
  */
int Timer1::eventAfter(uint64_t interval, uint16_t value)
{
    return eventAfterUs(interval * 1000, value);
}

/**
  * Configures this Timer1 instance to fire an event after period
  * microseconds.
  *
  * @param period the period to wait until an event is triggered, in microseconds.
  *
  * @param value the value to place into the Events' value field.
  */
int Timer1::eventAfterUs(uint64_t interval, uint16_t value)
{
    if(new ClockEvent(interval, value, &event_list))
        return DEVICE_OK;

    return DEVICE_NO_RESOURCES;
}

/**
  * Configures this Timer1 instance to fire an event every period
  * milliseconds.
  *
  * @param period the period to wait until an event is triggered, in milliseconds.
  *
  * @param value the value to place into the Events' value field.
  */
int Timer1::eventEvery(uint64_t period, uint16_t value)
{
    return eventEveryUs(period * 1000, value);
}

/**
  * Configures this Timer1 instance to fire an event every period
  * microseconds.
  *
  * @param period the period to wait until an event is triggered, in microseconds.
  *
  * @param value the value to place into the Events' value field.
  */
int Timer1::eventEveryUs(uint64_t period, uint16_t value)
{
    ClockEvent* clk = new ClockEvent(period, value, &event_list, true);

    if(!clk)
        return DEVICE_NO_RESOURCES;

    if(event_list.next == &clk->list && period < overflow_period)
    {
        __disable_irq();
        set_interrupt(period);
        __enable_irq();
    }

    return DEVICE_OK;
}

/**
  * Start this Timer1 instance.
  *
  * @param precisionUs The precisions that the timer should use. Defaults to
  *        TIMER_ONE_DEFAULT_PRECISION_US (1 us)
  */
int Timer1::start(uint64_t precisionUs)
{
    TIMSK1 = _BV(TOIE1); // interrupt on overflow

    setClockSelect(precisionUs);

    uint8_t sreg = SREG;
    __disable_irq();
    TCNT1 = 0;
    SREG = sreg;

    TCCR1B &= ~(_BV(CS10) | _BV(CS11) | _BV(CS12));
    TCCR1B = (uint8_t)clock_configuration->register_config;

    __enable_irq();

    return DEVICE_OK;
}

/**
  * Stop this Timer1 instance
  */
int Timer1::stop()
{
    TIMSK1 &= ~_BV(TOIE1); // disable interrupt on overflow
    TCCR1B &= ~(_BV(CS10) | _BV(CS11) | _BV(CS12));
    return DEVICE_OK;
}

Timer1::~Timer1()
{
    stop();
}