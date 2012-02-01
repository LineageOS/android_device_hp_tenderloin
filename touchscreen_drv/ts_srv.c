/*
 * This is a userspace touchscreen driver for cypress ctma395 as used
 * in HP Touchpad configured for WebOS.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 * The code was written from scratch, the hard math and understanding the
 * device output by jonpry @ gmail
 * uinput bits and the rest by Oleg Drokin green@linuxhacker.ru
 * Multitouch detection by Rafael Brune mail@rbrune.de
 *
 * Copyright (c) 2011 CyanogenMod Touchpad Project.
 *
 *
 */

#include <linux/input.h>
#include <linux/uinput.h>
#include <linux/hsuart.h>
#include <sched.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/select.h>

#if 1
// This is for Android
#define UINPUT_LOCATION "/dev/uinput"
#else
// This is for webos and possibly other Linuxes
#define UINPUT_LOCATION "/dev/input/uinput"
#endif

/* Set to 1 to print coordinates to stdout. */
#define DEBUG 0

/* Set to 1 to see raw data from the driver */
#define RAW_DATA_DEBUG 0
// Removes values below threshold for easy reading, set to 0 to see everything.
// A value of 2 should remove most unwanted output
#define RAW_DATA_THRESHOLD 0

// Set to 1 to see event logging
#define EVENT_DEBUG 0
// Set to 1 to enable tracking ID logging
#define TRACK_ID_DEBUG 0

#define AVG_FILTER 1

#define USERSPACE_270_ROTATE 0

#define RECV_BUF_SIZE 1540
#define LIFTOFF_TIMEOUT 25000

#define MAX_TOUCH 10 // Max touches that will be reported

#define MAX_DELTA_FILTER 1 // Set to 1 to use max delta filtering
// This value determines when a large distance change between one touch
// and another will be reported as 2 separate touches instead of a swipe.
// This distance is in pixels.
#define MAX_DELTA 130
// If we exceed MAX_DELTA, we'll check the previous touch point to see if
// it was moving fairly far.  If the previous touch moved far enough and is
// within the same direction / angle, we'll allow it to be a swipe.
// This is the distance theshold that the previous touch must have traveled.
// This value is in pixels.
#define MIN_PREV_DELTA 40
// This is the angle, plus or minus that the previous direction must have
// been traveling.  This angle is an arctangent. (atan2)
#define MAX_DELTA_ANGLE 0.25
#define MAX_DELTA_DEBUG 0 // Set to 1 to see debug logging for max delta

// Any touch above this threshold is immediately reported to the system
#define TOUCH_INITIAL_THRESHOLD 32
// Previous touches that have already been reported will continue to be
// reported so long as they stay above this threshold
#define TOUCH_CONTINUE_THRESHOLD 20
// New touches above this threshold but below TOUCH_INITIAL_THRESHOLD will not
// be reported unless the touch continues to appear.  This is designed to
// filter out brief, low threshold touches that may not be valid.
#define TOUCH_DELAY_THRESHOLD 24
// Delay before a touch above TOUCH_DELAY_THRESHOLD but below
// TOUCH_INITIAL_THRESHOLD will be reported.  We will wait and see if this
// touch continues to show up in future buffers before reporting the event.
#define TOUCH_DELAY 2
// Threshold for end of a large area. This value needs to be set low enough
// to filter out large touch areas and tends to be related to other touch
// thresholds.
#define LARGE_AREA_UNPRESS 32 //TOUCH_CONTINUE_THRESHOLD
#define LARGE_AREA_FRINGE 15 // Threshold for large area fringe

// Enables filtering of a single touch to make it easier to long press.
// Keeps the initial touch point the same so long as it stays within
// the radius (note it's not really a radius and is actually a square)
#define DEBOUNCE_FILTER 1 // Set to 1 to enable the debouce filter
#define DEBOUNCE_RADIUS 10 // Radius for debounce in pixels
#define DEBOUNCE_DEBUG 0 // Set to 1 to enable debounce logging

// This is used to help calculate ABS_TOUCH_MAJOR
// This is roughly the value of 1024 / 40 or 768 / 30
#define PIXELS_PER_POINT 25

// This enables slots for the type B multi-touch protocol.
// The kernel must support slots (ABS_MT_SLOT). The TouchPad 2.6.35 kernel
// doesn't seem to handle liftoffs with protocol B properly so leave it off
// for now.
#define USE_B_PROTOCOL 0

/** ------- end of user modifiable parameters ---- */
#define MIN(X,Y) ((X) < (Y) ? (X) : (Y))
#define MAX(X,Y) ((X) > (Y) ? (X) : (Y))
#define isBetween(A, B, C) ( ((A-B) > 0) && ((A-C) < 0) )
// We square MAX_DELTA to prevent the need to use sqrt
#define MAX_DELTA_SQ (MAX_DELTA * MAX_DELTA)
#define MIN_PREV_DELTA_SQ (MIN_PREV_DELTA * MIN_PREV_DELTA)

#define X_AXIS_POINTS  30
#define Y_AXIS_POINTS  40
#define X_AXIS_MINUS1 X_AXIS_POINTS - 1 // 29
#define Y_AXIS_MINUS1 Y_AXIS_POINTS - 1 // 39

#if USERSPACE_270_ROTATE
#define X_RESOLUTION  768
#define Y_RESOLUTION 1024
#define X_LOCATION_VALUE ((float)X_RESOLUTION) / ((float)X_AXIS_MINUS1)
#define Y_LOCATION_VALUE ((float)Y_RESOLUTION) / ((float)Y_AXIS_MINUS1)
#else
#define X_RESOLUTION 1024
#define Y_RESOLUTION  768
#define X_LOCATION_VALUE ((float)X_RESOLUTION) / ((float)Y_AXIS_MINUS1)
#define Y_LOCATION_VALUE ((float)Y_RESOLUTION) / ((float)X_AXIS_MINUS1)
#endif // USERSPACE_270_ROTATE

