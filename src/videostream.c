#include <videostream.h>

#include <stdio.h>

#include <cv.h>
#include <highgui.h>

static char* window_name = "Video Streamer";
static CvCapture* capture = 0;

unsigned char* raw_data= NULL;
IplImage *img_gray = NULL;
IplImage *img_resize = NULL; 

int
setup_capture(const int device, const double scale, const int gray_scale)
{
	capture = cvCaptureFromCAM(device);
	if (!capture) {
		printf("Camera device (%i) not detected.\n", device);
		return -1;
	}

	IplImage* image = cvQueryFrame(capture);

	if(!image){
		printf("image not retrieved.\n");
		return -1;
	}
	printf("img WIDTH: %i\n", image->width);
	printf("img HEIGHT: %i\n", image->height);
	printf("img nchannels: %i\n", image->nChannels);
	printf("img depth: %i\n", image->depth);
	


	CvSize size = {.width =  scale * image->width, .height = scale * image->height};


	if (gray_scale)
	{
		img_gray = cvCreateImage(cvGetSize(image), IPL_DEPTH_8U, 1);
		img_resize = cvCreateImage(size, IPL_DEPTH_8U, 1);

	} else {
		img_resize = cvCreateImage(size, IPL_DEPTH_8U, 3);

	}
	
	
	raw_data = malloc(image->imageSize*sizeof(unsigned char));

	return 0;
}

IplImage* grab_image(const double scale, const int convert_grayscale)
{
	IplImage* image = cvQueryFrame(capture);

	if (!image) {
		printf("Capture of image failed.\n");
		return NULL;
	}

	if (convert_grayscale) {
		cvCvtColor(image, img_gray, CV_RGB2GRAY);
		image = img_gray;
	}
	
	if (scale != 1) {
		cvResize(image, img_resize, CV_INTER_LINEAR); 
		image = img_resize;
	}

	return image;
}

unsigned char*
grab_raw_data(const double scale, const int convert_grayscale, int* imagesize)
{
	IplImage *image = grab_image(scale, convert_grayscale);

	if(!image) {
		printf("Grab image failed.\n");
		return NULL;
	}
	
	*imagesize = image->imageSize;
	
	cvGetRawData(image, &raw_data, NULL, NULL);

	return raw_data;
}

int
clean_up_stream(void)
{
	cvReleaseCapture(&capture);
	cvDestroyWindow(window_name);

	return 0;
}
