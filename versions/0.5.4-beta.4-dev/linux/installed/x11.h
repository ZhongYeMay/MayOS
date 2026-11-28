/* X11 backend for the hard-disk desktop. The live image still uses fbdev. */
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <strings.h>

static Display *x_display;
static Window x_window;
static XImage *x_image;
static Atom x_close, x_clients, x_active, x_request_close;
static Window x_firefox, x_focused;
static int firefox_ready;
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

static Window *x11_windows(Atom property, unsigned long *count) {
    Atom type;
    int format;
    unsigned long tail;
    unsigned char *data = NULL;
    Window root = DefaultRootWindow(x_display);
    if (XGetWindowProperty(x_display, root, property, 0, 4096, False, XA_WINDOW,
                          &type, &format, count, &tail, &data) == Success &&
        type == XA_WINDOW && format == 32)
        return (Window *)data;
    if (data) XFree(data);
    *count = 0;
    return NULL;
}

static int x11_ignore_stale_window(Display *display, XErrorEvent *event) {
    (void)display;
    if (event->error_code != BadWindow)
        fprintf(stderr, "MayOS X11 error %u (request %u)\n",
                event->error_code, event->request_code);
    return 0;
}

static void x11_refresh_browser(void) {
    unsigned long count;
    Window *active = x11_windows(x_active, &count);
    x_focused = count ? active[0] : None;
    if (active) XFree(active);
    x_firefox = None;
    Window *clients = x11_windows(x_clients, &count);
    while (count > 0) {
        Window window = clients[--count];
        XClassHint hint;
        if (!XGetClassHint(x_display, window, &hint)) continue;
        int firefox = hint.res_class && (!strcasecmp(hint.res_class, "firefox") ||
                                         !strcasecmp(hint.res_class, "firefox-esr"));
        if (hint.res_name) XFree(hint.res_name);
        if (hint.res_class) XFree(hint.res_class);
        if (firefox && (!x_firefox || window == x_focused)) x_firefox = window;
        if (x_firefox && x_firefox == x_focused) break;
    }
    if (clients) XFree(clients);
}

static void x11_browser_message(Atom message, long first, long second, long third) {
    XEvent event = {0};
    event.xclient.type = ClientMessage;
    event.xclient.window = x_firefox;
    event.xclient.message_type = message;
    event.xclient.format = 32;
    event.xclient.data.l[0] = first;
    event.xclient.data.l[1] = second;
    event.xclient.data.l[2] = third;
    XSendEvent(x_display, DefaultRootWindow(x_display), False,
               SubstructureRedirectMask | SubstructureNotifyMask, &event);
    XFlush(x_display);
}

static int x11_open(void) {
    x_display = XOpenDisplay(NULL);
    if (!x_display) return 0;
    XSetErrorHandler(x11_ignore_stale_window);
    int screen = DefaultScreen(x_display);
    x_window = XCreateSimpleWindow(x_display, RootWindow(x_display, screen),
                                   0, 0, W, H, 0, 0, BlackPixel(x_display, screen));
    if (!x_window || !x11_surface(W, H)) return 0;
    XStoreName(x_display, x_window, "MayOS Desktop");
    Atom decorations = XInternAtom(x_display, "_MOTIF_WM_HINTS", False);
    unsigned long hints[] = {2, 0, 0, 0, 0};
    XChangeProperty(x_display, x_window, decorations, decorations, 32,
                    PropModeReplace, (const unsigned char *)hints, 5);
    x_clients = XInternAtom(x_display, "_NET_CLIENT_LIST", False);
    x_active = XInternAtom(x_display, "_NET_ACTIVE_WINDOW", False);
    x_request_close = XInternAtom(x_display, "_NET_CLOSE_WINDOW", False);
    XSelectInput(x_display, DefaultRootWindow(x_display), PropertyChangeMask);
    x11_refresh_browser();
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
    static time_t last_check;
    time_t now = time(NULL);
    if (now != last_check) {
        last_check = now;
        int ready = geteuid() != 0 && access("/usr/bin/firefox-esr", X_OK) == 0;
        if (ready != firefox_ready) {
            firefox_ready = ready;
            return 1;
        }
    }
    int budget = 64;
    while (budget-- && XPending(x_display)) {
        XEvent event;
        XNextEvent(x_display, &event);
        switch (event.type) {
        case PropertyNotify:
            if (event.xproperty.atom == x_clients || event.xproperty.atom == x_active) {
                x11_refresh_browser();
                return 1;
            }
            break;
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
    return firefox_ready;
}

int platform_browser_state(void) {
    return !x_firefox ? 0 : x_firefox == x_focused ? 2 : 1;
}

void platform_browser_window_action(int close) {
    if (!x_firefox) return;
    if (close) x11_browser_message(x_request_close, CurrentTime, 2, 0);
    else
        XIconifyWindow(x_display, x_firefox, DefaultScreen(x_display));
}

int platform_browser_open(void) {
    if (!platform_browser_ready()) return 0;
    if (x_firefox) {
        x11_browser_message(x_active, 2, CurrentTime, x_focused);
        serial("MayOS Linux: Firefox restored from the desktop\n");
        return 1;
    }
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