#define X_RESOLUTION_MINUS1 X_RESOLUTION - 1
#define Y_RESOLUTION_MINUS1 Y_RESOLUTION - 1

struct touchpoint {
	int pw;
	float i;
	float j;
#if USE_B_PROTOCOL
	int slot;
#endif
	int tracking_id;
	int prev_loc;
#if MAX_DELTA_FILTER
	float direction;
	int distance;
#endif
	int touch_major;
	int x;
	int y;
	int raw_x;
	int raw_y;
	int isValid;
	int touch_delay;
};

struct touchpoint tpoint[MAX_TOUCH];
struct touchpoint prevtpoint[MAX_TOUCH];
struct touchpoint prev2tpoint[MAX_TOUCH];

unsigned char cline[64];
unsigned int cidx = 0;
unsigned char matrix[X_AXIS_POINTS][Y_AXIS_POINTS];
int invalid_matrix[X_AXIS_POINTS][Y_AXIS_POINTS];
int uinput_fd;
#if USE_B_PROTOCOL
int slot_in_use[MAX_TOUCH];
#endif

int send_uevent(int fd, __u16 type, __u16 code, __s32 value)
{
	struct input_event event;

#if EVENT_DEBUG
	char ctype[20], ccode[20];
	switch (type) {
		case EV_ABS:
			strcpy(ctype, "EV_ABS");
			break;
		case EV_KEY:
			strcpy(ctype, "EV_KEY");
			break;
		case EV_SYN:
			strcpy(ctype, "EV_SYN");
			break;
	}
	switch (code) {
		case ABS_MT_SLOT:
			strcpy(ccode, "ABS_MT_SLOT");
			break;
		case ABS_MT_TRACKING_ID:
			strcpy(ccode, "ABS_MT_TRACKING_ID");
			break;
		case ABS_MT_TOUCH_MAJOR:
			strcpy(ccode, "ABS_MT_TOUCH_MAJOR");
			break;
		case ABS_MT_POSITION_X:
			strcpy(ccode, "ABS_MT_POSITION_X");
			break;
		case ABS_MT_POSITION_Y:
			strcpy(ccode, "ABS_MT_POSITION_Y");
			break;
		case SYN_MT_REPORT:
			strcpy(ccode, "SYN_MT_REPORT");
			break;
		case SYN_REPORT:
			strcpy(ccode, "SYN_REPORT");
			break;
		case BTN_TOUCH:
			strcpy(ccode, "BTN_TOUCH");
			break;
	}
	printf("event type: '%s' code: '%s' value: %i \n", ctype, ccode, value);
#endif

	memset(&event, 0, sizeof(event));
	event.type = type;
	event.code = code;
	event.value = value;

	if (write(fd, &event, sizeof(event)) != sizeof(event)) {
		fprintf(stderr, "Error on send_event %d", sizeof(event));
		return -1;
	}

	return 0;
}

#if AVG_FILTER
void avg_filter(struct touchpoint *t) {
#if DEBUG
	printf("before: x=%d, y=%d", t->x, t->y);
#endif 
	float total_div = 6.0;
	int xsum = 4 * t->raw_x + 2 * prevtpoint[t->prev_loc].raw_x;
	int ysum = 4 * t->raw_y + 2 * prevtpoint[t->prev_loc].raw_y;
	if(prevtpoint[t->prev_loc].prev_loc > -1) {
		xsum += prev2tpoint[prevtpoint[t->prev_loc].prev_loc].raw_x;
		ysum += prev2tpoint[prevtpoint[t->prev_loc].prev_loc].raw_y;
		total_div += 1.0;
	}
	t->x = xsum / total_div;
	t->y = ysum / total_div;
#if DEBUG
	printf("|||| after: x=%d, y=%d\n", t->x, t->y);
#endif
}
#endif // AVG_FILTER

#if USE_B_PROTOCOL
void liftoff_slot(int slot) {
	// sends a liftoff indicator for a specific slot
#if EVENT_DEBUG
	printf("liftoff slot function, lifting off slot: %i\n", slot);
#endif
	// According to the Linux kernel documentation, this is the right events
	// to send for protocol B, but the TouchPad 2.6.35 kernel doesn't seem to
	// handle them correctly.
	send_uevent(uinput_fd, EV_ABS, ABS_MT_SLOT, slot);
	send_uevent(uinput_fd, EV_ABS, ABS_MT_TRACKING_ID, -1);
}
#endif // USE_B_PROTOCOL

void liftoff(void)
{
#if USE_B_PROTOCOL
	// Send liftoffs for any slots that haven't been lifted off
	int i;
	for (i=0; i<MAX_TOUCH; i++) {
		if (slot_in_use[i]) {
			slot_in_use[i] = 0;
			liftoff_slot(i);
		}
	}
#endif
	// Sends liftoff events - nothing is touching the screen
#if EVENT_DEBUG
	printf("liftoff function\n");
#endif
#if !USE_B_PROTOCOL
	send_uevent(uinput_fd, EV_SYN, SYN_MT_REPORT, 0);
#endif
	send_uevent(uinput_fd, EV_SYN, SYN_REPORT, 0);
}

