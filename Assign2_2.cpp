// Name:LIYAO JIANG(1512446)
// Section#: EA1
//
// Name：XIAOLEI ZHANG（1515335）
// Section#：LBL A1
//
// Assignment 2 Part 1 restaurant finder

//include headers for display and sd and touchscreen and the lcd_image library.
#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <Adafruit_ILI9341.h>
#include <Adafruit_GFX.h>
#include <TouchScreen.h>
#include "lcd_image.h"
#include <math.h>

#define TFT_DC 9
#define TFT_CS 10
#define SD_CS 6

#define TFT_WIDTH  320
#define TFT_HEIGHT 240

// touch screen pins, obtained from the documentaion
#define YP A2  // must be an analog pin, use "An" notation!
#define XM A3  // must be an analog pin, use "An" notation!
#define YM  5  // can be a digital pin
#define XP  4  // can be a digital pin

// calibration data for the touch screen, obtained from documentation
// the minimum/maximum possible readings from the touch point
#define TS_MINX 150
#define TS_MINY 120
#define TS_MAXX 920
#define TS_MAXY 940

// thresholds to determine if there was a touch
#define MINPRESSURE   10
#define MAXPRESSURE 1000

//related to resturant information
#define REST_START_BLOCK 4000000
#define NUM_RESTAURANTS 1066

// These constants are for the 2048 by 2048 map.
#define MAP_WIDTH 2048
#define MAP_HEIGHT 2048
#define LAT_NORTH 5361858l
#define LAT_SOUTH 5340953l
#define LON_WEST -11368652l
#define LON_EAST -11333496l

// These functions convert between x/y map position and lat/lon
// (and vice versa.)
int32_t x_to_lon(int16_t x) {
    return map(x, 0, MAP_WIDTH, LON_WEST, LON_EAST);
}

int32_t y_to_lat(int16_t y) {
    return map(y, 0, MAP_HEIGHT, LAT_NORTH, LAT_SOUTH);
}

int16_t lon_to_x(int32_t lon) {
    return map(lon, LON_WEST, LON_EAST, 0, MAP_WIDTH);
}

int16_t lat_to_y(int32_t lat) {
    return map(lat, LAT_NORTH, LAT_SOUTH, 0, MAP_HEIGHT);
}

//initialize the TouchScreen
TouchScreen ts = TouchScreen(XP, YP, XM, YM, 300);

//initialize rawdata read
Sd2Card card;

//struct use to save the information pulled from sd
struct restaurant {
  int32_t lat;
  int32_t lon;
  uint8_t rating; // from 0 to 10
  char name[55];
};

//smaller struct, due to memory limitation
struct RestDist {
  uint16_t index; // index of restaurant from 0 to NUM_RESTAURANTS-1
  uint16_t dist;  // Manhatten distance to cursor position
};

//global variable that used for storing the block containing
//the required resturant
restaurant restBlock[8];
//initialize the variable that stores which block was read last time
uint32_t previousblockNum = 0;

//related to displaying map
Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC);

//load the whole yegImage file from SD
#define DISPLAY_WIDTH  320
#define DISPLAY_HEIGHT 240
#define YEG_SIZE 2048

lcd_image_t yegImage = { "yeg-big.lcd", YEG_SIZE, YEG_SIZE };

//initialize the input of joystick
#define JOY_VERT  A1 // should connect A1 to pin VRx
#define JOY_HORIZ A0 // should connect A0 to pin VRy
#define JOY_SEL   2

//constants related to joystick
#define JOY_CENTER   512
#define JOY_DEADZONE 64

//define size of cursor
#define CURSOR_SIZE 10

//initialize the cursor position on the display
//and the variables for storing previous postion of cursor
int cursorX, cursorY;
int precursorX, precursorY;
//two booleans will be used for checking if the joystick had moved
bool changeposx =0;
bool changeposy =0;
//four booleans tracking if the cursor previously at the edeg
bool leftedge = 0;
bool rightedge =0;
bool topedge =0;
bool bottomedge =0;

//left top corner coordinate of the drawed portion of the map
int yegX;
int yegY;

