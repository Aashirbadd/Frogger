#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include "framebuffer.h"
#include <pthread.h>
#include <unistd.h>
#include "initGPIO.h"
#include <math.h>
#include "mainMenuStart.c"
#include "mainMenuQuit.c"
#include "pauseRestart.c"
#include "pauseQuit.c"
#include "HUD.c"
#include "frogSprite.c"
#include "carR.c"
#include "carL.c"
#include "GameOver.c"
#include "YouWon.c"

/*Frogger Game
 * 
 * Author 1: Aashirbad Dhital, 	UCID 3009 2107, 	Lecture 2
 * Author 2: Mikal Munir, 		UCID 3008 6727, 	Lecture 2
 * 
 */

#define CLK 11
#define LAT 9
#define DAT 10
#define CLO_REG 0xFE003004      // System Clock Address

unsigned int *gpio;

#define INP_GPIO(g) *(gpio+((g)/10)) &= ~(7<<(((g)%10)*3))          // Changes the GPIO pin to input mode via the formula
#define OUT_GPIO(g) *(gpio+((g)/10)) |= (1<<(((g)%10)*3))           // Changes the GPIO pin to output mode via the formula
#define SET_GPIO_ALT(g, a) *(gpio+(((g)/10))) |= (((a) <=3?(a)+4?3:2)<<(((g)%10)*3))//Get this one fixed eh.

// Global Data

char buttonMapping[16][20] = {"B\0", "Y\0", "Select\0", "Start\0", "Joy-Pad UP\0", "Joy-Pad Down\0", "Joy-Pad Left\0",
                              "Joy-Pad Right\0",
                              "A\0", "X\0", "Left Bumper\0", "Right Bumper\0"};
int buttons[16];

int screenX = 1280;                    // Sets the boundry for the screen x coordinates
int screenY = 720;                    // Sets the boundry for the screen y coordinates
int lives = 4;                        // Initialises the number of lives
int cTime = 551;                    // Initialises the total remaining time
int cMoves = 207;                    // Initialises total moves remaining
int timeDecrementer = 100;
int state = 0;                        // Keeps track of game state

int
fmt(int i) {                        // Function used to format integer values to a multiple of 32 for uniformity of the game elements.
    return i * 32;
}

/* Definitions */
typedef struct {                    // Definition for pixel.
    int color;
    int x, y;
} Pixel;

struct shared {
    int fPos[2];                    // Stores the x and y coordinates of the frog
    int **cR1;                        // Stores the coordinates for all obsticles.
};

struct fbs framebufferstruct;        // Initialising Frame Buffer
void drawPixel(Pixel *pixel);

void *controllerThreadFunc(void *voidPointer);

void *visualThreadFunction(void *voidPointer);

void *backgroundThreadFunction(void *voidPointer);
//Controller Functions


void Init_GPIO() {
    INP_GPIO(CLK);                        // Setting to input clears the lines
    INP_GPIO(LAT);
    INP_GPIO(DAT);                        // Setting the line to input, and leaving it there
    OUT_GPIO(CLK);                        // Setting clock to output.
    OUT_GPIO(LAT);                        // Setting latch to output.
}

void Write_Latch() {                        // SNES controller driver function
    int setValue = 1 << LAT;
    gpio[7] = setValue;
}

void Clear_Latch() {                        // SNES controller driver function

    int clearValue = 1 << LAT;
    gpio[10] = clearValue;

}

void Write_Clock() {                        // SNES controller driver function
    int setValue = 1 << CLK;
    gpio[7] = setValue;
}

void Clear_Clock() {                        // SNES controller driver function

    int clearValue = 1 << CLK;
    gpio[10] = clearValue;

}

int Read_Data() {                        // SNES controller driver function
    int v = (gpio[13] >> DAT) & 1;
    return v;

}

void Wait(int microSeconds) {            // SNES controller driver function

    usleep(microSeconds);

}

int Read_SNES() {                        // SNES controller driver function

    int i = 0;
    Write_Latch();
    Write_Clock();
    Wait(12);
    Clear_Latch();

    while (i < 16) {
        Wait(6);
        Clear_Clock();
        Wait(6);
        Read_Data();
        buttons[i] = Read_Data();
        Write_Clock();
        i++;
    }

    return 0;

}

