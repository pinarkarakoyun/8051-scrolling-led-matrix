#include <reg51.h>

// Hardware Pin Definitions
sbit DS    = P1^0;  // Serial Data Input for the first 74HC595
sbit SH_CP = P1^1;  // Shift Register Clock Input (shared across all chips)
sbit ST_CP = P1^2;  // Storage Register Clock Input / Latch (shared)

// Control Flags and Variables
bit isPaused = 0;               // Flag to pause the scrolling text
unsigned char speedMode = 1;    // Variable to hold the current speed level (1, 2, or 3)
unsigned char scrollLimit = 20; // This controls how many timer ticks before shifting
unsigned char scrollDelay = 0;  // Counter for the scrolling delay
unsigned int  charIndex = 0;    // Tracks which character of the message we are on
unsigned char colIndex = 0;     // Tracks which column of the current character is being drawn

// Display Buffer
// 4 dot matrix displays * 8 columns = 32 bytes to hold the current screen data
unsigned char displayBuffer[32]; 

//long message 
unsigned char code message[] = " WELCOME TO ANTALYA BILIM UNIVERSITY SPRING 2026 ";

// MODULE 1: Character Lookup Table
// I created a simplified font array to save memory. 
// Each row represents the hex values for an 8x8 character.
unsigned char code font[][8] = {
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // Space (0)
    {0x7E,0x11,0x11,0x11,0x7E,0x00,0x00,0x00}, // A (1)
    {0x7F,0x49,0x49,0x49,0x36,0x00,0x00,0x00}, // B (2)
    {0x3E,0x41,0x41,0x41,0x22,0x00,0x00,0x00}, // C (3)
    {0x7F,0x49,0x49,0x49,0x41,0x00,0x00,0x00}, // E (4)
    {0x7F,0x09,0x09,0x09,0x01,0x00,0x00,0x00}, // F (5)
    {0x7F,0x08,0x08,0x08,0x7F,0x00,0x00,0x00}, // H (6)
    {0x00,0x41,0x7F,0x41,0x00,0x00,0x00,0x00}, // I (7)
    {0x7F,0x04,0x0A,0x11,0x60,0x00,0x00,0x00}, // K (8)
    {0x7F,0x40,0x40,0x40,0x40,0x00,0x00,0x00}, // L (9)
    {0x7F,0x02,0x0C,0x02,0x7F,0x00,0x00,0x00}, // M (10)
    {0x7F,0x04,0x08,0x10,0x7F,0x00,0x00,0x00}, // N (11)
    {0x3E,0x41,0x41,0x41,0x3E,0x00,0x00,0x00}, // O (12)
    {0x7F,0x09,0x09,0x09,0x06,0x00,0x00,0x00}, // P (13)
    {0x7F,0x09,0x19,0x29,0x46,0x00,0x00,0x00}, // R (14)
    {0x46,0x49,0x49,0x49,0x31,0x00,0x00,0x00}, // S (15)
    {0x01,0x01,0x7F,0x01,0x01,0x00,0x00,0x00}, // T (16)
    {0x3F,0x40,0x40,0x40,0x3F,0x00,0x00,0x00}, // U (17)
    {0x1F,0x20,0x40,0x20,0x1F,0x00,0x00,0x00}, // V (18)
    {0x7F,0x20,0x18,0x20,0x7F,0x00,0x00,0x00}, // W (19)
    {0x63,0x14,0x08,0x14,0x63,0x00,0x00,0x00}, // X (20)
    {0x03,0x04,0x78,0x04,0x03,0x00,0x00,0x00}, // Y (21)
    {0x61,0x51,0x49,0x45,0x43,0x00,0x00,0x00}, // Z (22)
    {0x21,0x41,0x45,0x4B,0x31,0x00,0x00,0x00}, // 2 (23)
    {0x3E,0x49,0x49,0x49,0x32,0x00,0x00,0x00}, // 6 (24)
    {0x3E,0x51,0x49,0x45,0x3E,0x00,0x00,0x00}  // 0 (25)
};

// Helper function to map an ASCII character to my font array index
unsigned char getCharIndex(unsigned char c) {
    if(c == ' ') return 0;
    if(c == 'A') return 1;  if(c == 'B') return 2;  if(c == 'C') return 3;
    if(c == 'E') return 4;  if(c == 'F') return 5;  if(c == 'H') return 6;
    if(c == 'I') return 7;  if(c == 'K') return 8;  if(c == 'L') return 9;
    if(c == 'M') return 10; if(c == 'N') return 11; if(c == 'O') return 12;
    if(c == 'P') return 13; if(c == 'R') return 14; if(c == 'S') return 15;
    if(c == 'T') return 16; if(c == 'U') return 17; if(c == 'V') return 18;
    if(c == 'W') return 19; if(c == 'X') return 20; if(c == 'Y') return 21;
    if(c == 'Z') return 22; if(c == '2') return 23; if(c == '6') return 24;
    if(c == '0') return 25;
    return 0; // Return space if character is not found
}

