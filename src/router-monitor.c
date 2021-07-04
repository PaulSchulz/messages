// router-monitor

// This program monitors the router traffic and displays an ongoing report.

// Required for networking code with GNU libc. Is not portable to other libc.
#include <errno.h>

// GLib headers
#include <glib.h>
#include <gio/gio.h>
#include <gmodule.h>

// Gtk
#include <gtk/gtk.h>

// Text interface
#include <ncurses.h>

#define BUFSIZE 1024
#define TEXTBUF 256
#define STRSIZE 32

#define DEBUG
#ifdef DEBUG
#define   D(...) g_printerr(__VA_ARGS__);
#else
#define   D(...)
#endif

// NCURSES interface
// #define NCURSES FALSE
#define NCURSES TRUE

// Target data
typedef struct {
    gchar* name;
    gchar* address;
    guint16 port;
} RtTarget;

// Message data
typedef struct {
    gint64 timein;
    gchar* message;
} RtData;

// Queues - messages are sorted into queues, which may have different delay
// times.
typedef struct {
    gchar    *name;
    RtTarget target;
    guint16  port_in;
    GQueue   *queue;
    gint64   delay;       // Default delay to target in seconds.
    gint64   nextservice; // When the next packet should be sent. Stored here so
                          // that the queue does not need to store this time
                          // (uint64) for every packet. It is calculated by
                          // looking at the next message packet after a packet
                          // is sent. Set to 0 if no packet available to be
                          // sent. If a packet is added to an empty queue (first
                          // packet) then this value also needs to be set.
} RtQueue;

// FIXME: No longer a widget data structure. Should be renamed.
typedef struct {
  guint     gSourceId;
  gchar     packetData[BUFSIZE];
  RtQueue  *rtqueue;
  GArray   *queues;
} appWidgets;

appWidgets widgetData;
appWidgets *widgets = &widgetData;

gboolean enable_ncurses = NCURSES;

// Pre-declarations
void rt_queue_display(RtQueue *rtqueue);

//////////////////////////////////////////////////////////////////////////////
// NCurses Display
void
rt_ncurses_open()
{
  initscr();
  raw();
  keypad(stdscr, TRUE);
  noecho();
}

void
rt_ncurses_close()
{
    endwin();
}

void
rt_queue_display_header (int y, int x)
{
    mvprintw(y,  x,"%s", "Name          PortIn  From          To                   Delay   Msgs  Next");
    mvprintw(y+1,x,"%s", "------------  ------  ------------  ------------------  ------  -----  -------------------------");
}

// FIXME: The following breaks if a queue has not data in it.
void
rt_queue_display_mv (int y, int x, RtQueue *rtqueue_p)
{
    guint      size;
    RtData*    data;
    GDateTime* datetime;
    GQueue*    queue = rtqueue_p->queue;

    gchar*     dst = "";
    gchar*     str;

    dst = g_strnfill (TEXTBUF,' ');
    g_snprintf(dst, TEXTBUF, "%s:%d",
               rtqueue_p->target.address,
               rtqueue_p->target.port);
    //size = g_queue_get_length (queue);
    //data = g_queue_peek_head (queue);
    datetime = g_date_time_new_from_unix_local (data->timein/1000000);
    str = g_date_time_format (datetime, "%Y/%m/%d %H:%M:%S %z");

    mvprintw(y,x,"%-12s  %6d  %-12s  %-18s  %6d  %5d  %s",
	     rtqueue_p->name,
             rtqueue_p->port_in,
             rtqueue_p->target.name,
             dst,
             rtqueue_p->delay,
             size,
             str
        );
    g_free(str);
    g_free(dst);
    g_date_time_unref(datetime);
}

void
rt_ncurses_screen(GArray* queues){
  printw("Queues");
  rt_queue_display_header (2,0);
  for(int i; i<queues->len; i++){
    rt_queue_display_mv (4+i, 0, &g_array_index(queues, RtQueue, i));
  }
  refresh();
};

//////////////////////////////////////////////////////////////////////////////
GArray *queues;     // Array of Queues

//////////////////////////////////////////////////////////////////////////////
int
main (int    argc,
      char **argv)
{
    D("[DEBUG] Not working yet.");
    D("[DEBUG] Monitoring...");

    // Setup NCURSES display
    if (enable_ncurses){
        rt_ncurses_open();
    }

    // NCURSES screen
    // if(enable_ncurses){
//    rt_ncurses_screen(queues);
//}

    //    rt_queue_display_header(1, 1);

    // gtk_main ();

    // Cleanup NCURSES display
    if(enable_ncurses){
        rt_ncurses_close();
    }

    return 0;
}