// Global data
struct shared s;            // Define the struct here
int multiplier = 1;


int
frogHit(int x, int y) {                // Functcion returns true if there is a colision with the frog, false otherwise.
    if ((x >= s.fPos[0] && x <= (s.fPos[0] + 32)) && (y >= s.fPos[1] && y <= (s.fPos[1] + 32))) {
        return 1;
    } else return 0;
}


int yPos = 0;
int arraySize = 1;
int width = 2;
int speed = 3;
int start = 4;
int elements = 11;
short bufferArray[1280][720];

int carRows = 13;                        // Tells other functions the total number of obsticles.
/* main function */

int cc(int x,
       int y) {                    // Converts i, j coordinates to an offset for array of moving objects (cars, logs, ..)
    return ((x * elements) + y);
}

int c(int x, int y) {                    // Converts i, j coordinates to an object for the framebuffer array.
    return ((x * screenX) + y);
}

void drawBackground() {                    // Renders the background into the bufferArray to be displayed later
    for (int y = 0; y < screenY; y++) {
        for (int x = 0; x < screenX; x++) {
            bufferArray[x][y] = 0x7A7A7A;
        }
    }
}

void drawFrog() {                        // Renders the frog into the bufferArray to be displayed later
    short int *frogPtr = frogSprite.pixel_data;
    int i = 0;
    for (int y = s.fPos[1]; y < s.fPos[1] + 32; y++) {
        for (int x = s.fPos[0]; x < s.fPos[0] + 32; x++) {
            bufferArray[x][y] = frogPtr[i];
            i++;
        }
    }
}

void drawCars() {                        // Renders the cars into the bufferArray to be displayed later
    for (int j = 0; j < carRows; j++) {
        int cWidth = s.cR1[cc(j, width)];
        int yCar = s.cR1[cc(j, yPos)];
        int xCar;
        short int *carRPtr = carR.pixel_data;
        short int *carLPtr = carL.pixel_data;
        short int *currentCar;
        int currentSpeed = s.cR1[cc(j, speed)];
        if (currentSpeed > 0) {
            currentCar = carR.pixel_data;
        } else {
            currentCar = carL.pixel_data;
        }
        for (int i = start; i < s.cR1[cc(j, arraySize)]; i++) {

            int counter = 0;
            xCar = s.cR1[cc(j, i)];
            for (int y = yCar; y < yCar + (32 * multiplier); y++) {
                if (xCar < 0) xCar = 0;
                for (int x = xCar; (x < (xCar + (cWidth))) && (x < screenX); x++) {
                    bufferArray[x][y] = currentCar[counter];
                    counter++;
                }
            }
        }
    }
}

void drawMenu(Pixel *pixel) {            // Displays the main menu
    short int *menuPtr;
    if (state == 0) {
        menuPtr = menuStart.pixel_data;
    }
    if (state == 1) {
        menuPtr = menuQuit.pixel_data;
    }
    int i = 0;
    for (int y = 0; y < 720; y++) {
        for (int x = 0; x < 1280; x++) {
            pixel->color = menuPtr[i];
            pixel->x = x;
            pixel->y = y;

            drawPixel(pixel);
            i++;

        }
    }
}

void drawPause(Pixel *pixel) {            // Displays the pause menu
    short int *pausePtr;
    if (state == 3) {
        pausePtr = restart.pixel_data;
    }
    if (state == 4) {
        pausePtr = quit.pixel_data;
    }
    int i = 0;
    for (int y = 0; y < 720; y++) {
        for (int x = 0; x < 1280; x++) {
            pixel->color = pausePtr[i];
            pixel->x = x;
            pixel->y = y;

            drawPixel(pixel);
            i++;

        }
    }
}