int num = NUM_RESTAURANTS;

// forward declaration for redrawing the cursor
void redrawCursor(uint16_t colour);

void setup() {
  //standard initialize process of serial monitor, joystick
  //SD card, rotation of screen
  init();

  Serial.begin(9600);

	pinMode(JOY_SEL, INPUT_PULLUP);

	tft.begin();

	Serial.print("Initializing SD card...");
	if (!SD.begin(SD_CS)) {
		Serial.println("failed! Is it inserted properly?");
		while (true) {}
	}
	Serial.println("OK!");

  // initialize SPI (serial peripheral interface)
  // communication between the Arduino and the SD controller

  Serial.print("Initializing SPI communication for raw reads...");
  if (!card.init(SPI_HALF_SPEED, SD_CS)) {
    Serial.println("failed! Is the card inserted properly?");
    while (true) {}
  }
  else {
    Serial.println("OK!");
  }

	tft.setRotation(3);

  //fill the background with black
  tft.fillScreen(ILI9341_BLACK);

  // draws the centre of the Edmonton map, leaving the rightmost 48 columns black
	yegX = YEG_SIZE/2 - (DISPLAY_WIDTH - 48)/2;
	yegY = YEG_SIZE/2 - DISPLAY_HEIGHT/2;
	lcd_image_draw(&yegImage, &tft, yegX, yegY,
                 0, 0, DISPLAY_WIDTH - 48, DISPLAY_HEIGHT);

  // initial cursor position is the middle of the screen
  //initial previous cursor position in the middle
  cursorX =(DISPLAY_WIDTH - 48)/2;
  cursorY = DISPLAY_HEIGHT/2;
  precursorX= cursorX;
  precursorY= cursorY;
  //draw the redrawCursor in the center
  redrawCursor(ILI9341_RED);
}

//funciton that detects touch from lecture 13
//used for changerating
//returns 0 when no touch
//returns 1 when minus button touched
//returns 2 when plus button touched
uint8_t checkTouch() {
	TSPoint touch = ts.getPoint();

	if (touch.z < MINPRESSURE || touch.z > MAXPRESSURE) {
		// no touch, just quit
		return 0;
	}

	// get the y coordinate of where the display was touched
	// remember the x-coordinate of touch is really our y-coordinate
	// on the display
	int touchY = map(touch.x, TS_MINX, TS_MAXX, 0, TFT_HEIGHT - 1);

	// need to invert the x-axis, so reverse the
	// range of the display coordinates
	int touchX = map(touch.y, TS_MINY, TS_MAXY, TFT_WIDTH - 1, 0);

  // when minus touched
  if( ((276< touchX) && (touchX < 276+40)) && ((160< touchY) && (touchY <160+65))){
    return 1;
  }
  //when plus touched
  else if (((276< touchX) && (touchX < 276+40)) && ((85< touchY) && (touchY <85+65))){
    return 2;
  }
  else{
    //no touch on either plus or minus button
    return 0;
  }
}

void scrollmap(int direction){
  //up
  if(direction == 0){
    yegY = constrain(yegY-120,0,YEG_SIZE-DISPLAY_HEIGHT);
    lcd_image_draw(&yegImage, &tft, yegX, yegY,
                   0, 0, DISPLAY_WIDTH - 48, DISPLAY_HEIGHT);
  }
  //down
  else if(direction == 1){
    yegY = constrain(yegY+120,0,YEG_SIZE-DISPLAY_HEIGHT);
    lcd_image_draw(&yegImage, &tft, yegX, yegY,
                   0, 0, DISPLAY_WIDTH - 48, DISPLAY_HEIGHT);
  }
  //left
  else if(direction == 2){
    yegX = constrain(yegX-136,0,YEG_SIZE-(DISPLAY_WIDTH-48));
    lcd_image_draw(&yegImage, &tft, yegX, yegY,
                   0, 0, DISPLAY_WIDTH - 48, DISPLAY_HEIGHT);
  }
  //rigt
  else if(direction == 3){
    yegX = constrain(yegX+136,0,YEG_SIZE-(DISPLAY_WIDTH-48));
    lcd_image_draw(&yegImage, &tft, yegX, yegY,
                   0, 0, DISPLAY_WIDTH - 48, DISPLAY_HEIGHT);
  }

  // move cursor position back to middle of the screen
  //initialize previous cursor position in the middle
  cursorX =(DISPLAY_WIDTH - 48)/2;
  cursorY = DISPLAY_HEIGHT/2;
  precursorX= cursorX;
  precursorY= cursorY;
  //draw the redrawCursor in the center again
  redrawCursor(ILI9341_RED);
}