void determine_area_loc_fringe(float *isum, float *jsum, int *tweight, int i,
	int j, int cur_touch_id){
	// Set fringe point to used for this touch point
	invalid_matrix[i][j] = cur_touch_id;

	// Track touch values to help determine the pixel x, y location
	float powered = pow(matrix[i][j], 1.5);
	*tweight += powered;
	*isum += powered * i;
	*jsum += powered * j;

	// Check the nearby points to see if they are above LARGE_AREA_FRINGE
	// but still decreasing in value to ensure that they are part of the same
	// touch and not a nearby, pinching finger.
	if (i > 0 && invalid_matrix[i-1][j] != cur_touch_id)
	{
		if (matrix[i-1][j] >= LARGE_AREA_FRINGE &&
			matrix[i-1][j] < matrix[i][j])
			determine_area_loc_fringe(isum, jsum, tweight, i - 1, j,
				cur_touch_id);
	}
	if (i < X_AXIS_MINUS1 && invalid_matrix[i+1][j] != cur_touch_id)
	{
		if (matrix[i+1][j] >= LARGE_AREA_FRINGE &&
			matrix[i+1][j] < matrix[i][j])
			determine_area_loc_fringe(isum, jsum, tweight, i + 1, j,
				cur_touch_id);
	}
	if (j > 0 && invalid_matrix[i][j-1] != cur_touch_id) {
		if (matrix[i][j-1] >= LARGE_AREA_FRINGE &&
			matrix[i][j-1] < matrix[i][j])
			determine_area_loc_fringe(isum, jsum, tweight, i, j - 1,
				cur_touch_id);
	}
	if (j < Y_AXIS_MINUS1 && invalid_matrix[i][j+1] != cur_touch_id)
	{
		if (matrix[i][j+1] >= LARGE_AREA_FRINGE &&
			matrix[i][j+1] < matrix[i][j])
			determine_area_loc_fringe(isum, jsum, tweight, i, j + 1,
				cur_touch_id);
	}
	if (i > 0 && j > 0 && invalid_matrix[i-1][j-1] != cur_touch_id)
	{
		if (matrix[i-1][j-1] >= LARGE_AREA_FRINGE &&
			matrix[i-1][j-1] < matrix[i][j])
			determine_area_loc_fringe(isum, jsum, tweight, i - 1, j - 1,
				cur_touch_id);
	}
	if (i < X_AXIS_MINUS1 && j > 0 && invalid_matrix[i+1][j-1] != cur_touch_id)
	{
		if (matrix[i+1][j-1] >= LARGE_AREA_FRINGE &&
			matrix[i+1][j-1] < matrix[i][j])
			determine_area_loc_fringe(isum, jsum, tweight, i + 1, j - 1,
				cur_touch_id);
	}
	if (j < Y_AXIS_MINUS1 && i > 0 && invalid_matrix[i-1][j+1] != cur_touch_id)
	{
		if (matrix[i-1][j+1] >= LARGE_AREA_FRINGE &&
			matrix[i-1][j+1] < matrix[i][j])
			determine_area_loc_fringe(isum, jsum, tweight, i - 1, j + 1,
				cur_touch_id);
	}
	if (j < Y_AXIS_MINUS1 && i < X_AXIS_MINUS1 &&
		invalid_matrix[i+1][j+1] != cur_touch_id)
	{
		if (matrix[i+1][j+1] >= LARGE_AREA_FRINGE &&
			matrix[i+1][j+1] < matrix[i][j])
			determine_area_loc_fringe(isum, jsum, tweight, i + 1, j + 1,
				cur_touch_id);
	}
}

