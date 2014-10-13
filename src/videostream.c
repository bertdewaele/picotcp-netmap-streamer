#include <videostream.h>

#include <stdio.h>

#include <cv.h>
#include <highgui.h>

static char* window_name = "Video Streamer";
static CvCapture* capture = 0;

int
setup_capture(const int device)
{
	capture = cvCaptureFromCAM(device);
	if (!capture) {
		printf("Camera device (%i) not detected.\n", device);
		return -1;
	}
	
	return 0;
}

IplImage* grab_image(const int scale, const int convert_grayscale)
{
	IplImage* image = cvQueryFrame(capture);

	if (!image) {
		printf("Capture of image failed.\n");
		return NULL;
	} else {
		printf("Image captured.\n");
	}

	if (scale != 1) {
		CvSize size = {.width =  scale * image->width, .height = scale * image->height};
		IplImage *img_resize = cvCreateImage(size, IPL_DEPTH_8U, 1);
		cvResize(image, img_resize, CV_INTER_LINEAR); 
		image = img_resize;
	}
	
	if (convert_grayscale) {
		IplImage *img_gray = cvCreateImage(cvGetSize(image), IPL_DEPTH_8U, 1);
		cvCvtColor(image, img_gray, CV_RGB2GRAY);
		return img_gray;
	}

	return image;
}

unsigned char*
grab_raw_data(const int scale, const int convert_grayscale, int* imagesize)
{
	IplImage *image = grab_image(scale, convert_grayscale);

	if(!image) {
		printf("Grab image failed.\n");
		return NULL;
	}
	
	*imagesize = image->imageSize;
	
	unsigned char* rawdata = malloc(image->imageSize*sizeof(unsigned char));
	printf("Image size: %i\n", image->imageSize);
	cvGetRawData(image, &rawdata, NULL, NULL);

	return rawdata;
}

int
clean_up_stream(void)
{
	cvReleaseCapture(&capture);
	cvDestroyWindow(window_name);

	return 0;
}