void redrawCursor(uint16_t colour) {
  tft.fillRect(cursorX - CURSOR_SIZE/2, cursorY - CURSOR_SIZE/2,
               CURSOR_SIZE, CURSOR_SIZE, colour);
}

//function developed for redraw the patch of edmonton map where the cursor
//was previously on without redrawing the whole screen
void redrawMap(){
  // define the coordinate of the patch of map we want
  int MappatchX = yegX+(precursorX - CURSOR_SIZE/2);
  int MappatchY = yegY+(precursorY - CURSOR_SIZE/2);
	// int MappatchX = (YEG_SIZE/2 - (DISPLAY_WIDTH - 48)/2)+(precursorX - CURSOR_SIZE/2);
	// int MappatchY = (YEG_SIZE/2 - DISPLAY_HEIGHT/2)+(precursorY - CURSOR_SIZE/2);
  //draw the patch with size of the cursor and
  //draw it on the previous position of the cursor
  lcd_image_draw(&yegImage, &tft, MappatchX, MappatchY,
                 precursorX - CURSOR_SIZE/2,
                 precursorY - CURSOR_SIZE/2,
                 CURSOR_SIZE, CURSOR_SIZE);
}

//In mode2 process the movement of joystick
//when it reaches after bottom item or top item, do page scrolling
bool joysticklist(int& selectedRest, int& page){
  int yVal = analogRead(JOY_VERT);
  //page rolling cases
  //go to next page
  if( (page != ceil(num/30)) && (selectedRest == 29) && (yVal > JOY_CENTER + JOY_DEADZONE)){
    page = page +1;
    selectedRest = 0;
    return 1;
  }
  //go to previous page
  else if( page != 0 && (selectedRest == 0) && (yVal < JOY_CENTER - JOY_DEADZONE)){
    page = page -1;
    selectedRest = 0;
    return 1;
  }
  //case with no page change
  // joystick moved down
  else if (yVal > JOY_CENTER + JOY_DEADZONE) {
    if(page == ceil(num/30)){
      selectedRest = constrain(selectedRest+1,0,(num%30)-1);
    }
    else{
      selectedRest = constrain(selectedRest+1,0,29);
    }
    return 0;
  }
  //joystick moved up
  else if (yVal < JOY_CENTER - JOY_DEADZONE){
    selectedRest = constrain(selectedRest-1,0,29);
    return 0;
  }
  else{
    return 0;
  }
}

// read the restaurant at position "restIndex" from the card
// and store at the location pointed to by restPtr
void getRestaurantFast(int restIndex, restaurant* restPtr) {
  // calculate the block containing this restaurant
  uint32_t blockNum = REST_START_BLOCK + restIndex/8;

  // Serial.println(blockNum);

  // fetch the block of restaurants containing the restaurant
  // with index "restIndex"
  // only reads the block if this restaurants is in a different block
  if(previousblockNum != blockNum){
    //we store the block read by readBlock in a global variable restBlock
    //then in the next call, if the restaurant and the last restaurant belongs
    //to the same block, we don't need to read it again
    while (!card.readBlock(blockNum, (uint8_t*) restBlock)) {
      Serial.println("Read block failed, trying again.");
    }
  }

  // Serial.print("Loaded: ");
  // Serial.println(restBlock[0].name);

  *restPtr = restBlock[restIndex % 8];
  previousblockNum = blockNum;
}