void determine_area_loc(float *isum, float *jsum, int *tweight, int i, int j,
	int *mini, int *maxi, int *minj, int *maxj, int cur_touch_id,
	int *highest_val){
	// Invalidate this touch point so that we don't process it later
	invalid_matrix[i][j] = cur_touch_id;

	// Track the size of the touch for TOUCH_MAJOR
	if (i < *mini)
		*mini = i;
	if (i > *maxi)
		*maxi = i;
	if (j < *minj)
		*minj = j;
	if (j > *maxj)
		*maxj = j;

	// Track the highest value of the touch to determine which threshold
	// applies.
	if (matrix[i][j] > *highest_val)
		*highest_val = matrix[i][j];

	// Track touch values to help determine the pixel x, y location
	float powered = pow(matrix[i][j], 1.5);
	*tweight += powered;
	*isum += powered * i;
	*jsum += powered * j;

	// Check nearby points to see if they are above LARGE_AREA_UNPRESS
	// or if they are above LARGE_AREA_FRINGE but the next nearby point is
	// decreasing in value.  If the value is not decreasing and below
	// LARGE_AREA_UNPRESS then we have 2 fingers pinched close together.
	if (i > 0 && invalid_matrix[i-1][j] != cur_touch_id)
	{
		if (matrix[i-1][j] >= LARGE_AREA_UNPRESS)
			determine_area_loc(isum, jsum, tweight, i - 1, j, mini, maxi, minj,
			maxj, cur_touch_id, highest_val);
		else if (matrix[i-1][j] >= LARGE_AREA_FRINGE &&
			matrix[i-1][j] < matrix[i][j])
			determine_area_loc_fringe(isum, jsum, tweight, i - 1, j,
				cur_touch_id);
	}
	if (i < X_AXIS_MINUS1 && invalid_matrix[i+1][j] != cur_touch_id)
	{
		if (matrix[i+1][j] >= LARGE_AREA_UNPRESS)
			determine_area_loc(isum, jsum, tweight, i + 1, j, mini, maxi, minj,
			maxj, cur_touch_id, highest_val);
		else if (matrix[i+1][j] >= LARGE_AREA_FRINGE &&
			matrix[i+1][j] < matrix[i][j])
			determine_area_loc_fringe(isum, jsum, tweight, i + 1, j,
				cur_touch_id);
	}
	if (j > 0 && invalid_matrix[i][j-1] != cur_touch_id)
	{
		if (matrix[i][j-1] >= LARGE_AREA_UNPRESS)
			determine_area_loc(isum, jsum, tweight, i, j - 1, mini, maxi, minj,
				maxj, cur_touch_id, highest_val);
		else if (matrix[i][j-1] >= LARGE_AREA_FRINGE &&
			matrix[i][j-1] < matrix[i][j])
			determine_area_loc_fringe(isum, jsum, tweight, i, j - 1,
				cur_touch_id);
	}
	if (j < Y_AXIS_MINUS1 && invalid_matrix[i][j+1] != cur_touch_id)
	{
		if (matrix[i][j+1] >= LARGE_AREA_UNPRESS)
			determine_area_loc(isum, jsum, tweight, i, j + 1, mini, maxi, minj,
				maxj, cur_touch_id, highest_val);
		else if (matrix[i][j+1] >= LARGE_AREA_FRINGE &&
			matrix[i][j+1] < matrix[i][j])
			determine_area_loc_fringe(isum, jsum, tweight, i, j + 1,
				cur_touch_id);
	}
	if (i > 0 && j > 0 && invalid_matrix[i-1][j-1] != cur_touch_id)
	{
		if (matrix[i-1][j-1] >= LARGE_AREA_UNPRESS)
			determine_area_loc(isum, jsum, tweight, i - 1, j - 1, mini, maxi,
				minj, maxj, cur_touch_id, highest_val);
		else if (matrix[i-1][j-1] >= LARGE_AREA_FRINGE &&
			matrix[i-1][j-1] < matrix[i][j])
			determine_area_loc_fringe(isum, jsum, tweight, i - 1, j - 1,
				cur_touch_id);
	}
	if (i < X_AXIS_MINUS1 && j > 0 && invalid_matrix[i+1][j-1] != cur_touch_id)
	{
		if (matrix[i+1][j-1] >= LARGE_AREA_UNPRESS)
			determine_area_loc(isum, jsum, tweight, i + 1, j - 1, mini, maxi,
				minj, maxj, cur_touch_id, highest_val);
		else if (matrix[i+1][j-1] >= LARGE_AREA_FRINGE &&
			matrix[i+1][j-1] < matrix[i][j])
			determine_area_loc_fringe(isum, jsum, tweight, i + 1, j - 1,
				cur_touch_id);
	}
	if (j < Y_AXIS_MINUS1 && i > 0 && invalid_matrix[i-1][j+1] != cur_touch_id)
	{
		if (matrix[i-1][j+1] >= LARGE_AREA_UNPRESS)
			determine_area_loc(isum, jsum, tweight, i - 1, j + 1, mini, maxi,
				minj, maxj, cur_touch_id, highest_val);
		else if (matrix[i-1][j+1] >= LARGE_AREA_FRINGE &&
			matrix[i-1][j+1] < matrix[i][j])
			determine_area_loc_fringe(isum, jsum, tweight, i - 1, j + 1,
				cur_touch_id);
	}
	if (j < Y_AXIS_MINUS1 && i < X_AXIS_MINUS1 &&
		invalid_matrix[i+1][j+1] != cur_touch_id)
	{
		if (matrix[i+1][j+1] >= LARGE_AREA_UNPRESS)
			determine_area_loc(isum, jsum, tweight, i + 1, j + 1, mini, maxi,
				minj, maxj, cur_touch_id, highest_val);
		else if (matrix[i+1][j+1] >= LARGE_AREA_FRINGE &&
			matrix[i+1][j+1] < matrix[i][j])
			determine_area_loc_fringe(isum, jsum, tweight, i + 1, j + 1,
				cur_touch_id);
	}
}

void process_new_tpoint(struct touchpoint *t, int *tracking_id) {
	// Handles setting up a brand new touch point
	if (t->isValid > TOUCH_DELAY_THRESHOLD) {
		t->tracking_id = *tracking_id;
		*tracking_id += 1;
		if (t->isValid <= TOUCH_INITIAL_THRESHOLD)
			t->touch_delay = TOUCH_DELAY;
	} else {
		t->isValid = 0;
	}
}

