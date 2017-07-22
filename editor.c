// Instructions for base project found at http://viewsourcecode.org/snaptoken/kilo/

#include <termios.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <string.h>
#include <sys/types.h>

#define CTRL_KEY(k) ((k) & 0x1f)
#define EDITOR_VERSION "0.0.1"

enum editorKey {
    ARROW_LEFT = 1000,
    ARROW_RIGHT,
    ARROW_UP,
    ARROW_DOWN,
    PAGE_UP, // shift + fn + upkey
    PAGE_DOWN, // shift + fn + downkey
    HOME_KEY,
    END_KEY,
    DEL_KEY,
};

// Row of text in editor
struct erow {
    int size;
    char* chars;
};

struct editorConfig {
    struct termios orig_termios;
    int screenrows;
    int screencols;
    int cx;
    int cy;
    int numrows;
    struct erow* row; // Array of struct erow
    int rowoff; // Row offset user is current scrolled to
    int coloff;
} E;

void die(const char *s) {
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);

    perror(s);
    exit(1);
}

void disableRawMode() {
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1)
        die("tcsetattr");
}

// Send keypresses to the program as they run, rather than when user hits enter
void enableRawMode() {
    if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1)
        die("tcgetattr");
    atexit(disableRawMode);

    struct termios raw = E.orig_termios;
    // Part of the tradition of enabling raw mode
    raw.c_cflag |= (CS8);
    // Turn off all output processing
    raw.c_oflag &= ~(OPOST);
    // turn off ctrl-s, ctrl-q; ctrl-m carriage returns to newlines
    raw.c_iflag &= ~(IXON | ICRNL | ISTRIP | INPCK | BRKINT);
    // No echo; read byte by byte, not line by line; turn off signals; turn off ctrl-o discard
    raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN); 
    // Set a timeout for read operation
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw))
        die("tcsetattr");
}

