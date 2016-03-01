/*
 * Written by: William Burgin (waburgin)
 *
 * File provides PID allocation / deallocation utilities and
 * management tools accordingly.
 *
 */

#include <pid.h>

pid_t
pidgen(void){
	// Return a random pid between 2 and 32767
	return (pid_t)(random() % (__PID_MAX + 1 - __PID_MIN) + __PID_MIN);	
}