void drawHUD() {                            // Draws the hud to the bufferArray to be rendered later.
    short int *hudPtr = hudData.pixel_data;
    int i = 0;
    int rMoves = cMoves - 207;
    int remainingMoves = 975 + rMoves;
    for (int y = 660; y < 720; y++) {
        for (int x = 0; x < 1280; x++) {
            bufferArray[x][y] = hudPtr[i];
            if (lives <= 0 && x >= 1110) {
                bufferArray[x][y] = 0;
            } else if (lives <= 1 && x >= 1152) {
                bufferArray[x][y] = 0;
            } else if (lives <= 2 && x >= 1192) {
                bufferArray[x][y] = 0;
            } else if (lives <= 3 && x >= 1235) {
                bufferArray[x][y] = 0;
            }
            if (x > cTime + 70 && x < 621) {
                bufferArray[x][y] = 0;
            }
            if (x >= remainingMoves && x < 975) {
                bufferArray[x][y] = 0;
            }
            i++;
        }
    }
}

int specialFreeze[4] = {0, 0, 0,
                        2};                // Data structure to represent one of the special power ups (active, x, y, type)
int specialSpeed[4] = {0, 0, 0,
                       -4};                // Data structure to represent one of the special power ups (active, x, y, type)
int speedMultiplier = 1;                            // State variable altered by the special power ups.


void
drawSpecial() {                                    // Draws a image of the special block into the buffer array to be displayed later.
    if (specialFreeze[0] == 1) {
        for (int y = specialFreeze[2]; y < specialFreeze[2] + 32; y++) {
            for (int x = specialFreeze[1]; x < specialFreeze[1] + 32; x++) {
                bufferArray[x][y] = 0x0a0a;
            }
        }
    }


    if (specialSpeed[0] == 1) {
        for (int y = specialSpeed[2]; y < specialSpeed[2] + 32; y++) {
            for (int x = specialSpeed[1]; x < specialSpeed[1] + 32; x++) {
                bufferArray[x][y] = 0xfbb;
            }
        }
    }
}

void resetGame() {                                    // Function to restart game
    state = 0;
    lives = 4;
    cTime = 551;
    cMoves = 207;
    s.fPos[0] = fmt(18);
    s.fPos[1] = fmt(19);
    cTime = 555;
}


int powerUpTime = 0;                                // Variable used for power up functionality.

