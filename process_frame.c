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

//Zeitstempel bei erstmaliger Detektion von Region >1500
long framestep = 0;
//Aufaddierung der Farbwerte über alle analysierten Pixel {b,g,r}
long colorcounter[3] = {0,0,0};
//Colorcounter durch # Pixel
int coloravarage[3] = {0,0,0};
//Zählvariable für # analysierter Pixel bzw. zuerst # Farbwerte
long stp = 0;
//Groesste erkannte Region
int BiggestArea = 0;
//Zweitgroesste erkannte Region
int SecondBiggestArea = 0;
//Aufaddierung der Groessenwerte der SecondBiggestArea über die analysierten Frames (timetodetect)
int SecondBiggestAreaCounter;
//SecondBiggestAreaCounter durch # analysierte Frames (=timetodetect)
int SecondBigAvarage = 0;
//Aufaddierung der Groessenwerte der BiggestRegion über die analysierten Frames (timetodetect)
int BiggestAreaCounter;
//BiggestAreaCounter durch # analysierte Frames (=timetodetect)
int BiggestAreaAvarage = 0;
//Eindeutige Zuweisungsnummer für grösste Area während genau einem Frame
int RegionNumber = 0;
//Differenz zw. aktuellem Frame und dem gesetzten Stempel framestep
int framediff = 0;
//Definieren von # Positionen der Warteschlange vor Weiche
#define sizetimebuffer 10
//Warteschlange vor Weiche, welche bei jedem frame überprüft bzw. abgearbeitet wird
int timestamp[sizetimebuffer] = {0,0,0,0,0,0,0,0,0,0};
//gpiotimer schaltet Weiche wenn über 0
int gpiotimer = 0;
//Anzahl Frames ueber die Groesse und Farbe gemittelt wird
#define timetodetect 5
//Anzahl Frames über die gewartet wird. Verhindert Doppelerkennung des selben Gummibärchen.
#define timetowait 20





//local function definitions
void ChangeDetection();
void DetectRegions();
void DrawBoundingBox(struct OSC_PICTURE *picIn, struct OSC_VIS_REGIONS *regions, s_color color);
void DrawRegion(struct OSC_PICTURE *picIn, struct OSC_VIS_REGIONS *regions, s_color color);
void toggle(struct OSC_VIS_REGIONS *regions);
void MaxArea(struct OSC_VIS_REGIONS *regions);
void Activated();
void Decisions();
void ControlGPIO(struct OSC_PICTURE *picIn, struct OSC_VIS_REGIONS *regions);
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
	OscGpioWrite(GPIO_OUT1, FALSE);
	OscGpioWrite(GPIO_OUT2, FALSE);
	//set initial status of IO
	outputIO = 1;


	FILE *fs = fopen("/home/httpd/stat.html", "w");
	fprintf(fs,"<tr><th>Schrittnummer</th><th>framediff</th><th>Biggest Area</th><th>AreaAvarage</th><th>B</th><th>G</th><th>R</th></tr> \n");
	fclose(fs);


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



		//call function for region detection
		DetectRegions();
		//DrawRegion(&Pic2, &ImgRegions, color);

		//save current image frame in BACKGROUND (before we draw the rectangles)
		if((data.ipc.state.nStepCounter==100)) { //each 100th pic captured, will be compared with BACKROUND.
			memcpy(data.u8TempImage[BACKGROUND], data.u8TempImage[SENSORIMG], sizeof(data.u8TempImage[BACKGROUND]));
		}

		//draw regions directly to the image (the image content is changed!)
		//DrawBoundingBox(&Pic2, &ImgRegions, color);

		MaxArea(&ImgRegions);

		//Activated();

		ControlGPIO(&Pic2, &ImgRegions);


