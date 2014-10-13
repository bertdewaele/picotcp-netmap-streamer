#include <videostream.h>

#include <stdio.h>

#include <cv.h>
#include <highgui.h>

static char* window_name = "Video Streamer";
static CvCapture* capture = 0;

IplImage *img_gray = NULL;
IplImage *img_resize = NULL; 

int
setup_capture(const int device, const double scale)
{
	capture = cvCaptureFromCAM(device);
	if (!capture) {
		printf("Camera device (%i) not detected.\n", device);
		return -1;
	}

	IplImage* image = cvQueryFrame(capture);
	
	img_gray = cvCreateImage(cvGetSize(image), IPL_DEPTH_8U, 1);
	
	CvSize size = {.width =  scale * image->width, .height = scale * image->height};
	img_resize = cvCreateImage(size, IPL_DEPTH_8U, 1);
	
	return 0;
}

IplImage* grab_image(const double scale, const int convert_grayscale)
{
	IplImage* image = cvQueryFrame(capture);

	if (!image) {
		printf("Capture of image failed.\n");
		return NULL;
	} else {
		printf("Image captured.\n");
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
