#pragma once
#pragma GCC diagnostic ignored "-Wparentheses"
/*
Debugging definitions
Definition for the hardware LED variations:
** (a) one LED with 4 characters vs. two with a total of 8
** (b) whether to implement an up/down flip for projection of that LED through a lens
*/
#define USE_SERIAL 1 // define to 0 to eliminate access to Serial
//#define DEBUG_TO_SERIAL
#define FLIPPED_LED_UPDOWN 0
#define DUAL_ROW_LED_DISPLAY 0
#if defined(DEBUG_TO_SERIAL) && (USE_SERIAL > 0)
#define DEBUG_OUTPUT1(a) Serial.print(a)
#define DEBUG_OUTPUT2(a, b) Serial.print(a,b)
#define DEBUG_STATEMENT(x) x
#else
#define DEBUG_OUTPUT1(a) 
#define DEBUG_OUTPUT2(a, b) 
#define DEBUG_STATEMENT(X) 
#endif

