#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>


struct termios orig_termios;
void disableRawMode()
{
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
}

void enableRawMode()
{
    tcgetattr(STDIN_FILENO, &orig_termios);
    atexit(disableRawMode);

    struct termios raw = orig_termios;
    raw.c_lflag &= ~( ECHO | ICANON ); /* ICANON comes from termios. It is a local flag in the c_lflag field.*/

    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);   
}

int main()
{
    /**
     * By default, the terminal starts in canonical mode. Keyboard input is sent to programm when 'enter' is pressed.
     * 
     * Added 'Raw' mode.
     * 
     * The ECHO feature causes each key you type to be printed to the terminal
     * but can be distracting when rendering a UI in raw mode, so it is turned off.
     * 
     * Terminal attributes can be read into a termios struct by tcgetattr(), and applied with tcsetattr().
     * The TCSAFLUSH arg specifies when to apply the change.
     * c_lflag is for local flags && ECHO is a bitflag
     * 
     * 
    */
    enableRawMode();
    char c;
    while (read( STDIN_FILENO, &c, 1) == 1 && c != 'q') /* Read 1 byte from standard input into variable c until none are left to read. */
    {
        if( iscntrl( c )) {
            printf("%d\n", c);
        } else {
            printf("%d ('%c')\n", c, c);
        }
    }
    return 0;
}