int main() {                                            // Main function initialises game state, creates all 3 threads, and draws the menu/game over screens.
    s.fPos[0] = fmt(18);
    s.fPos[1] = fmt(19);

    srand(time(0));
    int carRow[14][11] = {
            {fmt(18), 7, fmt(1), 1,  fmt(0), fmt(18), fmt(
                    25),                                       0,       0, 0, 0},                        // Yposition, Array size, width, direction
            {fmt(16), 7, fmt(1), 2,  fmt(6), fmt(12), fmt(19), fmt(28), 0, 0, 0},
            {fmt(17), 8, fmt(1), -2, fmt(1), fmt(5),  fmt(15), fmt(25), 0, 0, 0},
            {fmt(15), 8, fmt(1), -1, fmt(1), fmt(10), fmt(30), 0,       0, 0, 0},

            {fmt(12), 7, fmt(1), -2, 6 * 32, 12 * 32, 19 * 32, 28 * 32, 0, 0, 0},
            {fmt(13), 8, fmt(1), +2, 1 * 32, fmt(5),  fmt(15), fmt(25), 0, 0, 0},
            {fmt(11), 8, fmt(1), +1, 1 * 32, fmt(10), fmt(30), 0,       0, 0, 0},

            {fmt(8),  8, fmt(1), -2, 6 * 32, fmt(9),  19 * 32, 25 * 32, 0, 0, 0},
            {fmt(7),  8, fmt(1), +2, 1 * 32, fmt(5),  fmt(15), fmt(25), 0, 0, 0},
            {fmt(6),  8, fmt(1), +1, 1 * 32, fmt(10), fmt(30), 0,       0, 0, 0},

            {fmt(4),  8, fmt(1), -3, 6 * 32, fmt(9),  fmt(19), fmt(25), 0, 0, 0},
            {fmt(2),  8, fmt(1), +3, 1 * 32, fmt(5),  fmt(15), fmt(25), 0, 0, 0},
            {fmt(3),  8, fmt(1), +4, 1 * 32, fmt(10), fmt(30), 0,       0, 0, 0}
    };

    s.cR1 = carRow;

    gpio = getGPIOPtr();

    pthread_t controllerThread;                // Initialising an id for controllerThread
    pthread_t visualThread;
    pthread_t backgroundThread;
    pthread_attr_t attr;                    // Attribute initialisation
    pthread_attr_init(&attr);                // Setting attributes to default settings

    int gt = 0;
    int vt = 0;
    int bt = 0;

    gt = pthread_create(&controllerThread, &attr, controllerThreadFunc,
                        "Control");                        // Thread to handle controller input
    vt = pthread_create(&visualThread, &attr, visualThreadFunction,
                        "GUI");                                // Thread to create visual movement of objects (make cars move)
    bt = pthread_create(&backgroundThread, &attr, backgroundThreadFunction,
                        "BGR");                        // Thread to render images onto the screen

    framebufferstruct = initFbInfo();

    /* initialize a pixel */
    Pixel *pixel;
    pixel = malloc(sizeof(Pixel));

    while (1) {
        usleep(1666);
        if (cTime == 400) {
            specialFreeze[0] = 1;
            specialFreeze[1] = fmt(rand() % 32);
            specialFreeze[2] = fmt(rand() % 16) + fmt(2);
            for (int i = 1; i < 2; i++) {
                if (specialFreeze[i] % 32 != 0) {
                    int difference = specialFreeze[i] % 32;
                    //specialFreeze[i] += difference;
                }
            }
        }

        if (cTime == 300) {
            specialSpeed[0] = 1;
            specialSpeed[1] = fmt(rand() % 32);
            specialSpeed[2] = fmt(rand() % 16) + fmt(2);
            for (int i = 1; i < 2; i++) {
                if (specialSpeed[i] % 32 != 0) {
                    int difference = specialSpeed[i] % 32;
                    //specialSpeed[i] += difference;
                }
            }


        }

        if ((lives <= 0 || cTime <= 0) || cMoves <=
                                          0) {                                            // Displays game over if one of these conditions is true
            printf("Game Over!!\n");
            state = 6;
            short int *gameOverPtr = gameOver.pixel_data;
            int counter = 0;
            usleep(1000000);

            for (int y = 0; y < 720; y++) {
                for (int x = 0; x < 1280 * multiplier; x++) {
                    pixel->color = gameOverPtr[counter];
                    pixel->x = x;
                    pixel->y = y;
                    drawPixel(pixel);
                    counter++;
                }
            }
            usleep(3000000);
            resetGame();
        }
        if (s.fPos[1] <=
            fmt(0)) {                                                                // Displays congratulatory message if user wins.
            printf("You WON!\n");
            state = 6;
            short int *winIMG = YouWin.pixel_data;
            int counter = 0;
            usleep(1000000);

            for (int y = 0; y < 720; y++) {
                for (int x = 0; x < 1280 * multiplier; x++) {
                    pixel->color = winIMG[counter];
                    pixel->x = x;
                    pixel->y = y;
                    drawPixel(pixel);
                    counter++;
                }
            }
            usleep(3000000);
            resetGame();
        }

        if (state == 2) {
            timeDecrementer -= 1;
            if (timeDecrementer <= 0) {
                timeDecrementer = 100;
                cTime -= 1;
            }

        }

        while (state == 1 || state == 0) {                            // State 0 and 1 display the main menu
            drawMenu(pixel);
        }

        while (state == 3 || state == 4) {                            // State 3 and 4 display pause menu.
            drawPause(pixel);
        }


    }

    pthread_join(controllerThread, NULL);
    pthread_join(visualThread, NULL);
    pthread_join(backgroundThread, NULL);

    return 0;
}