/*
		if(!(data.ipc.state.nStepCounter%50)) {
			toggle(&ImgRegions);
		}
*/


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
			//if(Dif > NUM_COLORS*data.ipc.state.nThreshold) {
			//if the difference is larger than threshold value (can not be changed on web interface)
			if(Dif > 10) {

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

	//PrintObjectProperties(&ImgRegions); //Ausgabe der detektierten Objekte in Konsole unten; AREA: ca. 3500 Pixel (Änderung)

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

void DrawRegion(struct OSC_PICTURE *picIn, struct OSC_VIS_REGIONS *regions, s_color color) {
        uint16 o, cpl;
        uint8 *pImg = (uint8*)picIn->data;
        const uint16 width = picIn->width;
        //uint8 col[3] = {color.blue, color.green, color. red};
        uint8 col[2][3] = {{255,0,0},{0,255,0}};
        for(o = 0; o < regions->noOfObjects; o++) {
        	 struct OSC_VIS_REGIONS_RUN* CurrentRun = regions->objects[o].root;
                // Draw the horizontal lines. */
        	 do {
                for (uint16 c = CurrentRun->startColumn; c < CurrentRun->endColumn; c += 1) {
                	for(cpl = 0; cpl < NUM_COLORS; cpl++) {
                		pImg[(width * CurrentRun->row + c)* NUM_COLORS + cpl] = col[o%2][cpl];
                	}
                }
                CurrentRun = CurrentRun->next;
        	 } while (CurrentRun != 0);
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
	  err = OscGpioWrite(GPIO_OUT1, FALSE);
	  outputIO = 0;
    }else{
	  err = OscGpioWrite(GPIO_OUT1, TRUE);
	  outputIO = 1;
    }
	if (err != SUCCESS) {
	  fprintf(stderr, "%s: ERROR: GPIO write error! (%d)\n", __func__, err);
	}

	return;
}
/*
void toggle(struct OSC_VIS_REGIONS *regions)
{
    OSC_ERR err = SUCCESS;
    if(regions->noOfObjects>0){
	  err = OscGpioWrite(GPIO_OUT1, TRUE);
	  err = OscGpioWrite(GPIO_OUT2, TRUE);
	  outputIO = 0;
    }
    else{
	  err = OscGpioWrite(GPIO_OUT1, FALSE);
	  err = OscGpioWrite(GPIO_OUT2, FALSE);
	  outputIO = 1;
    }
	if (err != SUCCESS){
	  fprintf(stderr, "%s: ERROR: GPIO write error! (%d)\n", __func__, err);
	}
	return;
}
*/

//Suche grösste und zweitgrösste Änderung im aktuellen Bild verglichen mit Hintergrundbild.
//Wenn Änderung grösser als definierte Störungsänderung oder framediff==timetodetect, wird Activated() aufgerufen.
void MaxArea(struct OSC_VIS_REGIONS *regions){
	int temp = 0;
	int secondtemp = 0;
	int numbertemp = 0;
	for (int i = 0; i < regions->noOfObjects; i++){
		if (regions->objects[i].area >= temp){
			secondtemp = temp;
			temp = regions->objects[i].area;
			numbertemp = i;
		}
	}
	printf("Biggest Area: %d\n", temp);

	//RegionNumber und BiggestArea weitergeben (erst jetzt in externe Variable geschrieben):
	BiggestArea = temp;
	SecondBiggestArea = secondtemp;
	RegionNumber = numbertemp;
	//Hier wird bei genuegender Groesse der Aktiviert-Modus aktiviert.

	//Differenz Zeitstempel und aktuelle Zeit bzw. Frame
		framediff = data.ipc.state.nStepCounter-framestep;

	if (BiggestArea >= 1500 || framediff == timetodetect){
		Activated(&Pic2, &ImgRegions);
	}
}


void Activated(struct OSC_PICTURE *picIn, struct OSC_VIS_REGIONS *regions, s_color color)
{

	//Differenz Zeitstempel und aktuelle Zeit bzw. Frame
	framediff = data.ipc.state.nStepCounter-framestep;

	//Ist der Abstand genuegend gross, also nicht mehr das gleiche Gummibaerchen, kann erneut mit dem Errechnen eines Durchschnittes begonnen werden.
	if(framediff > timetowait){
		//Zeitstempel wird gesetzt
		framestep = data.ipc.state.nStepCounter;
		//Die Aufzählvariablen werden fuer das neue Gummibarchen wieder auf 0 gesetzt
		memset (colorcounter, 0, sizeof (colorcounter));
		BiggestAreaCounter = 0;
		SecondBiggestAreaCounter = 0;
		stp = 0;
	}

	//Sind wir noch in der Durchschnittsberechnung, wird diese Weitergefuehrt
	if(framediff < timetodetect){
		uint8 *pImg = (uint8*)picIn->data;
		const uint16 width = picIn->width;
		uint8 col[3] = {color.blue, color.green, color. red};
		//uint8 col[2][3] = {{255,0,0},{0,255,0}};
		struct OSC_VIS_REGIONS_RUN* CurrentRun = regions->objects[RegionNumber].root;
		do {
			for (uint16 c = CurrentRun->startColumn; c < CurrentRun->endColumn; c += 1) {
				for(uint16 cpl = 0; cpl < NUM_COLORS; cpl++) {

					//count color values
					colorcounter[cpl] = colorcounter[cpl] + pImg[(width * CurrentRun->row + c)* NUM_COLORS + cpl];
					stp++;
					pImg[(width * CurrentRun->row + c)* NUM_COLORS + cpl] = col[cpl];
				}
			}
			CurrentRun = CurrentRun->next;
		} while (CurrentRun != 0);
	BiggestAreaCounter += BiggestArea;
	SecondBiggestAreaCounter += SecondBiggestArea;
	}

/*
	printf("Der colorcounter betraegt: ");
	for(int k = 0; k < 3; k++){
	printf("%d ", colorcounter[k]);
	}
*/


	//Am Ende des Betrachtungzeitraumes
	if(framediff == timetodetect){
		//Durchschnitts-Variable wird auf Null gesetzt
		memset (coloravarage, 0, sizeof (coloravarage));
		//stp wird durch anzahl farben geteilt. Somit haben wir die totale anzahl analysierter pixel
		stp = stp/3;

		//Ueberpruefungsausgabe
		printf("\n");
		printf("Die 20-er Durchschnittsfarbe ist:");
		printf("\n");

		//Hier wird der Farbdurchschnitt errechnet:
		for(int coln = 0; coln < NUM_COLORS; coln++){
			if (stp > 0){
				coloravarage[coln] = colorcounter[coln]/stp;
			}
			printf("%d ", coloravarage[coln]); //Ausgabe in Konsole
		}
		//Hier wird die Durchschnittgrösse der BiggestArea und der SecondBiggestArea errechnet
		if (stp > 0){
			BiggestAreaAvarage = BiggestAreaCounter/timetodetect;
			SecondBigAvarage = SecondBiggestAreaCounter/timetodetect;
		}
		// Hier wird dann die decisions-Funktion aufgerufen.
		Decisions();
	}
}


void Decisions(){

	int white[6] = {76,119,135,200,89,144};
	int darkred [6] = {38,63,56,99,53,95};
	int lightred [6] = {0,255,0,255,0,255};
	int green [6] = {0,255,0,255,0,255};
	int yellow [6] = {0,255,0,255,0,255};


	int color = 0;
	int size = 0;


//Überprüfung ob Gummibärchen weiss ist
	if (data.ipc.state.nSortOutWhite == 1 && white[0] < coloravarage[0] && white[1] > coloravarage[0]  && white[2] < coloravarage[1] && white[3] > coloravarage[1]  && white[4] < coloravarage[2] && white[5] > coloravarage[2])
	{
		//Gummibaerchen ist weiss
		color = 1;
		if(BiggestAreaAvarage > 1500 && BiggestArea < 4000){
			size = 1;
		}

	}


//Überprüfung ob Gummibärchen dunkelrot (darkred) ist
	else if (data.ipc.state.nSortOutRed == 1 && darkred[0] < coloravarage[0] && darkred[1] > coloravarage[0]  && darkred[2] < coloravarage[1] && darkred[3] > coloravarage[1]  && darkred[4] < coloravarage[2] && darkred[5] > coloravarage[2])
	{
		//Gummibarrchen ist dunkelrot
		color = 1;
		if(BiggestAreaAvarage > 1500 && BiggestArea < 6000){
			size = 1;
		}
	}

//Überprüfung ob Gummibärchen hellrot (lightred) ist
	else if (data.ipc.state.nSortOutRed == 1 && lightred[0] < coloravarage[0] && lightred[1] > coloravarage[0]  && lightred[2] < coloravarage[1] && lightred[3] > coloravarage[1]  && lightred[4] < coloravarage[2] && lightred[5] > coloravarage[2])
	{
		//Gummibarrchen ist hellrot
		color = 1;
		if(BiggestAreaAvarage > 1500 && BiggestArea < 6000){
			size = 1;
		}
	}
	else if (lightred[0] < coloravarage[0] && lightred[1] > coloravarage[0]  && lightred[2] < coloravarage[1] && lightred[3] > coloravarage[1]  && lightred[4] < coloravarage[2] && lightred[5] > coloravarage[2])
	{
		//Gummibarrchen ist blabla

	}


	//Test: color und size sind immer = 1
	//color = 1;
	//size = 1;


	/*
	if (BiggestArea > 1500 && BiggestAreaAvarage < 3000)
	{
		size=1;
	}
	*/
	//Relevante Werte werden in Textdatei geschrieben / FARBANALYSE
	FILE *fs = fopen("/home/httpd/stat.html", "a");
	fprintf(fs,"<tr><td>%d</td><td>%d</td><td>%d</td><td>%d</td><td>%d</td><td>%d</td><td>%d</td></tr> \n", data.ipc.state.nStepCounter, framediff, BiggestArea, BiggestAreaAvarage, coloravarage[0], coloravarage[1], coloravarage[2]);
	fclose(fs);

	if (size == 1 && color == 1){
/*
		OSC_ERR err = SUCCESS;
		//Turn on GPIO
		err = OscGpioWrite(GPIO_OUT1, TRUE);
		outputIO = 1;
*/

		//Zeitstempel setzen auf erste Null-Position im Array timestamp
		for(int m = 0; m < sizetimebuffer; m++){
			if(timestamp[m] == 0){
				timestamp[m] = data.ipc.state.nStepCounter;
				m = sizetimebuffer;
			}
		}
	}
}

void ControlGPIO(struct OSC_PICTURE *picIn, struct OSC_VIS_REGIONS *regions){
	//Zeitstempelanalyse:

	//Hier kann eingestellt werden, wie viele Frames vergehen nach dem Entscheiden und dem Handeln, also Ausgang einschalten
	if (data.ipc.state.nStepCounter-timestamp[0] == 10){
		//Hier kann eingestellt werden wie lange der Ausgang eingeschaltet bleibt
		gpiotimer += 4;
		//Hier wird der abgearbeitete Zeitstempel verworfen und die restlichen rutschen eins nach oben
		memmove (&timestamp[0], &timestamp[1], sizeof(timestamp) - sizeof(*timestamp));
		timestamp[sizetimebuffer-1] = 0;
	}


//GPIOS ansteuern

	OSC_ERR err = SUCCESS;
	if(gpiotimer > 0){
		//Turn on GPIO
		err = OscGpioWrite(GPIO_OUT1, TRUE);
		  outputIO = 1;
		  gpiotimer--;
	}else{
		//Turn off GPIO
		err = OscGpioWrite(GPIO_OUT1, FALSE);
		outputIO = 0;
	}
	if (err != SUCCESS) {
		fprintf(stderr, "%s: ERROR: GPIO write error! (%d)\n", __func__, err);
	}


	/*
	printf("\n");
	for(int k = 0; k < sizetimebuffer; k++){
		printf("%d ", timestamp[k]);
	}
	printf("\n");
	printf("Aktueller Schritt:");
	printf("%d", data.ipc.state.nStepCounter);
	printf("\n");
	printf("GPIO-Timer:");
	printf("%d", gpiotimer);
	printf("\n");
*/

	printf("Weiss:");
	printf("%d", data.ipc.state.nSortOutWhite);
	printf("\n");
	printf("Rot:");
	printf("%d", data.ipc.state.nSortOutRed);
	printf("\n");
	printf("Belichtung:");
	printf("%d", data.ipc.state.nExposureTime);
	printf("\n");
	printf("Schwelle:");
	printf("%d", data.ipc.state.nThreshold);
	printf("\n");


}



