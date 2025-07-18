#include "analog_read.h"
#include "stdinclude.h"
#include "hardware/adc.h"
void ResponsiveAnalogRead::begin(int pin, bool sleepEnable, float snapMultiplier){
    adc_init();
    adc_gpio_init(pin);
    this->pin = pin;
    this->sleepEnable = sleepEnable;
    setSnapMultiplier(snapMultiplier);
    adc_gpio_init(pin);
}


void ResponsiveAnalogRead::update()
{
    adc_select_input(2);
    rawValue = adc_read();
    this->update(rawValue);
}

void ResponsiveAnalogRead::update(int rawValueRead)
{
    rawValue = rawValueRead;
    prevResponsiveValue = responsiveValue;
    responsiveValue = getResponsiveValue(rawValue);
    responsiveValueHasChanged = responsiveValue != prevResponsiveValue;
}

int ResponsiveAnalogRead::getResponsiveValue(int newValue)
{
    // if sleep and edge snap are enabled and the new value is very close to an edge, drag it a little closer to the edges
    // This'll make it easier to pull the output values right to the extremes without sleeping,
    // and it'll make movements right near the edge appear larger, making it easier to wake up
    if(sleepEnable && edgeSnapEnable) {
        if(newValue < activityThreshold) {
            newValue = (newValue * 2) - activityThreshold;
        } else if(newValue > analogResolution - activityThreshold) {
            newValue = (newValue * 2) - analogResolution + activityThreshold;
        }
    }

    // get difference between new input value and current smooth value
    unsigned int diff = abs(newValue - smoothValue);

    // measure the difference between the new value and current value
    // and use another exponential moving average to work out what
    // the current margin of error is
    errorEMA += ((newValue - smoothValue) - errorEMA) * 0.4;

    // if sleep has been enabled, sleep when the amount of error is below the activity threshold
    if(sleepEnable) {
        // recalculate sleeping status
        sleeping = abs(errorEMA) < activityThreshold;
    }

    // if we're allowed to sleep, and we're sleeping
    // then don't update responsiveValue this loop
    // just output the existing responsiveValue
    if(sleepEnable && sleeping) {
        return (int)smoothValue;
    }

    // use a 'snap curve' function, where we pass in the diff (x) and get back a number from 0-1.
    // We want small values of x to result in an output close to zero, so when the smooth value is close to the input value
    // it'll smooth out noise aggressively by responding slowly to sudden changes.
    // We want a small increase in x to result in a much higher output value, so medium and large movements are snappy and responsive,
    // and aren't made sluggish by unnecessarily filtering out noise. A hyperbola (f(x) = 1/x) curve is used.
    // First x has an offset of 1 applied, so x = 0 now results in a value of 1 from the hyperbola function.
    // High values of x tend toward 0, but we want an output that begins at 0 and tends toward 1, so 1-y flips this up the right way.
    // Finally the result is multiplied by 2 and capped at a maximum of one, which means that at a certain point all larger movements are maximally snappy

    // then multiply the input by SNAP_MULTIPLER so input values fit the snap curve better.
    float snap = snapCurve(diff * snapMultiplier);

    // when sleep is enabled, the emphasis is stopping on a responsiveValue quickly, and it's less about easing into position.
    // If sleep is enabled, add a small amount to snap so it'll tend to snap into a more accurate position before sleeping starts.
    if(sleepEnable) {
        snap *= 0.5 + 0.5;
    }

    // calculate the exponential moving average based on the snap
    smoothValue += (newValue - smoothValue) * snap;

    // ensure output is in bounds
    if(smoothValue < 0.0) {
        smoothValue = 0.0;
    } else if(smoothValue > analogResolution - 1) {
        smoothValue = analogResolution - 1;
    }

    // expected output is an integer
    return (int)smoothValue;
}

float ResponsiveAnalogRead::snapCurve(float x)
{
    float y = 1.0 / (x + 1.0);
    y = (1.0 - y) * 2.0;
    if(y > 1.0) {
        return 1.0;
    }
    return y;
}

void ResponsiveAnalogRead::setSnapMultiplier(float newMultiplier)
{
    if(newMultiplier > 1.0) {
        newMultiplier = 1.0;
    }
    if(newMultiplier < 0.0) {
        newMultiplier = 0.0;
    }
    snapMultiplier = newMultiplier;
}