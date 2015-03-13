/* Copying and distribution of this file, with or without modification,
 * are permitted in any medium without royalty. This file is offered as-is,
 * without any warranty.
 */

/*! @file process_frame.c
 * @brief Contains the actual algorithm and calculations.
 */

/* Definitions specific to this application. Also includes the Oscar main header file. */
#include "template.h"
#include <string.h>
#include <stdlib.h>

/*! @brief this contains a color value.
 * the ordering corresponds to the ordering of the color planes in the images
 * */
typedef struct {
        uint8 blue, green, red;
} s_color;

//local function definitions
void ChangeDetection();
void DetectRegions();
void DrawBoundingBox(struct OSC_PICTURE *picIn, struct OSC_VIS_REGIONS *regions, s_color color);
void toggle();
void MaxArea(struct OSC_VIS_REGIONS *regions);
void GetAvarageColor();

//width of SENSORIMG (the original camera image is reduced by a factor of 2)
const int nc = OSC_CAM_MAX_IMAGE_WIDTH/2;
//size of SENSORIMG (the original camera image is reduced by a factor of 2)
const int nr = OSC_CAM_MAX_IMAGE_HEIGHT/2;
//total number of pixel of images (the number of bytes is 3 times larger due to the
//three color planes)
const int siz = (OSC_CAM_MAX_IMAGE_WIDTH/2)*(OSC_CAM_MAX_IMAGE_HEIGHT/2);
//we require these structures to use Oscar functions
//c.f.-> file:///home/oscar/leanXcam/oscar/documentation/html/structOSC__PICTURE.html
struct OSC_PICTURE Pic1, Pic2;
//c.f. -> file:///home/oscar/leanXcam/oscar/documentation/html/structOSC__VIS__REGIONS.html
struct OSC_VIS_REGIONS ImgRegions;//these contain the foreground objects
//keeps track of digital output status
int outputIO;



/*********************************************************************//*!
 * @brief this function is only executed at start up
 * put all initialization stuff in here
 *
 *//*********************************************************************/
void InitProcess() {
	//set polarity of digital output (required?)
	OscGpioSetupPolarity(GPIO_OUT1, FALSE);
	OscGpioSetupPolarity(GPIO_OUT2, FALSE);
	//set one digital output to high the other to low
	OscGpioWrite(GPIO_OUT1, TRUE);
	OscGpioWrite(GPIO_OUT2, FALSE);
	//set initial status of IO
	outputIO = 1;
}


/*********************************************************************//*!
 * @brief this function is executed for each image processing step
 * the camera image is in the image buffer: data.u8TempImage[SENSORIMG]
 * it has nr = 240 number of rows and nc = 376 number of columns and
 * each pixel is represented by three bytes corresponding to the color
 * planes blue, green and red (in this ordering)
 * the buffer data.u8TempImage[] contains three more images
 * (c.f. template.h line 57ff); two of them, referenced by the ENUM values
 * BACKGROUND and THRESHOLD are - in addition to the image SENSORIMG -
 * displayed on the web interface
 *//*********************************************************************/
void ProcessFrame() {
	//this color is used for drawing the rectangles in the image
	s_color color = {255, 0, 0};

	//step counter, is increased after each step
	if(data.ipc.state.nStepCounter == 1) {
		//this is the first time we have valid image data
		//here we put routines that require image data and are only executed once at the beginning

		//set frame-buffer THRESHOLD to zero
		memset(data.u8TempImage[THRESHOLD], 0, sizeof(data.u8TempImage[THRESHOLD]));

		//save current image frame in BACKGROUND
		memcpy(data.u8TempImage[BACKGROUND], data.u8TempImage[SENSORIMG], sizeof(data.u8TempImage[BACKGROUND]));
	} else {
		//this is done for all following processing steps

		//uncomment the following line to see an example for log-output on the console (for further info c.f. chapter 8.3. of leanXcam user doc)
		//OscLog(INFO, "%s: currently running ProcessFrame for step counter %d\n", __func__, data.ipc.state.nStepCounter);

		//call function change detection
		ChangeDetection();
		GetAvarageColor();
		//call function for region detection
		DetectRegions();

		//save current image frame in BACKGROUND (before we draw the rectangles)
		memcpy(data.u8TempImage[BACKGROUND], data.u8TempImage[SENSORIMG], sizeof(data.u8TempImage[BACKGROUND]));

		//draw regions directly to the image (the image content is changed!)
		DrawBoundingBox(&Pic2, &ImgRegions, color);

		MaxArea(&ImgRegions);

		//every ten image steps we toggle the digital outputs
		if(!(data.ipc.state.nStepCounter%50)) {
			toggle(&ImgRegions);
		}
	}
}


/*********************************************************************//*!
 * @brief calculate the difference of the current image (SENSORIMG) and
 * the last image (BACKGROUND) and compare with threshold value
 * if difference is large set THRESHOLD image to 255 (only blue - plane)
 * in addition a binary image is written to PROCESSFRAME0
 *//*********************************************************************/