int calc_point(void)
{
	int i, j, k;
	int tweight = 0;
	int tpc = 0;
	float isum = 0, jsum = 0;
	float avgi, avgj;
	static int previoustpc, tracking_id = 0;
#if DEBOUNCE_FILTER
	int new_debounce_touch = 0;
	static int initialx, initialy;
#endif

	if (tpoint[0].x < -20) {
		previoustpc = 0;
#if DEBOUNCE_FILTER
		new_debounce_touch = 1;
#endif
	}

	// Record values for processing later
	for(i=0; i < previoustpc; i++) {
		prev2tpoint[i].i = prevtpoint[i].i;
		prev2tpoint[i].j = prevtpoint[i].j;
		prev2tpoint[i].pw = prevtpoint[i].pw;
#if USE_B_PROTOCOL
		prev2tpoint[i].slot = prevtpoint[i].slot;
#endif
		prev2tpoint[i].tracking_id = prevtpoint[i].tracking_id;
		prev2tpoint[i].prev_loc = prevtpoint[i].prev_loc;
#if MAX_DELTA_FILTER
		prev2tpoint[i].direction = prevtpoint[i].direction;
		prev2tpoint[i].distance = prevtpoint[i].distance;
#endif
		prev2tpoint[i].touch_major = prevtpoint[i].touch_major;
		prev2tpoint[i].x = prevtpoint[i].x;
		prev2tpoint[i].y = prevtpoint[i].y;
		prev2tpoint[i].raw_x = prevtpoint[i].raw_x;
		prev2tpoint[i].raw_y = prevtpoint[i].raw_y;
		prev2tpoint[i].isValid = prevtpoint[i].isValid;
		prev2tpoint[i].touch_delay = prevtpoint[i].touch_delay;

		prevtpoint[i].i = tpoint[i].i;
		prevtpoint[i].j = tpoint[i].j;
		prevtpoint[i].pw = tpoint[i].pw;
#if USE_B_PROTOCOL
		prevtpoint[i].slot = tpoint[i].slot;
#endif
		prevtpoint[i].tracking_id = tpoint[i].tracking_id;
		prevtpoint[i].prev_loc = tpoint[i].prev_loc;
#if MAX_DELTA_FILTER
		prevtpoint[i].direction = tpoint[i].direction;
		prevtpoint[i].distance = tpoint[i].distance;
#endif
		prevtpoint[i].touch_major = tpoint[i].touch_major;
		prevtpoint[i].x = tpoint[i].x;
		prevtpoint[i].y = tpoint[i].y;
		prevtpoint[i].raw_x = tpoint[i].raw_x;
		prevtpoint[i].raw_y = tpoint[i].raw_y;
		prevtpoint[i].isValid = tpoint[i].isValid;
		prevtpoint[i].touch_delay = tpoint[i].touch_delay;
	}

	// Generate list of high values
	memset(&invalid_matrix, 0, sizeof(invalid_matrix));
	for(i=0; i < X_AXIS_POINTS; i++) {
		for(j=0; j < Y_AXIS_POINTS; j++) {
#if RAW_DATA_DEBUG
			if (matrix[i][j] < RAW_DATA_THRESHOLD)
				printf("   ");
			else
				printf("%2.2X ", matrix[i][j]);
#endif
			if (tpc < MAX_TOUCH && matrix[i][j] > TOUCH_CONTINUE_THRESHOLD &&
				!invalid_matrix[i][j]) {

				isum = 0;
				jsum = 0;
				tweight = 0;
				int mini = i, maxi = i, minj = j, maxj = j;
				int highest_val = matrix[i][j];
				determine_area_loc(&isum, &jsum, &tweight, i, j, &mini,
					&maxi, &minj, &maxj, tpc + 1, &highest_val);

				avgi = isum / (float)tweight;
				avgj = jsum / (float)tweight;
				maxi = maxi - mini;
				maxj = maxj - minj;

				tpoint[tpc].pw = tweight;
				tpoint[tpc].i = avgi;
				tpoint[tpc].j = avgj;
				tpoint[tpc].touch_major = MAX(maxi, maxj) *
					PIXELS_PER_POINT;
				tpoint[tpc].tracking_id = -1;
#if USE_B_PROTOCOL
				tpoint[tpc].slot = -1;
#endif
				tpoint[tpc].prev_loc = -1;
#if USERSPACE_270_ROTATE
				tpoint[tpc].x = tpoint[tpc].i * X_LOCATION_VALUE;
				tpoint[tpc].y = Y_RESOLUTION_MINUS1 - tpoint[tpc].j *
					Y_LOCATION_VALUE;
#else
				tpoint[tpc].x = X_RESOLUTION_MINUS1 - tpoint[tpc].j *
					X_LOCATION_VALUE;
				tpoint[tpc].y = Y_RESOLUTION_MINUS1 - tpoint[tpc].i *
					Y_LOCATION_VALUE;
#endif // USERSPACE_270_ROTATE
				// It is possible for x and y to be negative with the math
				// above so we force them to 0 if they are negative.
				if (tpoint[tpc].x < 0)
					tpoint[tpc].x = 0;
				if (tpoint[tpc].y < 0)
					tpoint[tpc].y = 0;
				tpoint[tpc].raw_x = tpoint[tpc].x;
				tpoint[tpc].raw_y = tpoint[tpc].y;
				tpoint[tpc].isValid = highest_val;
				tpoint[tpc].touch_delay = 0;
				tpc++;
			}
		}
#if RAW_DATA_DEBUG
		printf(" |\n"); // end of row
#endif
	}
#if RAW_DATA_DEBUG
	printf("end of raw data\n"); // helps separate one frame from the next
#endif

#if USE_B_PROTOCOL
	// Set all previously used slots to -1 so we know if we need to lift any
	// of them off after matching
	for (i=0; i<MAX_TOUCH; i++)
		if(slot_in_use[i])
			slot_in_use[i] = -1;
#endif

	// Match up tracking IDs
	{
		int smallest_distance[MAX_TOUCH], cur_distance;
		int deltax, deltay;
		int smallest_distance_loc[MAX_TOUCH];
		// Find closest points for each touch
		for (i=0; i<tpc; i++) {
			smallest_distance[i] = 1000000;
			smallest_distance_loc[i] = -1;
			for (j=0; j<previoustpc; j++) {
				if (prevtpoint[j].isValid) {
					deltax = tpoint[i].raw_x - prevtpoint[j].raw_x;
					deltay = tpoint[i].raw_y - prevtpoint[j].raw_y;
					cur_distance = (deltax * deltax) + (deltay * deltay);
					if(cur_distance < smallest_distance[i]) {
						smallest_distance[i] = cur_distance;
						smallest_distance_loc[i] = j;
					}
				}
			}
		}

		// Remove mapping for touches which aren't closest
		for (i=0; i<tpc; i++) {
			for (j=i + 1; j<tpc; j++) {
				if (smallest_distance_loc[i] > -1 &&
				   smallest_distance_loc[i] == smallest_distance_loc[j]) {
					if (smallest_distance[i] < smallest_distance[j])
						smallest_distance_loc[j] = -1;
					else
						smallest_distance_loc[i] = -1;
				}
			}
		}

		// Assign ids to closest touches
		for (i=0; i<tpc; i++) {
			if (smallest_distance_loc[i] > -1) {
#if MAX_DELTA_FILTER
				// Filter for impossibly large changes in touches
				if (smallest_distance[i] > MAX_DELTA_SQ) {
					int need_lift = 1;
					// Check to see if the previous point was moving quickly
					if (prevtpoint[smallest_distance_loc[i]].distance >
						MIN_PREV_DELTA_SQ) {
						// Check the direction of the previous point and see
						// if we're continuing in roughly the same direction.
						tpoint[i].direction = atan2(
						tpoint[i].x - prevtpoint[smallest_distance_loc[i]].x,
						tpoint[i].y - prevtpoint[smallest_distance_loc[i]].y);
						if (fabsf(tpoint[i].direction -
							prevtpoint[smallest_distance_loc[i]].direction) <
							MAX_DELTA_ANGLE) {
#if MAX_DELTA_DEBUG
							printf("direction is close enough, no liftoff\n");
#endif
							// No need to lift off
							need_lift = 0;
						}
#if MAX_DELTA_DEBUG
						else
							printf("angle change too great, going to lift\n");
#endif
					}
#if MAX_DELTA_DEBUG
					else
						printf("previous distance too low, going to lift\n");
#endif
					if (need_lift) {
						//  This is an impossibly large change in touches
#if TRACK_ID_DEBUG
						printf("Over Delta %d - %d,%d - %d,%d -> %d,%d\n",
							prevtpoint[smallest_distance_loc[i]].tracking_id,
							smallest_distance_loc[i], i, tpoint[i].x,
							tpoint[i].y,
							prevtpoint[smallest_distance_loc[i]].x,
							prevtpoint[smallest_distance_loc[i]].y);
#endif
#if USE_B_PROTOCOL
#if EVENT_DEBUG || MAX_DELTA_DEBUG
						printf("sending max delta liftoff for slot: %i\n",
							prevtpoint[smallest_distance_loc[i]].slot);
#endif // EVENT_DEBUG || MAX_DELTA_DEBUG
						liftoff_slot(prevtpoint[smallest_distance_loc[i]].slot);
#endif // USE_B_PROTOCOL
						process_new_tpoint(&tpoint[i], &tracking_id);
					}
				} else
#endif // MAX_DELTA_FILTER
				{
#if TRACK_ID_DEBUG
					printf("Continue Map %d - %d,%d - %lf,%lf -> %lf,%lf\n",
						prevtpoint[smallest_distance_loc[i]].tracking_id,
						smallest_distance_loc[i], i, tpoint[i].i, tpoint[i].j,
						prevtpoint[smallest_distance_loc[i]].i,
						prevtpoint[smallest_distance_loc[i]].j);
#endif
					tpoint[i].tracking_id =
						prevtpoint[smallest_distance_loc[i]].tracking_id;
					tpoint[i].prev_loc = smallest_distance_loc[i];
					tpoint[i].touch_delay =
						prevtpoint[smallest_distance_loc[i]].touch_delay;
#if MAX_DELTA_FILTER
					// Track distance and angle
					tpoint[i].distance = smallest_distance[i];
					tpoint[i].direction = atan2(
						tpoint[i].x - prevtpoint[smallest_distance_loc[i]].x,
						tpoint[i].y - prevtpoint[smallest_distance_loc[i]].y);
#endif
#if AVG_FILTER
					avg_filter(&tpoint[i]);
#endif // AVG_FILTER
				}
#if USE_B_PROTOCOL
				tpoint[i].slot = prevtpoint[smallest_distance_loc[i]].slot;
				slot_in_use[prevtpoint[smallest_distance_loc[i]].slot] = 1;
#endif
			} else {
				process_new_tpoint(&tpoint[i], &tracking_id);
#if TRACK_ID_DEBUG
				printf("New Mapping - %lf,%lf - tracking ID: %i\n",
					tpoint[i].i, tpoint[i].j, tpoint[i].tracking_id);
#endif
			}
		}
	}

#if USE_B_PROTOCOL
	// Assign unused slots to touches that don't have a slot yet
	for (i=0; i<tpc; i++) {
		if (tpoint[i].slot < 0 && tpoint[i].isValid && !tpoint[i].touch_delay) {
			for (j=0; j<MAX_TOUCH; j++) {
				if (slot_in_use[j] <= 0) {
					if (slot_in_use[j] == -1) {
#if EVENT_DEBUG
						printf("lifting unused slot %i & reassigning it\n", j);
#endif
						liftoff_slot(j);
					}
					tpoint[i].slot = j;
					slot_in_use[j] = 1;
#if TRACK_ID_DEBUG
					printf("new slot [%i] trackID: %i slot: %i | %lf , %lf\n",
						i, tpoint[i].tracking_id, tpoint[i].slot, tpoint[i].i,
						tpoint[i].j);
#endif
					j = MAX_TOUCH;
				}
			}
		}
	}

	// Lift off any previously used slots that haven't been reassigned
	for (i=0; i<MAX_TOUCH; i++) {
		if (slot_in_use[i] == -1) {
#if EVENT_DEBUG
			printf("lifting off slot %i - no longer in use\n", i);
#endif
			liftoff_slot(i);
			slot_in_use[i] = 0;
		}
	}
#endif // USE_B_PROTOCOL

#if DEBOUNCE_FILTER
	// The debounce filter only works on a single touch.
	// We record the initial touchdown point, calculate a radius in
	// pixels and re-center the point if we're still within the
	// radius.  Once we leave the radius, we invalidate so that we
	// don't debounce again even if we come back to the radius.
	if (tpc == 1) {
		if (new_debounce_touch) {
			// We record the initial location of a new touch
			initialx = tpoint[0].x;
			initialy = tpoint[0].y;
#if DEBOUNCE_DEBUG
			printf("new touch recorded at %i, %i\n", initialx, initialy);
#endif
		} else if (initialx > -20) {
			// See if the current touch is still inside the debounce
			// radius
			if (abs(initialx - tpoint[0].x) <= DEBOUNCE_RADIUS
				&& abs(initialy - tpoint[0].y) <= DEBOUNCE_RADIUS) {
				// Set the point to the original point - debounce!
				tpoint[0].x = initialx;
				tpoint[0].y = initialy;
#if DEBOUNCE_DEBUG
				printf("debouncing!!!\n");
#endif
			} else {
				initialx = -100; // Invalidate
#if DEBOUNCE_DEBUG
				printf("done debouncing\n");
#endif
			}
		}
	}
#endif

	// Report touches
	for (k = 0; k < tpc; k++) {
		if (tpoint[k].isValid && !tpoint[k].touch_delay) {
#if EVENT_DEBUG
			printf("send event for tracking ID: %i\n", tpoint[k].tracking_id);
#endif
#if USE_B_PROTOCOL
			send_uevent(uinput_fd, EV_ABS, ABS_MT_SLOT, tpoint[k].slot);
#endif
			send_uevent(uinput_fd, EV_ABS, ABS_MT_TRACKING_ID,
				tpoint[k].tracking_id);
			send_uevent(uinput_fd, EV_ABS, ABS_MT_TOUCH_MAJOR,
				tpoint[k].touch_major);
			send_uevent(uinput_fd, EV_ABS, ABS_MT_POSITION_X, tpoint[k].x);
			send_uevent(uinput_fd, EV_ABS, ABS_MT_POSITION_Y, tpoint[k].y);
#if !USE_B_PROTOCOL
			send_uevent(uinput_fd, EV_SYN, SYN_MT_REPORT, 0);
#endif
		} else if (tpoint[k].touch_delay) {
			// This touch didn't meet the threshold so we don't report it yet
			tpoint[k].touch_delay--;
		}
	}
	if (tpc > 0) {
		send_uevent(uinput_fd, EV_SYN, SYN_REPORT, 0);
	}
	previoustpc = tpc; // Store the touch count for the next run
	if (tracking_id >  2147483000)
		tracking_id = 0; // Reset tracking ID counter if it gets too big
	return tpc; // Return the touch count
}