//Manhatten distance btw cursor and each restaurant
uint16_t Manhatten(uint16_t x1,uint16_t y1,uint16_t x2,uint16_t y2){
  uint16_t distance;
  uint16_t p1= abs(x1-x2);
  uint16_t p2= abs(y1-y2);
  distance = p1+p2;
  return distance;
}

bool getrest_dist(uint8_t minstar, int restIndex, RestDist& disPtr) {
  // calculate the block containing the restaurant
  uint32_t blockNum = REST_START_BLOCK + restIndex/8;

  // fetch the block of restaurants containing the restaurant
  // with index "restIndex"
  // only reads the block if this restaurants is in a different block
  if(previousblockNum != blockNum){
    //we store the block read by readBlock in a global variable restBlock
    //then in the next call, if the restaurant and the last restaurant belongs
    //to the same block, we don't need to read it again
    while (!card.readBlock(blockNum, (uint8_t*) restBlock)) {
      Serial.println("Read block failed, trying again.");
    }
  }
  //calculate the rating in star form for the restaurant
  uint8_t rating = restBlock[restIndex%8].rating;
  uint8_t star = max(floor((rating+1)/2),1);
  uint16_t x0= yegX+cursorX;
  uint16_t y0= yegY+cursorY;
  uint16_t x = lon_to_x(restBlock[restIndex%8].lon);
  uint16_t y = lat_to_y(restBlock[restIndex%8].lat);
  uint16_t distance = Manhatten(x0,y0,x,y);
  disPtr.dist = distance;
  disPtr.index= restIndex;
  //calculate Manhatten distance btw restaurant and cursor;
  //map position of cursor (x0,y0)
  previousblockNum = blockNum;

  // if it meets the minimum star
  if( star >= minstar){
    return 1;
  }
  // if it does not meet the minimum required star
  else if (star < minstar){
    return 0;
  }

}

// Swap two restaurants of RestDist struct
void swap_rest(RestDist *ptr_rest1, RestDist *ptr_rest2) {
  RestDist tmp = *ptr_rest1;
  *ptr_rest1 = *ptr_rest2;
  *ptr_rest2 = tmp;
}

// //OLD VERSION Selection sort
// // rest_dist is an array of RestDist, from rest_dist[0] to rest_dist[len-1]
// void ssort(RestDist *rest_dist, int len) {
//   for (int i = len-1; i >= 1; --i) {
//     // Find the index of furthest restaurant
//     int max_idx = 0;
//     for (int j = 1; j <= i; ++j) {
//       if(rest_dist[j].dist > rest_dist[max_idx].dist) {
//         max_idx = j;
//       }
//     }
//     // Swap it with the last element of the sub-array
//     swap_rest(&rest_dist[i], &rest_dist[max_idx]);
//   }
// }

//pivot function
//implemented based on the lecture slide, used by the quicksort
//make all entries on the left to pi smaller or equal than pi
//all the entries on the right to pi bigger than pi
int pivot(RestDist *rest_dist, int len, int pi){
	swap_rest(&rest_dist[pi], &rest_dist[len-1]);
	int lo=0;
	int hi=len-2;
	while(lo <= hi){
		if(rest_dist[lo].dist <= rest_dist[len-1].dist){
			lo++;
		}
		else if(rest_dist[hi].dist > rest_dist[len-1].dist){
			hi--;
		}
		else{
			swap_rest(&rest_dist[lo],&rest_dist[hi]);
		}
	}
	swap_rest(&rest_dist[lo],&rest_dist[len-1]);
	return lo;
}

//NEW METHOD Quickpivot sort
void qsort(RestDist *rest_dist, int len){
	//base case
	if(len <= 1){
		return;
	}
	//pick a pivot to be the middle, for convinience, we take the middle one
	int pi = floor(len/2);
	int new_pi = pivot(rest_dist, len , pi);
  //for left and right part next to pi
  //do quicksort recursively
	qsort(rest_dist,new_pi);
	qsort(rest_dist + new_pi,len-new_pi);
}

