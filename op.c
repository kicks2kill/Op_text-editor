#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

//  // DEFINES

/*  CTRL_KEY macro bitwise-AND a character with value 00011111.
    In other words, it sets the 3 upper bits of the character to 0.
    This mirrors what the ctrl-key does in the terminal: it strips 5 and 6 from whatever key you press in combination with CTRL, and sends that.
*/
#define CTRL_KEY(k) ((k) & 0x1f) 

//  // DATA
struct editorConfig {
    int screenrows;
    int screencols;
    struct termios orig_termios;
};
struct editorConfig E;

//  // TERMINAL
void die( const char *s)
{
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H",3);

    perror(s);
    exit(1);
}

void disableRawMode()
{
    if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1)
        die("tcsetattr");
}

void enableRawMode()
{
    if(tcgetattr(STDIN_FILENO, &E.orig_termios) == -1) die("tcgetattr");
    atexit(disableRawMode);

    struct termios raw = E.orig_termios;

    /* Turn off all commands for now. Flags from termios, refer to them on info about flags */
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~( OPOST );
    raw.c_cflag |= (CS8);
    raw.c_lflag &= ~( ECHO | ICANON | IEXTEN | ISIG );
    /* VMIN and VTIME come from termios.h. They are indexes into the C_CC field, which are control characters.
        An array of bytes that control various terminal settings.
        VMIN value sets min number of bytes of input needed before read() can return. We set to 0 so it returns immediately.
        VTIME value sets the max amount of time to wait before read() returns. It is in tenths of a second, so we have 1/10 of a second.
     */
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;

    if( tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) die("tcsetattr");   
}

/* wait for one keypress and return it. */
char editorReadKey() 
{
    int nread;
    char c;
    while ((nread == read(STDIN_FILENO, &c, 1)) != 1)
    {
        if(nread == -1 && errno != EAGAIN) die("read");
    }
    return c;
}


int getCursorPosition(int *rows, int *cols)
{
    if(write( STDOUT_FILENO, "\x1b[6n", 4) != 4) return -1;

    printf("\r\n");
    char c;
    while( read( STDIN_FILENO, &c, 1) == 1) {
        if(iscntrl( c )){
            printf("%d\r\n", c);
        } else {
            printf("%d ('%c')\r\n", c, c);
        }
    }
    editorReadKey();
    return -1;
}

/* ioctl() will place the number of columns wide and number of rows high the terminal is into the given winsize struct. */
int getWindowSize(int *rows, int *cols)
{
    struct winsize ws;

    if(1 || ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
        if( write( STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) return -1;
            return getCursorPosition( rows, cols);
        }
    else
    {
        *cols = ws.ws_col;
        *rows = ws.ws_row;
        return 0;
    }   
}


//  //  INPUT
void editorProcessKeypress()
{
    char c = editorReadKey();

    switch(c) {
        case CTRL_KEY('q'):
            write(STDOUT_FILENO, "\x1b[2J", 4);
            write(STDOUT_FILENO, "\x1b[H",3);
            exit(0);
            break;
    }
}

//  // OUTPUT
void editorDrawRows()
{
    int y;
    for( y = 0; y < E.screenrows; y++){
        write(STDOUT_FILENO,"~\r\n", 3);
    }
}

void editorRefreshScreen()
{
    /*  The 4 call means we are writing 4 bytes out to the terminal.
        the first byte \x1b is the escape character (or 27 in decimal)

        We are writing an escape sequence, which always start with the escape character followed by a [
            Escape sequences instruct the terminal to do various text formatting tasks.
        The command J (Erase In Display) to clear the screen.
    */
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H",3);

    editorDrawRows();
    write(STDOUT_FILENO, "\x1b[H", 3);
}


//  //INIT

void initEditor()
{
    if(getWindowSize(&E.screenrows, &E.screencols) == -1) die("getWindowSize");
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
    initEditor();
    while(1)
    {
        editorRefreshScreen();
        editorProcessKeypress(); // waits for a keypress, and handles it.
    }
    return 0;
}