void ChangeDetection() {
	int row, col, cpl;
	//loop over the rows
	for(row = 0; row < siz; row += nc) {
		//loop over the columns
		for(col = 0; col < nc; col++) {
			int16 Dif = 0;
			//loop over the color planes (blue - green - red) and sum up the difference
			for(cpl = 0; cpl < NUM_COLORS; cpl++) {
				Dif += abs((int16) data.u8TempImage[SENSORIMG][(row+col)*NUM_COLORS+cpl]-
												(int16) data.u8TempImage[BACKGROUND][(row+col)*NUM_COLORS+cpl]);
			}
			//if the difference is larger than threshold value (can be changed on web interface)
			if(Dif > NUM_COLORS*data.ipc.state.nThreshold) {
				//set pixel value to 1 in PROCESSFRAME0 image (we use only the first third of the image buffer)
				data.u8TempImage[PROCESSFRAME0][(row+col)] = 1;
				//set pixel value to 255 in THRESHOLD image (only the blue plane)
				data.u8TempImage[THRESHOLD][(row+col)*NUM_COLORS] = 255;
			} else {
				//set values to zero
				data.u8TempImage[PROCESSFRAME0][(row+col)] = 0;
				data.u8TempImage[THRESHOLD][(row+col)*NUM_COLORS] = 0;
			}
		}
	}
}



/*********************************************************************//*!
 * @brief do a region labeling and property extraction using directly
 * the functions of the OSCar framework; therefore the images have to
 * be wrapped to the OSC_PICTURE structure
 * results are easily accessible through the structure OSC_VIS_REGIONS
 *//*********************************************************************/
void DetectRegions() {
	//wrap image PROCESSFRAME0 in picture struct
	//because the image MUST be binary (i.e. values of 0 and 1)
	//we use the extra frame PROCESSFRAME0;
	Pic1.data = data.u8TempImage[PROCESSFRAME0];
	Pic1.width = nc;
	Pic1.height = nr;
	Pic1.type = OSC_PICTURE_BINARY;

	//now do region labeling and feature extraction
	OscVisLabelBinary( &Pic1, &ImgRegions);
	OscVisGetRegionProperties( &ImgRegions);

	//also wrap SENSORIMG to an OSC_VIS_PICTURE structure
	//because we use it in DrawBoundingBoxColor()
	Pic2.data = data.u8TempImage[SENSORIMG];
	Pic2.width = nc;
	Pic2.height = nr;
	Pic2.type = OSC_PICTURE_BGR_24;
}


/*********************************************************************//*!
 * @brief draw a bounding box around all regions found in the given
 * OSC_VIS_REGION structure with the given color
 *
 *//*********************************************************************/
void DrawBoundingBox(struct OSC_PICTURE *picIn, struct OSC_VIS_REGIONS *regions, s_color color) {
        uint16 i, o, cpl;
        uint8 *pImg = (uint8*)picIn->data;
        const uint16 width = picIn->width;
        uint8 col[3] = {color.blue, color.green, color. red};
        for(o = 0; o < regions->noOfObjects; o++) {
                /* Draw the horizontal lines. */
                for (i = regions->objects[o].bboxLeft; i < regions->objects[o].bboxRight; i += 1) {
                	for(cpl = 0; cpl < NUM_COLORS; cpl++) {
                        pImg[(width * regions->objects[o].bboxTop + i) * NUM_COLORS + cpl] = col[cpl];
                        pImg[(width * (regions->objects[o].bboxBottom - 1) + i) * NUM_COLORS + cpl] = col[cpl];
                	}
                }

                /* Draw the vertical lines. */
                for (i = regions->objects[o].bboxTop; i < regions->objects[o].bboxBottom-1; i += 1) {
                	for(cpl = 0; cpl < NUM_COLORS; cpl++) {
                        pImg[(width * i + regions->objects[o].bboxLeft) * NUM_COLORS + cpl] = col[cpl];
                        pImg[(width * i + regions->objects[o].bboxRight) * NUM_COLORS + cpl] = col[cpl];
                	}
                }
        }
}


/*********************************************************************//*!
 * @brief Toggle digital output status
 *
 *//*********************************************************************/
void toggle(struct OSC_VIS_REGIONS *regions)
{
    OSC_ERR err = SUCCESS;
    if(outputIO == 1){
	  err = OscGpioWrite(GPIO_OUT1, TRUE);
	  err = OscGpioWrite(GPIO_OUT2, TRUE);
	  outputIO = 0;
    }else{
	  err = OscGpioWrite(GPIO_OUT1, FALSE);
	  err = OscGpioWrite(GPIO_OUT2, FALSE);
	  outputIO = 1;
    }
	if (err != SUCCESS) {
	  fprintf(stderr, "%s: ERROR: GPIO write error! (%d)\n", __func__, err);
	}

	return;
}

void MaxArea(struct OSC_VIS_REGIONS *regions){
		int i = 0;
		int temp = 0;
		for (i = 0; i < regions->noOfObjects; i++)
		{
			if (regions->objects[i].area >= temp)
			{
				temp = regions->objects[i].area;
			}
		}
		printf("Biggest Area: %d\n", temp);
}

void GetAvarageColor()
{
 	int colorcounter[2];
	int row, col, cpl, coln, stp;
	//loop over the rows
	for(row = 0; row < siz; row += nc) {
		//loop over the columns
		for(col = 0; col < nc; col++) {

			int16 Dif = 0;
			//loop over the color planes (blue - green - red) and sum up the difference

			for(cpl = 0; cpl < NUM_COLORS; cpl++) {
				Dif += abs((int16) data.u8TempImage[SENSORIMG][(row+col)*NUM_COLORS+cpl]-
												(int16) data.u8TempImage[BACKGROUND][(row+col)*NUM_COLORS+cpl]);
				//if the difference is larger than threshold value (can be changed on web interface)
				if(Dif > NUM_COLORS*data.ipc.state.nThreshold) {

					}
			}



		}
	}
	int coloravarage[2];
	for(coln = 0; coln < NUM_COLORS; coln++){
		coloravarage[coln] = (colorcounter[coln]/stp)*3;
		printf("%d ", coloravarage[coln]); //Ausgabe in Konsole
}
}

