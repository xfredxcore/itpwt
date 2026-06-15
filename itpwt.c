#define _XOPEN_SOURCE 600
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pty.h>
#include <sys/select.h>
#include <time.h>

#define WIDTH 800
#define HEIGHT 650
#define PAPER_Y 40
#define PAPER_W 500
#define PAPER_H 320
#define MAX_SPECKS 40

const char *rows_normal[] = {
    "1234567890-=",
    "qwertyuiop[]",
    "asdfghjkl;'` ",
    "zxcvbnm,./\\"
};

typedef struct {
    int x, y, w, h;
    char code;
    char label[16];
    int is_mod; 
    int flash;
} Key;

Key keys[120];
int key_count = 0;

char paper_lines[15][60];
int current_line = 0;
int current_col = 0;
int ink_level = 100; 
int paper_full = 0;
int last_was_newline = 0;

int hammer_active = 0;
int hammer_target_x = 0;
int hammer_target_y = 0;

char cmd_buffer[1024] = {0};
int cmd_len = 0;

typedef struct {
    int x, y;
    int size;
} PaperSpeck;

PaperSpeck specks[MAX_SPECKS];

typedef struct {
    const char *name;
    unsigned long bg;
    unsigned long fg;
    unsigned long paper_bg;
    unsigned long speck_color;
    int win95;
} Theme;

Theme themes[] = {
    {"CLASSIC", 0x000000, 0xFFFFFF, 0x000000, 0x222222, 0},
    {"INVERT",  0xFFFFFF, 0x000000, 0xFFFFFF, 0xE0E0E0, 0},
    {"RED",     0x8B0000, 0xFFFFFF, 0x8B0000, 0xA02020, 0},
    {"BLUE",    0x00008B, 0xFFFFFF, 0x00008B, 0x2020A0, 0},
    {"WIN95",   0x808080, 0x000000, 0xFFFFFF, 0xD0D0D0, 1}
};
int current_theme = 0;

void generate_dirt() {
    for (int i = 0; i < MAX_SPECKS; i++) {
        specks[i].x = 45 + (rand() % (PAPER_W - 10));
        specks[i].y = PAPER_Y + 5 + (rand() % (PAPER_H - 10));
        specks[i].size = 1 + (rand() % 2);
    }
}

void init_keyboard() {
    int start_y = 420;
    int k_w = 32;
    int k_h = 32;
    int spacing = 6;

    for (int r = 0; r < 4; r++) {
        int start_x = 40 + (r * 15); 
        int len = strlen(rows_normal[r]);
        for (int c = 0; c < len; c++) {
            Key k;
            k.x = start_x + c * (k_w + spacing);
            k.y = start_y + r * (k_h + spacing);
            k.w = k_w;
            k.h = k_h;
            k.code = rows_normal[r][c];
            k.label[0] = k.code;
            k.label[1] = '\0';
            k.is_mod = 0;
            k.flash = 0;
            keys[key_count++] = k;
        }
    }

    int panel_x = 560;
    Key k;
    
    k.x = panel_x + 100; k.y = start_y + 45; k.w = 90; k.h = 80; k.is_mod = 2; k.flash = 0; k.code = '\n'; strcpy(k.label, "RET / LF"); keys[key_count++] = k;
    k.x = panel_x; k.y = PAPER_Y + 40; k.w = 200; k.h = 45; k.is_mod = 3; k.flash = 0; strcpy(k.label, "FEED NEW PAPER"); keys[key_count++] = k;
    k.x = panel_x; k.y = PAPER_Y + 110; k.w = 200; k.h = 45; k.is_mod = 4; k.flash = 0; strcpy(k.label, "REFILL INK"); keys[key_count++] = k;
    k.x = panel_x; k.y = PAPER_Y + 180; k.w = 200; k.h = 45; k.is_mod = 5; k.flash = 0; strcpy(k.label, "THEME CYCLE"); keys[key_count++] = k;
}

void print_to_paper(char c) {
    if (paper_full) return;

    if (c == '\r') {
        current_col = 0;
        last_was_newline = 1;
        return;
    }

    if (c == '\n') {
        if (!last_was_newline) {
            current_col = 0;
        }
        current_line++;
        if (current_line >= 15) {
            paper_full = 1;
        }
        last_was_newline = 0;
        return;
    }

    last_was_newline = 0;

    if (c < 32) return;

    if (current_line < 15 && current_col < 58) {
        hammer_target_x = 60 + (current_col * 8);
        hammer_target_y = PAPER_Y + 40 + (current_line * 18);
        hammer_active = 1;

        if (ink_level > 0) {
            paper_lines[current_line][current_col++] = c;
            if (rand() % 6 == 0) ink_level--;
        } else {
            paper_lines[current_line][current_col++] = ' '; 
        }
        paper_lines[current_line][current_col] = '\0';
    }
}