int cline_valid(unsigned int extras)
{
	if (cline[0] == 0xff && cline[1] == 0x43 && cidx == 44-extras) {
		return 1;
	}
	if (cline[0] == 0xff && cline[1] == 0x47 && cidx > 4 &&
		cidx == (cline[2]+4-extras)) {
		return 1;
	}
	return 0;
}

void put_byte(unsigned char byte)
{
	if(cidx==0 && byte != 0xFF)
		return;

	// Sometimes a send is aborted by the touch screen. all we get is an out of
	// place 0xFF
	if(byte == 0xFF && !cline_valid(1))
		cidx = 0;

	cline[cidx++] = byte;
}

int consume_line(void)
{
	int i,j,ret=0;

	if(cline[1] == 0x47) {
		// Calculate the data points. all transfers complete
		ret = calc_point();
	}

	if(cline[1] == 0x43) {
		// This is a start event. clear the matrix
		if(cline[2] & 0x80) {
			for(i=0; i < X_AXIS_POINTS; i++)
				for(j=0; j < Y_AXIS_POINTS; j++)
					matrix[i][j] = 0;
		}

		// Write the line into the matrix
		for(i=0; i < Y_AXIS_POINTS; i++)
			matrix[cline[2] & 0x1F][i] = cline[i+3];
	}

	cidx = 0;

	return ret;
}

