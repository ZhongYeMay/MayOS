/* X11 backend for the hard-disk desktop. The live image still uses fbdev. */
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>

static Display *x_display;
static Window x_window;
static XImage *x_image;
static Atom x_close;
static unsigned int x_width, x_height;
static pid_t firefox_children[8];

static int keycode(unsigned int code, int value);

static void x11_close(void) {
    if (x_image) XDestroyImage(x_image);
    if (x_display) XCloseDisplay(x_display);
}

static int x11_surface(unsigned int width, unsigned int height) {
    if (width < 200 || height < 150 || width > 8192 || height > 4320) return 0;
    Visual *visual = DefaultVisual(x_display, DefaultScreen(x_display));
    if (visual->red_mask != 0xff0000 || visual->green_mask != 0xff00 || visual->blue_mask != 0xff)
        return 0;
    XImage *image = XCreateImage(x_display, visual, DefaultDepth(x_display, DefaultScreen(x_display)),
                                 ZPixmap, 0, NULL, width, height, 32, 0);
    if (!image) return 0;
    if (image->bits_per_pixel != 32 || image->byte_order != LSBFirst ||
        !(image->data = calloc(height, (size_t)image->bytes_per_line))) {
        XDestroyImage(image);
        return 0;
    }
    if (x_image) XDestroyImage(x_image);
    x_image = image;
    x_width = width;
    x_height = height;
    display = (MaySurface){.data = (u8 *)image->data, .width = width, .height = height,
        .pitch = image->bytes_per_line, .bpp = 32,
        .red = 16, .red_bits = 8, .green = 8, .green_bits = 8, .blue_bits = 8};
    return 1;
}

static int x11_open(void) {
    x_display = XOpenDisplay(NULL);
    if (!x_display) return 0;
    int screen = DefaultScreen(x_display);
    x_window = XCreateSimpleWindow(x_display, RootWindow(x_display, screen),
                                   0, 0, W, H, 0, 0, BlackPixel(x_display, screen));
    if (!x_window || !x11_surface(W, H)) return 0;
    XStoreName(x_display, x_window, "MayOS Desktop");
    x_close = XInternAtom(x_display, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(x_display, x_window, &x_close, 1);
    XSelectInput(x_display, x_window, ExposureMask | StructureNotifyMask | KeyPressMask |
                 PointerMotionMask | ButtonPressMask | ButtonReleaseMask | FocusChangeMask);
    Atom window_state = XInternAtom(x_display, "_NET_WM_STATE", False);
    Atom maximized[] = {XInternAtom(x_display, "_NET_WM_STATE_MAXIMIZED_HORZ", False),
                        XInternAtom(x_display, "_NET_WM_STATE_MAXIMIZED_VERT", False)};
    XChangeProperty(x_display, x_window, window_state, XA_ATOM, 32,
                    PropModeReplace, (const unsigned char *)maximized, 2);
    /* The MayOS content renders the cursor to match its configured style. */
    char clear = 0;
    Pixmap bitmap = XCreateBitmapFromData(x_display, x_window, &clear, 1, 1);
    XColor color = {0};
    Cursor invisible = XCreatePixmapCursor(x_display, bitmap, bitmap, &color, &color, 0, 0);
    XDefineCursor(x_display, x_window, invisible);
    XFreeCursor(x_display, invisible);
    XFreePixmap(x_display, bitmap);
    XMapWindow(x_display, x_window);
    XFlush(x_display);
    return 1;
}

static void x11_present(u32 *pixels) {
    surface_present(&display, pixels, display_scale_mode);
    XPutImage(x_display, x_window, DefaultGC(x_display, DefaultScreen(x_display)),
              x_image, 0, 0, 0, 0, x_width, x_height);
    XFlush(x_display);
}

static void x11_mouse(int x, int y, int *mouse_x, int *mouse_y) {
    *mouse_x = surface_screen_to_desktop(&display, display_scale_mode, x < 0 ? 0 : (u32)x, 0);
    *mouse_y = surface_screen_to_desktop(&display, display_scale_mode, y < 0 ? 0 : (u32)y, 1);
}

static int x11_input(int *mouse_x, int *mouse_y, int *buttons, int *key) {
    for (unsigned i = 0; i < sizeof(firefox_children) / sizeof(firefox_children[0]); i++)
        if (firefox_children[i] && waitpid(firefox_children[i], NULL, WNOHANG) == firefox_children[i])
            firefox_children[i] = 0;
    int budget = 64;
    while (budget-- && XPending(x_display)) {
        XEvent event;
        XNextEvent(x_display, &event);
        switch (event.type) {
        case ConfigureNotify:
            if ((unsigned)event.xconfigure.width != x_width || (unsigned)event.xconfigure.height != x_height)
                x11_surface(event.xconfigure.width, event.xconfigure.height);
            return 1;
        case Expose:
            return 1;
        case FocusOut:
            *buttons = 0;
            return 1;
        case MotionNotify:
            x11_mouse(event.xmotion.x, event.xmotion.y, mouse_x, mouse_y);
            return 1;
        case ButtonPress: case ButtonRelease: {
            unsigned int click = event.xbutton.button;
            int bit = click == Button1 ? 1 : click == Button3 ? 2 : click == Button2 ? 4 : 0;
            x11_mouse(event.xbutton.x, event.xbutton.y, mouse_x, mouse_y);
            if (event.type == ButtonPress) *buttons |= bit; else *buttons &= ~bit;
            if (event.type == ButtonPress && (click == Button4 || click == Button5))
                *key = click == Button4 ? 0x101 : 0x102;
            return 1;
        }
        case KeyPress:
            left_shift = !!(event.xkey.state & ShiftMask);
            left_ctrl = !!(event.xkey.state & ControlMask);
            right_shift = right_ctrl = 0;
            caps_lock = !!(event.xkey.state & LockMask);
            if (event.xkey.keycode >= 8 && (*key = keycode(event.xkey.keycode - 8, 1)))
                return 1;
            break;
        case ClientMessage:
            if ((Atom)event.xclient.data.l[0] == x_close) exit(0);
            break;
        }
    }
    return 0;
}

int platform_browser_ready(void) {
    return x_display && geteuid() != 0 && access("/usr/bin/firefox-esr", X_OK) == 0;
}

int platform_browser_open(void) {
    if (!platform_browser_ready()) return 0;
    for (unsigned i = 0; i < sizeof(firefox_children) / sizeof(firefox_children[0]); i++) {
        if (firefox_children[i]) continue;
        pid_t pid = fork();
        if (pid < 0) return 0;
        if (!pid) {
            execl("/usr/bin/firefox-esr", "firefox-esr", "about:blank", (char *)NULL);
            _exit(127);
        }
        firefox_children[i] = pid;
        serial("MayOS Linux: Firefox launched from the desktop\n");
        return 1;
    }
    return 0;
}