void draw_win95_box(Display *dis, Window win, GC gc, int x, int y, int w, int h, int pressed) {
    XSetForeground(dis, gc, pressed ? 0x808080 : 0xFFFFFF);
    XDrawLine(dis, win, gc, x, y, x + w, y);
    XDrawLine(dis, win, gc, x, y, x, y + h);
    XSetForeground(dis, gc, pressed ? 0xFFFFFF : 0x404040);
    XDrawLine(dis, win, gc, x + w, y, x + w, y + h);
    XDrawLine(dis, win, gc, x, y + h, x + w, y + h);
}

void draw_ui(Display *dis, Window win, GC gc, int screen) {
    Theme t = themes[current_theme];
    XSetWindowBackground(dis, win, t.bg);
    XClearWindow(dis, win);

    if (t.win95) {
        XSetForeground(dis, gc, 0xC0C0C0);
        XFillRectangle(dis, win, gc, 0, 0, WIDTH, HEIGHT);
        XSetForeground(dis, gc, 0xFFFFFF);
        XFillRectangle(dis, win, gc, 40, PAPER_Y, PAPER_W, PAPER_H);
        draw_win95_box(dis, win, gc, 40, PAPER_Y, PAPER_W, PAPER_H, 1);
    } else {
        XSetForeground(dis, gc, t.fg);
        XDrawRectangle(dis, win, gc, 40, PAPER_Y, PAPER_W, PAPER_H);
    }
    
    XSetForeground(dis, gc, t.speck_color);
    for (int i = 0; i < MAX_SPECKS; i++) {
        XFillRectangle(dis, win, gc, specks[i].x, specks[i].y, specks[i].size, specks[i].size);
    }

    XSetForeground(dis, gc, t.win95 ? 0x000000 : t.fg);

    if (paper_full) {
        XDrawString(dis, win, gc, 60, PAPER_Y + 20, "!!! OUT OF PAPER !!! (CLICK 'FEED NEW PAPER')", 45);
    }

    for (int i = 0; i <= current_line && i < 15; i++) {
        XDrawString(dis, win, gc, 60, PAPER_Y + 40 + (i * 18), paper_lines[i], strlen(paper_lines[i]));
    }

    if (!paper_full && current_line < 15) {
        int cursor_x = 60 + (current_col * 8);
        int cursor_y = PAPER_Y + 40 + (current_line * 18);
        XDrawLine(dis, win, gc, cursor_x, cursor_y - 10, cursor_x, cursor_y + 2);
    }

    if (hammer_active) {
        XSetForeground(dis, gc, t.win95 ? 0x000000 : t.fg);
        XSetLineAttributes(dis, gc, 2, LineSolid, CapNotLast, JoinMiter);
        XDrawLine(dis, win, gc, 40 + (PAPER_W / 2), PAPER_Y + PAPER_H, hammer_target_x, hammer_target_y);
        XFillArc(dis, win, gc, hammer_target_x - 4, hammer_target_y - 8, 8, 8, 0, 360 * 64);
        XSetLineAttributes(dis, gc, 1, LineSolid, CapNotLast, JoinMiter);
        hammer_active = 0; 
    }

    char ink_str[32];
    sprintf(ink_str, "INK RESERVOIR: %d%%", ink_level);
    XSetForeground(dis, gc, t.win95 ? 0x000000 : t.fg);
    XDrawString(dis, win, gc, 560, PAPER_Y + 20, ink_str, strlen(ink_str));
    XDrawRectangle(dis, win, gc, 560, PAPER_Y + 25, 200, 10);
    XFillRectangle(dis, win, gc, 560, PAPER_Y + 25, ink_level * 2, 10);

    for (int i = 0; i < key_count; i++) {
        int pressed = keys[i].flash;
        
        if (t.win95) {
            XSetForeground(dis, gc, 0xC0C0C0);
            XFillRectangle(dis, win, gc, keys[i].x, keys[i].y, keys[i].w, keys[i].h);
            draw_win95_box(dis, win, gc, keys[i].x, keys[i].y, keys[i].w, keys[i].h, pressed);
            XSetForeground(dis, gc, 0x000000);
            XDrawString(dis, win, gc, keys[i].x + (pressed ? 9 : 8), keys[i].y + (pressed ? 23 : 22), keys[i].label, strlen(keys[i].label));
        } else {
            if (pressed) {
                XFillRectangle(dis, win, gc, keys[i].x, keys[i].y, keys[i].w, keys[i].h);
                XSetForeground(dis, gc, t.bg);
                XDrawString(dis, win, gc, keys[i].x + 8, keys[i].y + 22, keys[i].label, strlen(keys[i].label));
                XSetForeground(dis, gc, t.fg);
            } else {
                XDrawRectangle(dis, win, gc, keys[i].x, keys[i].y, keys[i].w, keys[i].h);
                XDrawString(dis, win, gc, keys[i].x + 8, keys[i].y + 22, keys[i].label, strlen(keys[i].label));
            }
        }
    }
    XFlush(dis);
}

