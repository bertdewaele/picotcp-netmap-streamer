#ifndef _VIDEOSTREAM_H_
#define _VIDEOSTREAM_H_

#include <cv.h>
#include <highgui.h>

int setup_capture(const int device);

IplImage* grab_image(const double scale, const int convert_grayscale);

unsigned char* grab_raw_data(const double scale, const int convert_grayscale, int* imagesize);

int clean_up_stream(void);

#endif /* _VIDEOSTREAM_H */