void sortedres(uint8_t minstar,RestDist *rest_dist){
  //only store the restaurant at the next index if the restaurant meets
  //the minimum star rating required
  int i=0;
  int reali=0;
  num = NUM_RESTAURANTS;
  while((i < NUM_RESTAURANTS) && (reali < num)){
    bool minimum = getrest_dist(minstar,i,rest_dist[reali]);
    if(minimum == 0){
      num = num -1;
      i++;
    }
    else if(minimum == 1){
      i++;
      reali++;
    }
  }

  //we use the new quick pivot sort to sort the restaurants
  qsort(rest_dist,num);

  // //TEST printing sorted restaurants
  // Serial.println("Sorted!");
  // for (int i=0 ; i< num; i++){
  //   Serial.print(rest_dist[i].index);
  //   Serial.print(", ");
  //   Serial.println(rest_dist[i].dist);
  // }
}

void displayNames(RestDist *restDist){

  int selectedRest = 0; // which restaurant is selected?
  int preselectedRest = 0;
  restaurant r;
  //variables storing the page information
  int page = 0;
  bool pagechange = 1;


  while(true){
    if(pagechange == 1){
      tft.fillScreen(0);
      tft.setTextSize(1);
      tft.setCursor(0, 0); // where the characters will be displayed
      tft.setTextWrap(false);

      if (page == ceil(num/30)){
        for (int i =( 0+ page*30); i < num; i++) {
          getRestaurantFast(restDist[i].index, &r);
          if ((i % 30) != selectedRest) { // not highlighted
            tft.setTextColor(0xFFFF, 0x0000); // white characters on black background
          } else { // highlighted
            tft.setTextColor(0x0000, 0xFFFF); // black characters on white background
          }
          tft.print(r.name);
          tft.print("\n");
        }
      }
      else{
        for (int i =( 0+ page*30); i < (30+page*30); i++) {
          getRestaurantFast(restDist[i].index, &r);
          if ((i % 30) != selectedRest) { // not highlighted
            tft.setTextColor(0xFFFF, 0x0000); // white characters on black background
          } else { // highlighted
            tft.setTextColor(0x0000, 0xFFFF); // black characters on white background
          }
          tft.print(r.name);
          tft.print("\n");
        }
      }
      tft.print("\n");
      pagechange = 0;
    }

    int buttonVal = digitalRead(JOY_SEL);
    //if button pressed down
    if(buttonVal == 0){
      getRestaurantFast(restDist[page*30+selectedRest].index,&r);
      //Test of the restaurant selected
      // Serial.println("restaurant number");
      // Serial.println(page*30+selectedRest);
      // Serial.println(restDist[page*30+selectedRest].index);
      // Serial.println(r.name);
      //GO BACK TO THE MAP MODE
      //get the coordinate of selected restaurant
      int resX = lon_to_x(r.lon);
      int resY = lat_to_y(r.lat);
      //for case that restaurant outside the map
      //half cursor size=5(if cursor cannot fully displayed)
      //also counts as outside map
      if( (resX < 5)||(resX > YEG_SIZE-5)||(resY < 5)||(resY > YEG_SIZE-5) ){
        //get screen position
        yegX = resX-(DISPLAY_WIDTH - 48)/2;
        yegY = resY- DISPLAY_HEIGHT/2;
        //check for each case, set cursor
        //X falls on map leftside
        if(yegX < 0){cursorX = CURSOR_SIZE/2;}
        //X falls on map rightside
        else if(yegX > YEG_SIZE-(DISPLAY_WIDTH - 48)){
          cursorX = 272-CURSOR_SIZE/2;
        }
        //when nothing wrong with X,put X in middle
        else {cursorX = (DISPLAY_WIDTH-48)/2;}
        //Y falls on map topside
        if(yegY < 0){cursorY = CURSOR_SIZE/2;}
        //y falls on map bottomside
        else if (yegY > YEG_SIZE-DISPLAY_HEIGHT){
          cursorY = 240 - CURSOR_SIZE/2;
        }
        //when nothing wrong with Y,put Y in middle
        else{cursorY = DISPLAY_HEIGHT/2;}
        //keep the screen inside the map, when the screen falls outside the map
        yegX = constrain(resX-(DISPLAY_WIDTH - 48)/2, 0 ,YEG_SIZE-(DISPLAY_WIDTH - 48));
        yegY = constrain(resY- DISPLAY_HEIGHT/2, 0 ,YEG_SIZE-DISPLAY_HEIGHT);
      }
      //check for case restaurant close to boundrary
      //cannot put cursor in middle
      else if((resY<DISPLAY_HEIGHT/2)||(resY > YEG_SIZE - DISPLAY_HEIGHT/2)||(resX < 272/2)||(resX > YEG_SIZE - 272/2)){
        //keep the screen inside the map, when the screen falls outside the map
        yegX = constrain(resX-(DISPLAY_WIDTH - 48)/2, 0 ,YEG_SIZE-(DISPLAY_WIDTH - 48));
        yegY = constrain(resY- DISPLAY_HEIGHT/2, 0 ,YEG_SIZE-DISPLAY_HEIGHT);
        //find the displacement between the constrained postion and original position
        int diffX = yegX - (resX-(DISPLAY_WIDTH - 48)/2);
        int diffY = yegY - (resY- DISPLAY_HEIGHT/2);
        //shift the cursor back to get rid of the displacement
        cursorX = (DISPLAY_WIDTH - 48)/2 - diffX;
        cursorY = (DISPLAY_HEIGHT/2) - diffY ;
      }

      //normal case cursor in the center
      else{
        yegX = resX-(DISPLAY_WIDTH - 48)/2;
        yegY = resY- DISPLAY_HEIGHT/2;
        // move cursor position back to middle of the screen
        cursorX =(DISPLAY_WIDTH - 48)/2;
        cursorY = DISPLAY_HEIGHT/2;
      }

      //initialize previous cursor position
      precursorX= cursorX;
      precursorY= cursorY;
      //draw black background
      tft.fillScreen(ILI9341_BLACK);
      //draw map
      lcd_image_draw(&yegImage, &tft, yegX, yegY,
        0, 0, DISPLAY_WIDTH - 48, DISPLAY_HEIGHT);
      //redrawCursor in the center again
      redrawCursor(ILI9341_RED);
      break;
    }
    preselectedRest = selectedRest;
    //check pagechange
    pagechange = joysticklist(selectedRest,page);
    delay(30);
    if(preselectedRest != selectedRest && pagechange == 0){
      //Testing highlight movements
      // Serial.println("highlight moves");
      //change the previous selected to normal
      getRestaurantFast(restDist[30*page + preselectedRest].index,&r);
      tft.setTextColor(0xFFFF, 0x0000);
      //set the cursor
      tft.setCursor(0,8*preselectedRest);
      tft.print(r.name);
      //change the new selected to highlight
      getRestaurantFast(restDist[30*page+selectedRest].index,&r);
      tft.setTextColor(0x0000, 0xFFFF);
      //set the cursor
      tft.setCursor(0,8*selectedRest);
      tft.print(r.name);
    }
  }
}