void *controllerThreadFunc(
        void *voidPointer) {                    // This thread handles controller input differently based on state.
    int end = 1;

    Init_GPIO();

    for (int i = 0; i < 16; i++) {
        buttons[i] = 1;
    }

    int j = 0;
    while (end) {
        Read_SNES();
        Wait(100000);
        for (int i = 0; i < 16; i++) {
            if (buttons[i] == 0) {

                if ((i == 3) && (state == 2)) {
                    state = 3;
                    j = 1;
                }
                if ((i == 3 && j == 0) && (state == 3 || state == 4)) {
                    state = 2;
                }
                j = 0;

                // 5 up, 6 down, 7 left, 8 right
                if (i == 4) {
                    if (state == 1) {
                        state = 0;
                    }
                    if (state == 4) {
                        state = 3;
                    }
                    if (state == 2) {
                        if (s.fPos[1] >= 32) {
                            s.fPos[1] -= 32;
                            cMoves -= 1;
                        }
                    }
                }

                if (i == 5) {
                    if (state == 0) {
                        state = 1;
                    }
                    if (state == 3) {
                        state = 4;
                    }
                    if (state == 2 && s.fPos[1] <= 624) {
                        s.fPos[1] += 32;
                        cMoves -= 1;
                    }
                }
                if (i == 6) {
                    if (s.fPos[0] >= 32) {
                        s.fPos[0] -= 32;
                        cMoves -= 1;
                    }
                }
                if (i == 7 && s.fPos[0] < 1248) {
                    s.fPos[0] += 32;
                    cMoves -= 1;
                }

                if (i == 8 && state == 0) {
                    state = 2;
                }
                if (i == 8 && state == 1) {
                    state = 5;
                    wait(10000);
                    system("clear");
                    exit(0);
                }
                if (i == 8 && state == 4) {
                    state = 0;
                }
                buttons[i] = 1;

            }

        }
    }
    pthread_exit(NULL);

}

void *visualThreadFunction(
        void *voidPointer) {                            // This thread controls the movement of the game objects (moving cars), and detects colisions.
    while (1) {
        usleep(1666 * 40);
        int frogX = s.fPos[0];
        int frogY = s.fPos[1];
        int yCoord;
        if (powerUpTime == 0) {
            speedMultiplier = 1;
        }
        if (powerUpTime > 0) {
            powerUpTime -= 1;
        }
        if (specialFreeze[0] == 1) {
            if (frogHit(specialFreeze[1], specialFreeze[2])) {
                specialFreeze[0] = 0;
                usleep(4000000);
            }
        }
        if (specialSpeed[0] == 1) {
            if (frogHit(specialSpeed[1], specialSpeed[2])) {
                specialSpeed[0] = 0;
                speedMultiplier = -3000;
                powerUpTime = 1000;
                printf("Colide");
            }
        } else speedMultiplier = 1;
        for (int i = 0; i < carRows; i++) {
            yCoord = s.cR1[cc(i, 0)];
            for (int j = start; j < s.cR1[cc(i, arraySize)]; j++) {
                int cSpeed = s.cR1[cc(i, speed)];
                cSpeed *= speedMultiplier;
                s.cR1[cc(i, j)] += cSpeed % screenX;
                int cWidth = s.cR1[cc(i, width)];
                int xCoord = s.cR1[cc(i, j)];
                if (xCoord <= -cWidth) s.cR1[cc(i, j)] = screenX - 1;
                if (xCoord >= screenX + cWidth) s.cR1[cc(i, j)] = 0;

                if ((frogX >= xCoord && frogX <= (xCoord + cWidth)) && (yCoord >= frogY && yCoord <= (frogY + 31))) {
                    s.fPos[0] = fmt(15);
                    s.fPos[1] = fmt(19);
                    lives -= 1;
                }
            }
        }
    }
    pthread_exit(NULL);
}

void *backgroundThreadFunction(
        void *voidPointer) {                        // This thread handles the rendering of images on the screen.
    /* initialize + get FBS */
    framebufferstruct = initFbInfo();

    /* initialize a pixel */
    Pixel *pixel;
    pixel = malloc(sizeof(Pixel));
    while (1) {

        // Draw Background
        while (state == 2) {
            usleep(1666);
            drawBackground();
            drawFrog();
            drawSpecial();
            drawCars();
            drawHUD();
            for (int y = 0; y < screenY * multiplier; y++) {
                for (int x = 0; x < screenX * multiplier; x++) {
                    pixel->color = bufferArray[x][y]; // Load pre rendered image
                    pixel->x = x;
                    pixel->y = y;

                    drawPixel(pixel);
                }
            }
        }
    }
    pthread_exit(NULL);
}

void
drawPixel(Pixel *pixel) {                                            // This code was obtained from d2l to draw pixels.
    long int location = (pixel->x + framebufferstruct.xOff) * (framebufferstruct.bits / 8) +
                        (pixel->y + framebufferstruct.yOff) * framebufferstruct.lineLength;
    *((unsigned short int *) (framebufferstruct.fptr + location)) = pixel->color;
}