void trigger_key_flash(Display *dis, Window win, GC gc, int screen, int index) {
    keys[index].flash = 1;
    draw_ui(dis, win, gc, screen);
    XFlush(dis);
    usleep(40000);
    keys[index].flash = 0;
}

int is_forbidden_command(const char *cmd) {
    if (strstr(cmd, "htop") != NULL ||
        strstr(cmd, "top") != NULL ||
        strstr(cmd, "cava") != NULL ||
        strstr(cmd, "nano") != NULL ||
        strstr(cmd, "vim") != NULL ||
        strstr(cmd, "vi") != NULL ||
        strstr(cmd, "less") != NULL ||
        strstr(cmd, "more") != NULL ||
        strstr(cmd, "man") != NULL) {
        return 1;
    }
    return 0;
}

int main() {
    srand(time(NULL));
    generate_dirt();

    for (int i = 0; i < 15; i++) paper_lines[i][0] = '\0';

    int pty_master;
    pid_t pid = forkpty(&pty_master, NULL, NULL, NULL);
    
    if (pid < 0) {
        perror("fuck:pty");
        return 1;
    }
    if (pid == 0) {
        char *env[] = { "TERM=dumb", NULL };
        execle("/bin/sh", "sh", NULL, env);
        exit(1);
    }

    Display *dis = XOpenDisplay(NULL);
    if (!dis) {
        fprintf(stderr, "fuck:display\n");
        return 1;
    }

    int screen = DefaultScreen(dis);
    Window win = XCreateSimpleWindow(dis, RootWindow(dis, screen), 100, 100, 
                                     WIDTH, HEIGHT, 0, 0, BlackPixel(dis, screen));
    
    XSelectInput(dis, win, ExposureMask | ButtonPressMask);
    XMapWindow(dis, win);
    XStoreName(dis, win, "itpwt");

    GC gc = XCreateGC(dis, win, 0, NULL);
    
    init_keyboard();
    int x11_fd = ConnectionNumber(dis);

    while (1) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(pty_master, &readfds);
        FD_SET(x11_fd, &readfds);

        int max_fd = (pty_master > x11_fd) ? pty_master : x11_fd;
        
        if (select(max_fd + 1, &readfds, NULL, NULL, NULL) < 0) break;

        if (FD_ISSET(pty_master, &readfds)) {
            char buf[4096];
            ssize_t n = read(pty_master, buf, sizeof(buf));
            if (n <= 0) break;
            for (ssize_t i = 0; i < n; i++) {
                print_to_paper(buf[i]);
            }
            draw_ui(dis, win, gc, screen);
        }

        while (XPending(dis)) {
            XEvent report;
            XNextEvent(dis, &report);

            if (report.type == Expose) {
                draw_ui(dis, win, gc, screen);
            }

            if (report.type == ButtonPress && report.xbutton.button == 1) {
                int mx = report.xbutton.x;
                int my = report.xbutton.y;

                for (int i = 0; i < key_count; i++) {
                    if (mx >= keys[i].x && mx <= keys[i].x + keys[i].w &&
                        my >= keys[i].y && my <= keys[i].y + keys[i].h) {
                        
                        trigger_key_flash(dis, win, gc, screen, i);

                        if (keys[i].is_mod == 3) {
                            for (int l = 0; l < 15; l++) paper_lines[l][0] = '\0';
                            current_line = 0;
                            current_col = 0;
                            paper_full = 0;
                            generate_dirt();
                        } 
                        else if (keys[i].is_mod == 4) {
                            ink_level = 100;
                        } 
                        else if (keys[i].is_mod == 5) {
                            current_theme = (current_theme + 1) % 5;
                        }
                        else {
                            char send_char = keys[i].code;

                            if (send_char == '\n' || send_char == '\r') {
                                cmd_buffer[cmd_len] = '\0';
                                if (is_forbidden_command(cmd_buffer)) {
                                    const char *warn_msg = "\n[itpwt: dynamic process forbidden]\n";
                                    for (int w = 0; warn_msg[w] != '\0'; w++) {
                                        print_to_paper(warn_msg[w]);
                                    }
                                    char cancel = 0x03;
                                    if (write(pty_master, &cancel, 1) < 0) {}
                                    cmd_len = 0;
                                    draw_ui(dis, win, gc, screen);
                                    break;
                                }
                                cmd_len = 0;
                            } else if (send_char >= 32 && cmd_len < (int)sizeof(cmd_buffer) - 1) {
                                cmd_buffer[cmd_len++] = send_char;
                            }

                            if (write(pty_master, &send_char, 1) < 0) {}
                        }

                        draw_ui(dis, win, gc, screen);
                        break;
                    }
                }
            }
        }
    }

    XFreeGC(dis, gc);
    XCloseDisplay(dis);
    return 0;
}