int snarf2(unsigned char* bytes, int size)
{
	int i,ret=0;

	for(i=0; i < size; i++) {
		put_byte(bytes[i]);
		if(cline_valid(0))
			ret += consume_line();
	}

	return ret;
}

void open_uinput(void)
{
	struct uinput_user_dev device;

	memset(&device, 0, sizeof device);

	uinput_fd=open(UINPUT_LOCATION,O_WRONLY);
	strcpy(device.name,"HPTouchpad");

	device.id.bustype=BUS_VIRTUAL;
	device.id.vendor = 1;
	device.id.product = 1;
	device.id.version = 1;

	device.absmax[ABS_MT_POSITION_X] = X_RESOLUTION;
	device.absmax[ABS_MT_POSITION_Y] = Y_RESOLUTION;
	device.absmin[ABS_MT_POSITION_X] = 0;
	device.absmin[ABS_MT_POSITION_Y] = 0;
	device.absfuzz[ABS_MT_POSITION_X] = 2;
	device.absflat[ABS_MT_POSITION_X] = 0;
	device.absfuzz[ABS_MT_POSITION_Y] = 1;
	device.absflat[ABS_MT_POSITION_Y] = 0;

	if (write(uinput_fd,&device,sizeof(device)) != sizeof(device))
		fprintf(stderr, "error setup\n");

	if (ioctl(uinput_fd,UI_SET_EVBIT, EV_SYN) < 0)
		fprintf(stderr, "error evbit key\n");

	if (ioctl(uinput_fd,UI_SET_EVBIT,EV_ABS) < 0)
		fprintf(stderr, "error evbit rel\n");

#if USE_B_PROTOCOL
	if (ioctl(uinput_fd,UI_SET_ABSBIT,ABS_MT_SLOT) < 0)
		fprintf(stderr, "error slot rel\n");
#endif

	if (ioctl(uinput_fd,UI_SET_ABSBIT,ABS_MT_TRACKING_ID) < 0)
		fprintf(stderr, "error trkid rel\n");

	if (ioctl(uinput_fd,UI_SET_ABSBIT,ABS_MT_TOUCH_MAJOR) < 0)
		fprintf(stderr, "error tool rel\n");

	//if (ioctl(uinput_fd,UI_SET_ABSBIT,ABS_MT_WIDTH_MAJOR) < 0)
	//	fprintf(stderr, "error tool rel\n");

	if (ioctl(uinput_fd,UI_SET_ABSBIT,ABS_MT_POSITION_X) < 0)
		fprintf(stderr, "error tool rel\n");

	if (ioctl(uinput_fd,UI_SET_ABSBIT,ABS_MT_POSITION_Y) < 0)
		fprintf(stderr, "error tool rel\n");

	if (ioctl(uinput_fd,UI_DEV_CREATE) < 0)
		fprintf(stderr, "error create\n");
}