//draw the "ui" of the rating selector
void rating(uint8_t star){
  //fill the rightmost 48 columns to be black
  tft.fillRect(273,0,48,240,0);
  //draw the rating selector buttons
  tft.drawRect(276,10,40,65,ILI9341_YELLOW);
  tft.drawRect(276,85,40,65,ILI9341_RED);
  tft.drawRect(276,160,40,65,ILI9341_RED);
  tft.setTextSize(5);
  tft.setTextColor(0xFFFF, 0x0000);
  //display the selected star level
  tft.setCursor(283,30);
  tft.print(star);
  tft.setCursor(283,105);
  tft.print("+");
  tft.setCursor(283,185);
  tft.print("-");
}

//change the rating using TouchScreen
uint8_t changerating(uint8_t star){
  uint8_t touch = checkTouch();
  if(touch == 2){
    star = constrain(star + 1,1,5);
    rating(star);
    delay(50);
  }
  else if(touch == 1){
    star = constrain(star -1,1,5);
    rating(star);
    delay(50);
  }
  else{}
  return star;
}

//function that reads the movement of the joystick
//and modify the cursor position
void processJoystick() {
  uint8_t star = 1;
  rating(star);
  while(true){
    //the rating selector gives the minimum standard stars the user want
    //the restaurants displayed should have equal or higher stars
    star = changerating(star);
    // reading from joystick
    int xVal = analogRead(JOY_HORIZ);
    int yVal = analogRead(JOY_VERT);
    int buttonVal = digitalRead(JOY_SEL);
    //initalize the sensitivity variable designed to make the cursor movement
    //speed depends on how far you push the joystick from the center
    int sensitivity = 0;

    //if button pressed down
    if(buttonVal == 0){
      RestDist rest_dist[NUM_RESTAURANTS];
      sortedres(star,rest_dist);
      displayNames(rest_dist);
      star = 1;
      rating(star);
      continue;
    }

    // joystick moved up
    if (yVal < JOY_CENTER - JOY_DEADZONE) {
      if(topedge == 1){
        scrollmap(0);
        topedge = 0;
        continue;
      }
      //sensitivity is proportional to how far you pushed the joystick
      sensitivity= ((JOY_CENTER - JOY_DEADZONE)-yVal)/80;
      //the constain function can make sure the cursor moves with in the screen
      cursorY = constrain(cursorY-sensitivity,CURSOR_SIZE/2,DISPLAY_HEIGHT-CURSOR_SIZE/2); // decrease the y coordinate of the cursor
      //the y coordinate changed
      changeposy= 1;
      if(cursorY == CURSOR_SIZE/2){
        topedge = 1;
      }
    }
    // joystick moved down
    // similar designed sensitivity and constrain as above statement
    else if (yVal > JOY_CENTER + JOY_DEADZONE) {
      if(bottomedge == 1){
        scrollmap(1);
        bottomedge = 0;
        continue;
      }
      sensitivity= (yVal-(JOY_CENTER + JOY_DEADZONE))/80;
      cursorY = constrain(cursorY+sensitivity,CURSOR_SIZE/2,DISPLAY_HEIGHT-CURSOR_SIZE/2);
      changeposy= 1;
      if(cursorY == DISPLAY_HEIGHT-CURSOR_SIZE/2){
        bottomedge = 1;
      }
    }
    // if the y coordinate was not changed
    else{changeposy= 0;}

    // remember the x-reading increases as we push left
    // joystick pushed to the left
    if (xVal > JOY_CENTER + JOY_DEADZONE) {
      if(leftedge == 1){
        scrollmap(2);
        leftedge = 0;
        continue;
      }
      sensitivity= (xVal-(JOY_CENTER + JOY_DEADZONE))/80;
      cursorX = constrain(cursorX-sensitivity,CURSOR_SIZE/2,(DISPLAY_WIDTH - 48)-CURSOR_SIZE/2);
      changeposx= 1;
      if(cursorX == CURSOR_SIZE/2){
        leftedge = 1;
      }
    }
    // joystick pushed to the right
    else if (xVal < JOY_CENTER - JOY_DEADZONE) {
      if(rightedge == 1){
        scrollmap(3);
        rightedge = 0;
        continue;
      }
      sensitivity= ((JOY_CENTER - JOY_DEADZONE)-xVal)/80;
      cursorX = constrain(cursorX+sensitivity,CURSOR_SIZE/2,(DISPLAY_WIDTH - 48)-CURSOR_SIZE/2);
      changeposx= 1;
      if(cursorX == (DISPLAY_WIDTH-48)-CURSOR_SIZE/2){
        rightedge = 1;
      }
    }
    // if the x coordinate was not changed
    else{changeposx= 0;}

    //using the two boolean variables to determined if the joystick moved the
    //cursor, only redraw when it is moving to avoid flickering
    if(changeposx || changeposy){
      redrawMap();
      redrawCursor(ILI9341_RED);
    }
    //save the current cursor position as previous position
    //for redrawing map patch
    precursorY = cursorY;
    precursorX = cursorX;
  }
}

int main() {
	setup();

  processJoystick();

	Serial.end();
	return 0;
}