int editorReadKey() {
    int nread;
    char c;
    while((nread = read(STDIN_FILENO, &c, 1)) != 1) { // While the number of bytes we have read is not one, keep reading
        if (nread == -1 && errno != EAGAIN) die("read");
    }

    // Handle multi byte sequences
    if (c == '\x1b') {
        char seq[3];
        if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
        if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';

        if (seq[0] == '[') {
            if (seq[1] >= '0' && seq[1] <= '9') {
                if (read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b';
                if(seq[2] == '~') {
                    switch (seq[1]) {
                    case '1': return HOME_KEY;
                    case '3': return DEL_KEY;
                    case '4': return END_KEY;
                    case '5': return PAGE_UP;
                    case '6': return PAGE_DOWN;
                    case '7': return HOME_KEY;
                    case '8': return END_KEY;
                    }
                }
            } else {
                switch (seq[1]) {
                case 'A': return ARROW_UP;
                case 'B': return ARROW_DOWN;
                case 'C': return ARROW_RIGHT;
                case 'D': return ARROW_LEFT;
                case 'H': return HOME_KEY;
                case 'F': return END_KEY;
                }
            }
        } else if (seq[0] == 'O') {
            switch (seq[1]) {
            case 'H': return HOME_KEY;
            case 'F': return END_KEY;
            }
        }

        return '\x1b';
    }

    return c;
}

int getWindowSize(int* rows, int* cols) {
    struct winsize ws;

    if(ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
        return -1;
    } else {
        *cols = ws.ws_col;
        *rows = ws.ws_row;
        return 0;
    }
}

void editorAppendRow(char* s, size_t len) {
    E.row = realloc(E.row, sizeof(struct erow) * (E.numrows + 1));

    int at = E.numrows;
    E.row[at].size = len;
    E.row[at].chars = malloc(len + 1);
    memcpy(E.row[at].chars, s, len);
    E.row[at].chars[len] = '\0';
    E.numrows ++;
}

void editorOpen(char* filename) {
    FILE *fp = fopen(filename, "r");
    if (!fp) die("fopen");

    char* line = NULL;
    size_t linecap = 0;
    ssize_t linelen;
    while ((linelen = getline(&line, &linecap, fp)) != -1) {
         while (linelen > 0 && (line[linelen - 1] == '\n' || line[linelen - 1] == '\r'))
            linelen --;

        editorAppendRow(line, linelen);
    }
   
    free(line);
    fclose(fp);
}

void editorMoveCursor(int key) {
    switch (key) {
    case ARROW_LEFT:
        if (E.cx != 0) {
            E.cx--;
        }
        break;
    case ARROW_RIGHT:
            E.cx ++;
        break;
    case ARROW_UP:
        if (E.cy != 0) {
            E.cy --;
        }
        break;
    case ARROW_DOWN:
        if (E.cy < E.numrows) {
            E.cy ++;
        }
        break;
    }
}

void editorProcessKeypress() {
    int c = editorReadKey();

    switch(c) {
    case CTRL_KEY('q'):
        write(STDOUT_FILENO, "\x1b[2J", 4);
        write(STDOUT_FILENO, "\x1b[H", 3);
        exit(0);
        break;

    case ARROW_UP:
    case ARROW_DOWN:
    case ARROW_LEFT:
    case ARROW_RIGHT:
        editorMoveCursor(c);
        break;

    case PAGE_UP:
    case PAGE_DOWN:
    {
        int times = E.screenrows;
        while (times --) {
            editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
        }
        break;
    }

    case HOME_KEY:
        E.cx = 0;
        break;
    case END_KEY:
        E.cx = E.screencols - 1;
        break;
    }
}

/* Append buffer - don't make tons of small writes or screen may flicker */
struct abuf {
    char* b;
    int len;
};
#define ABUF_INIT {NULL, 0}

void abAppend(struct abuf* ab, const char* s, int len) {
    char* new = realloc(ab->b, ab->len + len);

    if (new == NULL) return;
    memcpy(&new[ab->len], s, len);
    ab->b = new;
    ab->len += len;
}

void abFree(struct abuf* ab) {
    free(ab->b);
}

void editorDrawRows(struct abuf* ab) {
    for (int y = 0; y < E.screenrows; ++y) {
        int filerow = y + E.rowoff;
        if (filerow >= E.numrows) {
            if (E.numrows == 0 && y == E.screenrows / 3) { // 1/3 of the way down the screen
                char welcome[80];
                int welcomelen = snprintf(
                    welcome, 
                    sizeof(welcome), 
                    "TextEditor -- version %s",
                    EDITOR_VERSION);
                if (welcomelen > E.screencols) welcomelen = E.screencols;

                // Center it
                int padding = (E.screencols - welcomelen) / 2;
                if (padding) {
                    abAppend(ab, "~", 1);
                    padding --;
                }
                while (padding --) abAppend(ab, " ", 1);
                abAppend(ab, welcome, welcomelen);
            } else {
                abAppend(ab, "~", 1);
            }
        } else {
            int len = E.row[filerow].size - E.coloff;
            if (len < 0) len = 0;
            if (len > E.screencols) len = E.screencols;
            abAppend(ab, &E.row[filerow].chars[E.coloff], len);
        }
        
        abAppend(ab, "\x1b[K", 3); // Clear this line to right of cursor
        if (y != E.screenrows - 1) {
            abAppend(ab, "\r\n", 2);
        }
    }
}

void editorScroll() {
    if (E.cy < E.rowoff) {
        E.rowoff = E.cy;
    }
    if (E.cy >= E.rowoff + E.screenrows) {
        E.rowoff = E.cy - E.screenrows + 1;
    }
    if (E.cx < E.coloff) {
        E.coloff = E.cx;
    }
    if (E.cx >= E.coloff + E.screencols) {
        E.coloff = E.cx - E.screencols + 1;
    }
}

void editorRefreshScreen() {
    editorScroll();

    struct abuf ab = ABUF_INIT;

    abAppend(&ab, "\x1b[?25l", 6); // Hide cursor
    abAppend(&ab, "\x1b[H", 3);

    editorDrawRows(&ab);

    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (E.cy - E.rowoff) + 1, (E.cx - E.coloff) + 1);
    abAppend(&ab, buf, strlen(buf));

    abAppend(&ab, "\x1b[?25h", 6); // Show cursor

    write(STDOUT_FILENO, ab.b, ab.len);
    abFree(&ab);
}

void initEditor() {
    E.cx = 0;
    E.numrows = 0;
    E.row = NULL;
    E.rowoff = 0;
    E.coloff = 0;
    E.cy = 0;
    if(getWindowSize(&E.screenrows, &E.screencols) == -1) {
        die("getWindowSize");
    }
}

int main(int argc, char* argv[]) {
    enableRawMode();
    initEditor();
    if (argc >= 2) {
        editorOpen(argv[1]);
    }

    while(1) {
        editorRefreshScreen();
        editorProcessKeypress();
    }
    return 0;
}