void clear_arrays(void)
{
	// Clears arrays (for after a total liftoff occurs)
	int i;
	for(i=0; i<MAX_TOUCH; i++) {
		tpoint[i].pw = -1000;
		tpoint[i].i = -1000;
		tpoint[i].j = -1000;
#if USE_B_PROTOCOL
		tpoint[i].slot = -1;
#endif
		tpoint[i].tracking_id = -1;
		tpoint[i].prev_loc = -1;
#if MAX_DELTA_FILTER
		tpoint[i].direction = 0;
		tpoint[i].distance = 0;
#endif
		tpoint[i].touch_major = 0;
		tpoint[i].x = -1000;
		tpoint[i].y = -1000;

		prevtpoint[i].pw = -1000;
		prevtpoint[i].i = -1000;
		prevtpoint[i].j = -1000;
#if USE_B_PROTOCOL
		prevtpoint[i].slot = -1;
#endif
		prevtpoint[i].tracking_id = -1;
		prevtpoint[i].prev_loc = -1;
#if MAX_DELTA_FILTER
		prevtpoint[i].direction = 0;
		prevtpoint[i].distance = 0;
#endif
		prevtpoint[i].touch_major = 0;
		prevtpoint[i].x = -1000;
		prevtpoint[i].y = -1000;

		prev2tpoint[i].pw = -1000;
		prev2tpoint[i].i = -1000;
		prev2tpoint[i].j = -1000;
#if USE_B_PROTOCOL
		prev2tpoint[i].slot = -1;
#endif
		prev2tpoint[i].tracking_id = -1;
		prev2tpoint[i].prev_loc = -1;
#if MAX_DELTA_FILTER
		prev2tpoint[i].direction = 0;
		prev2tpoint[i].distance = 0;
#endif
		prev2tpoint[i].touch_major = 0;
		prev2tpoint[i].x = -1000;
		prev2tpoint[i].y = -1000;
	}
}

int main(int argc, char** argv)
{
	struct hsuart_mode uart_mode;
	int uart_fd, nbytes, need_liftoff = 0;
	unsigned char recv_buf[RECV_BUF_SIZE];
	fd_set fdset;
	struct timeval seltmout;
	/* linux maximum priority is 99, nonportable */
	struct sched_param sparam = { .sched_priority = 99 };

	/* We set ts server priority to RT so that there is no delay in
	 * in obtaining input and we are NEVER bumped from CPU until we
	 * give it up ourselves. */
	if (sched_setscheduler(0 /* that's us */, SCHED_FIFO, &sparam))
		perror("Cannot set RT priority, ignoring: ");

	uart_fd = open("/dev/ctp_uart", O_RDONLY|O_NONBLOCK);
	if(uart_fd<=0) {
		printf("Could not open uart\n");
		return 0;
	}

	open_uinput();

	ioctl(uart_fd,HSUART_IOCTL_GET_UARTMODE,&uart_mode);
	uart_mode.speed = 0x3D0900;
	ioctl(uart_fd, HSUART_IOCTL_SET_UARTMODE,&uart_mode);

	ioctl(uart_fd, HSUART_IOCTL_FLUSH, 0x9);

	// Lift off in case of driver crash or in case the driver was shut off to
	// save power by closing the uart.
	liftoff();
	clear_arrays();

	while(1) {
		FD_ZERO(&fdset);
		FD_SET(uart_fd, &fdset);
		seltmout.tv_sec = 0;
		/* 2x tmout */
		seltmout.tv_usec = LIFTOFF_TIMEOUT;

		if (0 == select(uart_fd + 1, &fdset, NULL, NULL, &seltmout)) {
			/* Timeout means liftoff, send event */
#if DEBUG
			printf("timeout! sending liftoff\n");
#endif

			if (need_liftoff) {
#if EVENT_DEBUG
				printf("timeout called liftoff\n");
#endif
				liftoff();
				clear_arrays();
				need_liftoff = 0;
			}

			FD_ZERO(&fdset);
			FD_SET(uart_fd, &fdset);
			/* Now enter indefinite sleep iuntil input appears */
			select(uart_fd + 1, &fdset, NULL, NULL, NULL);
			/* In case we were wrongly woken up check the event
			 * count again */
			continue;
		}
			
		nbytes = read(uart_fd, recv_buf, RECV_BUF_SIZE);
		
		if(nbytes <= 0)
			continue;
#if DEBUG
		printf("Received %d bytes\n", nbytes);
		int i;
		for(i=0; i < nbytes; i++)
			printf("%2.2X ",recv_buf[i]);
		printf("\n");
#endif
		if (!snarf2(recv_buf,nbytes)) {
			// Sometimes there is data, but no valid touches due to threshold
			if (need_liftoff) {
#if EVENT_DEBUG
				printf("snarf2 called liftoff\n");
#endif
				liftoff();
				clear_arrays();
				need_liftoff = 0;
			}
		} else
			need_liftoff = 1;
	}

	return 0;
}