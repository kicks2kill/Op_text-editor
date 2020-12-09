#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

//  // DEFINES

/*  CTRL_KEY macro bitwise-AND a character with value 00011111.
    In other words, it sets the 3 upper bits of the character to 0.
    This mirrors what the ctrl-key does in the terminal: it strips 5 and 6 from whatever key you press in combination with CTRL, and sends that.
*/

#define OP_VERSION "0.0.1"
#define CTRL_KEY(k) ((k) & 0x1f) 


//  // DATA
struct editorConfig {
    int cx, cy;
    int screenrows;
    int screencols;
    struct termios orig_termios;
};
struct editorConfig E;

/* we need to have a representation for arrow keys that does not conflict with wasd
    We will need to give them a larger int value that is out of range of a char and change all to type int that store keypresses. */
enum editorKey {
    ARROW_LEFT = 1000,
    ARROW_RIGHT,
    ARROW_UP,
    ARROW_DOWN,
};

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
    raw.c_cflag |= ( CS8 );
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
int editorReadKey() 
{
    int nread;
    char c;
    while ((nread == read(STDIN_FILENO, &c, 1)) != 1)
    {
        if(nread == -1 && errno != EAGAIN) die("read");
    }
    /* We need to imeediately read two more bytes into the seq buffer if we read an escape character.
        We make the seq buffer 3 bytes long because we will be handling longer escape sequences soon.
        We aliased the arrow keys to wasd keys. */
    if(c == '\x1b'){
        char seq[3];

        if(read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
        if(read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';

        if(seq[0] == '[') {
            switch(seq[1]){
                case 'A' : return ARROW_UP;
                case 'B' : return ARROW_DOWN;
                case 'C' : return ARROW_RIGHT;
                case 'D' : return ARROW_LEFT;
            }
        }
        return '\x1b';
    } else {
        return c;
    }
}


int getCursorPosition(int *rows, int *cols)
{
    char buf[32];
    unsigned int i = 0;

    /* when we print out to buffer, we don't want to print '\x1b' char, because the terminal would interpret it as an escape sequence.
       So we skip the first char in buf by passing &buf[1] to printf. Since it expects strings to end with a 0 byte, we assign '\0' to final byte of buf 
     */
    if(write( STDOUT_FILENO, "\x1b[6n", 4) != 4) return -1;

    while( i < sizeof(buf) - 1) {
        if( read( STDIN_FILENO, &buf[i], 1) != 1) break;
        if( buf[i] == 'R') break;
        i++;
    }

    /**
     * Since we made sure it responds with an esacpe sequence, we can pass a pointer to the 3rd char of buf to sscanf() (skipping 'x1b' and '['
     * So we are passing a string of the form 24;80 to sscanf() ).
     */ 
    buf[i] = "\0";
    if( buf[0] != '\x1b' || buf[1] != '[' ) return -1;
    if( sscanf(&buf[2], "%d;%d", rows, cols) != 2) return -1;

    return 0;

}

/* ioctl() will place the number of columns wide and number of rows high the terminal is into the given winsize struct. */
int getWindowSize(int *rows, int *cols)
{
    struct winsize ws;

    if(ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
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

struct _buf {
    char *b;
    int len;
};

/* to append a string s to _buf, the first thing we do is make sure we allocate enough memory.
    We ask realloc to give us a block of memory that is the size of cuirrent string plus what we are appending
    (which may use free() to get rid of current block of memory and reallocate)
    then we use memcpy to copy string s after the end of the current data in the buffer, then update the pointer and length of _buf to new values. */

void bufAppend( struct _buf *b, const char *s, int len)
{
    char *new = realloc(b->b, b->len + len);
    
    if(new == NULL) return;
    memcpy(&new[b->len], s, len);
    b->b = new;
    b->len += len;
}

void bufFree(struct _buf *b) {
    free(b->b);
}

#define BUF_INIT {NULL, 0}


//  //  INPUT
void editorProcessKeypress()
{
    int c = editorReadKey();

    switch(c) {
        case CTRL_KEY('q'):
            write(STDOUT_FILENO, "\x1b[2J", 4);
            write(STDOUT_FILENO, "\x1b[H",3);
            exit(0);
            break;

        case ARROW_UP:
        case ARROW_DOWN:
        case ARROW_LEFT:
        case ARROW_RIGHT:
            editorMoveCursor(c);
            break;
    }
}

void editorMoveCursor(int key)
{
    switch(key){
        case ARROW_LEFT:
            E.cx--;
            break;
        case ARROW_RIGHT:
            E.cx++;
            break;
        case ARROW_UP:
            E.cy--;
            break;
        case ARROW_DOWN:
            E.cy++;
            break;
    }
}

//  // OUTPUT
void editorDrawRows(struct _buf *b)
{
    int y;
    for( y = 0; y < E.screenrows; y++){
        if( y == E.screenrows / 3){
            char welcome[80];
            int welcomelen = snprintf(welcome, sizeof(welcome),
                "Op Editor -- version %s", OP_VERSION);
            if( welcomelen > E.screencols) welcomelen = E.screencols;
            int padding = (E.screencols - welcomelen) / 2;
            /* We centered the string by dividing the screen width by 2 and then subtracting half of the strings length from that. */
            if(padding) {
                bufAppend(b, "~", 1);
                padding--;
            }
            while(padding--) bufAppend(b, " ", 1);
            bufAppend(b, welcome, welcomelen);
        }else {
            bufAppend(b, "~", 1);
        } 
        /* command K stands for Erase In line. It's argument ( [2J ) is analogous to the J command that we removed from editorRefreshScreen() */
        bufAppend(b,"\x1b[K", 3);
        if( y < E.screenrows - 1) {
            bufAppend( b, "\r\n", 2);
        }
    }
}

void editorRefreshScreen()
{
    /*  The 4 call means we are writing 4 bytes out to the terminal.
        the first byte \x1b is the escape character (or 27 in decimal)

        We are writing an escape sequence, which always start with the escape character followed by a [
            Escape sequences instruct the terminal to do various text formatting tasks.
        The command J (Erase In Display) to clear the screen.
        The command h stands for Set Mode
        the command l stands for Reset mode
        ?25 is for hiding/show the cursor (?)
    */
    struct _buf b = BUF_INIT;
    bufAppend(&b, "\x1b[?25l", 6);
    bufAppend(&b, "\x1b[H", 3);

    editorDrawRows(&b);

    char buf[32];
    /* add 1 to E.cy and E.cx to convert from 0-index values to 1-indexed values.  */
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", E.cy + 1, E.cx + 1);
    bufAppend(&b, buf, strlen(buf));

    bufAppend(&b, "\x1b[?25h", 6);

    write(STDOUT_FILENO, b.b, b.len);
    bufFree(&b);
}


//  //INIT

void initEditor()
{   /* cx and cy will respond to horizontal and vertical coordinates of the cursor, respectively */
    E.cx = 0;
    E.cy = 0;
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