// MODULE 2: Display Driver
// Shifts 8 bits of data into the 74HC595 registers
void sendTo595(unsigned char dat) {
    unsigned char i;
    for(i = 0; i < 8; i++) {
        DS = (dat & 0x80) ? 1 : 0; // Send MSB first
        dat <<= 1;
        // Generate a clock pulse to shift the data in
        SH_CP = 1;
        SH_CP = 0;
    }
}

// Timer 0 Interrupt: Handles row multiplexing (scanning the display)
void timer0_ISR() interrupt 1 {
    static unsigned char row = 0;
    unsigned char i;
    
    // Reload values for ~1ms delay to prevent flickering
    TH0 = 0xFC; 
    TL0 = 0x18;
    
    P2 = 0x00; // Turn off all rows to prevent ghosting
    
    // Push 32 bytes (4 matrices * 8 columns) to the shift registers
    for(i = 0; i < 4; i++) {
        sendTo595(~displayBuffer[(3-i)*8 + row]); // Invert data because of active-low LEDs
    }
    
    // Latch the shifted data to the output pins
    ST_CP = 1; 
    ST_CP = 0;
    
    P2 = (1 << row); // Turn on the current row
    
    row++;
    if(row >= 8) row = 0; // Reset row counter after 8 rows
}

// MODULE 3: Scrolling Control
// Timer 1 Interrupt: Handles the leftward shifting of the message
void timer1_ISR() interrupt 3 {
    unsigned char i, fIndex;
    
    // Reload values for ~50ms base interval
    TH1 = 0x3C; 
    TL1 = 0xB0;
    
    if(isPaused) return; // Skip updating the buffer if I pressed pause
    
    scrollDelay++;
    if(scrollDelay >= scrollLimit) {
        scrollDelay = 0;
        
        // Shift everything in the display buffer left by 1 position
        for(i = 31; i > 0; i--) {
            displayBuffer[i] = displayBuffer[i-1];
        }
        
        // If we reached the end of the string, start over
        if (message[charIndex] == '\0') {
            charIndex = 0; 
        }
        
        // Fetch the next column of the current character and put it on the far right
        fIndex = getCharIndex(message[charIndex]);
        displayBuffer[0] = font[fIndex][colIndex];
        
        colIndex++;
        if(colIndex >= 8) {
            colIndex = 0; // Move to the next character once all 8 columns are shifted
            charIndex++;
        }
    }
}

// MODULE 4: External Controls
// External Interrupt 0 (Button 1): Toggle Pause/Resume
void ex0_ISR() interrupt 0 {
    isPaused = ~isPaused; // Flip the state
}

// External Interrupt 1 (Button 2): Change the scrolling speed
void ex1_ISR() interrupt 2 {
    speedMode++;
    if(speedMode > 3) speedMode = 1; // Loop back to slowest speed
    
    // Adjust the scroll limit. Lower value = faster shifting
    if(speedMode == 1) scrollLimit = 20; // Slow speed
    if(speedMode == 2) scrollLimit = 10; // Medium speed
    if(speedMode == 3) scrollLimit = 3;  // Fast speed
}

// Main Function
void main() {
    unsigned char i;
    
    // Clear the display buffer initially so we don't get garbage characters
    for(i = 0; i < 32; i++) displayBuffer[i] = 0x00; 
    
    // Timer Configuration
    TMOD = 0x11; // Configure both Timer 0 and Timer 1 to Mode 1 (16-bit)
    
    // Interrupt Configuration
    IT0 = 1; // Make INT0 trigger on falling edge (when button is pressed down)
    IT1 = 1; // Make INT1 trigger on falling edge
    
    EX0 = 1; // Enable External Interrupt 0
    EX1 = 1; // Enable External Interrupt 1
    
    ET0 = 1; // Enable Timer 0 Interrupt
    ET1 = 1; // Enable Timer 1 Interrupt
    
    EA = 1;  // Enable global interrupts
    
    TR0 = 1; // Start Timer 0 (Multiplexing starts)
    TR1 = 1; // Start Timer 1 (Scrolling logic starts)
    
    // Everything runs in the background using interrupts!
    while(1) {
        // Infinite loop, waiting for timer and external interrupts
    }
}