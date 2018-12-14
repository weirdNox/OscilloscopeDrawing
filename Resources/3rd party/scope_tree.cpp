/*
 * 	Oscilloscope Christmas Tree
 *
 *  	Created: Dec 10, 2011
 *
 *	Author: John M. De Cristofaro
 *
 *	License: This code CC-BY-SA 3.0 and is unsupported.
 *		 (see creativecommons.org/licenses for info)
 *
 */

/* ****************************************************************************
   Fritzing is here:

   http://www.flickr.com/photos/johngineer/6496005491/sizes/z/in/photostream/

   in case you can't see the image, the following circuit is on both PWM ports

   R
   PWM OUT ----/\/\/\-----+------------ OUTPUT
   |
   === C
   |
   GND

   R = 10k
   C = 0.1uF

   Use of a 16Mhz xtal/ceramic resonator is strongly suggested.

   **************************************************************************** */

#define TRACE_DELAY	2500  // trace delay in uS. making this longer will
// result in a straighter drawing, but slower
// refresh rate. making it too short will result
// in an angular blob.

#define NUM_POINTS	19    // our tree is defined by 19 x/y coord. pairs

#define X               6     // attach scope channel 1 (X) to pin 6
#define Y               5     // attach scope channel 2 (y) to pin 5


// x coords for drawing the tree (in rough clockwise order, from bottom)
unsigned char x_points[NUM_POINTS] = {	110, 110, 50, 80, 65, 95, 80, 110, 95, 125,
                                        155, 140, 170, 155, 185, 170, 200, 140, 140 };

// y coords
unsigned char y_points[NUM_POINTS] = {	15, 35, 35, 85, 85, 135, 135, 185, 185, 235,
                                        185, 185, 135, 135, 85, 85, 35, 35, 15 };

void setup()
{
    pinMode(X, OUTPUT);
    pinMode(Y, OUTPUT);

    // The following sets the PWM clock to maximum on the Arduino(no CPU clock division)
    // DO NOT CHANGE THESE UNLESS YOU KNOW WHAT YOU ARE DOING!

    TCCR0A = (	1<<COM0A1 | 0<<COM0A0 |		// clear OC0A on compare match (hi-lo PWM)
                1<<COM0B1 | 0<<COM0B0 |		// clear OC0B on compare match (hi-lo PWM)
                1<<WGM01  | 1<<WGM00);		// set PWM lines at 0xFF

    TCCR0B = (	0<<FOC0A | 0<<FOC0B |		// no force compare match
                0<<WGM02 |			// set PWM lines at 0xFF
                0<<CS02	 | 0<<CS01 |		// use system clock (no divider)
                1<<CS00 );

    TIMSK0 = (	0<<OCIE0B | 0<<TOIE0 |
                0<<OCIE0A );

}

void loop()
{
    unsigned char t;
    {
        for(t = 0; t < NUM_POINTS; t++)		// run through the points in x & y
        {
            analogWrite(X, x_points[t]);
            analogWrite(Y, y_points[t]);
            delayMicroseconds(TRACE_DELAY);		// wait TRACE_DELAY microseconds
        }
    }
}
