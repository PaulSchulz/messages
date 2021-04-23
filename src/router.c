// router

// An application to implement store and forward routing of UDP messages,
// including processing of packets. eg. adding delay.

// Required for networking code with GNU libc. Is not portable to other libc.
#include <errno.h>

// GLib headers
#include <glib.h>
#include <gio/gio.h>
//#include <gmodule.h>

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

// Target data
typedef struct {
    gchar * name;
    gchar * address;
    guint16 port;
} RtTarget;

// Message data
typedef struct {
    gint64 timein;
    gchar *message;
} RtData;

// Queues - messages are sorted into queues, which may have different delay
// times.
typedef struct {
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

typedef struct {
    guint     gSourceId;
    gchar     packetData[BUFSIZE];
} appWidgets;

appWidgets widgetData;
appWidgets *widgets = &widgetData;

//////////////////////////////////////////////////////////////////////////////
// Utilities

gchar * skn_gio_condition_to_string(GIOCondition condition)
{
    gchar *value = NULL;

    switch(condition) {
    case G_IO_IN:
        value = "G_IO_IN (There is data to read.)";
        break;
    case G_IO_OUT:
        value = "G_IO_OUT (Data can be written (without blocking).)";
        break;
    case G_IO_PRI:
        value = "G_IO_PRI (There is urgent data to read.)";
        break;
    case G_IO_ERR:
        value = "G_IO_ERR (Error condition.)";
        break;
    case G_IO_HUP:
        value = "G_IO_HUP (Hung up (the connection has been broken, usually for pipes and sockets).)";
        break;
    case G_IO_NVAL:
        value = "G_IO_NVAL (Invalid request. The file descriptor is not open.)";
        break;
    default:
        value = "Unknown GIOCondition!";
    }

    return (value);
 }

//////////////////////////////////////////////////////////////////////////////
// Networking
// Receive Packets

static gboolean
rt_receive_message_handler (GSocket *gSock, GIOCondition condition, appWidgets *widgets)
{
    GError *error = NULL;
    GSocketAddress *gsRmtAddr = NULL;
    GInetAddress *gsAddr = NULL;
    gchar * rmtHost = NULL;
    gchar *stamp = "";
    gssize gss_receive = 0;

    gchar message[1024];
    gssize size;
    const gchar *timestamp;
    gchar *peer;

    // FIXME - The following is a workaround as 'widgets' is not passed properly
    // and I don't know why. Implemented via a global variable 'widgetData'.
    widgets = &widgetData;

    // DEBUG
    D("[DEBUG] Received UDP packet - Condition: %s\n",
      skn_gio_condition_to_string(condition));

    if ((condition & G_IO_HUP) || (condition & G_IO_ERR) || (condition & G_IO_NVAL)) {
        /* SHUTDOWN THE MAIN LOOP */
        D("[DEBUG] DisplayService::cb_udp_request_handler(error) G_IO_HUP => %s\n",
          skn_gio_condition_to_string(condition));
        //g_main_loop_quit(pctrl->loop);
        return ( G_SOURCE_REMOVE );
    }

    if (condition != G_IO_IN) {
        D("[DEBUG] DisplayService::cb_udp_request_handler(error) NOT G_IO_IN => %s\n",
          skn_gio_condition_to_string(condition));
        return (G_SOURCE_CONTINUE);
    }

    // If socket times out before reading data any operation will error with 'G_IO_ERROR_TIMED_OUT'.
    gss_receive = g_socket_receive_from (gSock,
                                         &gsRmtAddr,
                                         widgets->packetData,
                                         sizeof(widgets->packetData),
                                         NULL,
                                         &error);

    if (error != NULL) {
        // gss_receive = Number of bytes read, or 0 if the connection was closed by the peer, or -1 on error
        g_error("[ERROR] g_socket_receive_from() => %s", error->message);
        g_clear_error(&error);
        return (G_SOURCE_CONTINUE);
    }

    // TODO: Put stuff here
    D("[DEBUG] Received UDP packet from client! %ld bytes\n", gss_receive);
    return (G_SOURCE_CONTINUE);
}

void
rt_socket_open (char *address, guint16 port)
{
    GSocket *gSock;
    GInetAddress *anyAddr;
    GSocketAddress *gsAddr;
    GSource *gSource;
    guint gSourceId;

    GError *error = NULL;

    // Create networking socket for UDP
    D("[DEBUG] - Create networking socket for listening for UDP packets\n");
    gSock = g_socket_new(G_SOCKET_FAMILY_IPV4,
                         G_SOCKET_TYPE_DATAGRAM,
                         G_SOCKET_PROTOCOL_UDP,
                         &error);
    if (error != NULL) {
        g_error("g_socket_new() => %s", error->message);
        g_clear_error(&error);
        exit(EXIT_FAILURE);
    }

    D("[DEBUG] - Create networking address to listen to\n");
    // FIXME: Add ability to select network address to listen on (eg. if address)
    anyAddr = g_inet_address_new_any(G_SOCKET_FAMILY_IPV4);
    gsAddr = g_inet_socket_address_new(anyAddr, port);

    // Bind address to socket
    D("[DEBUG] - Bind socket to network address\n");
    g_socket_bind(gSock, gsAddr, TRUE, &error);
    if (error != NULL) {
        g_error("g_socket_bind() => %s", error->message);
        g_clear_error(&error);
        exit(EXIT_FAILURE);
    }

// Create and add socket to gmain loop for UDP service.
    D("[DEBUG] - Add socket to main loop to service received packets\n");
    gSource = g_socket_create_source (gSock, G_IO_IN, NULL);
    g_source_set_callback (gSource,
                           (GSourceFunc) rt_receive_message_handler,
                           // its really a GSocketSourceFunc
                           &widgets,
                           NULL);

    widgets->gSourceId = gSourceId = g_source_attach (gSource, NULL);
}

//////////////////////////////////////////////////////////////////////////////
// Display

void
rt_queue_display(GQueue *queue)
{
    guint size,i;
    RtData *data;

    GDateTime *datetime;

    //    datetime = g_date_time_new_from_unix_local ();
    size = g_queue_get_length (queue);

    for (i=0; i<size; i++){
        data = g_queue_peek_nth (queue,i);

        // g_print("timein:  %8ld  \n", data->timein/1000000);
        datetime = g_date_time_new_from_unix_local (data->timein/1000000);
        gchar *str = g_date_time_format (datetime, "%Y/%m/%d %H:%M:%S %z");
        // g_print("%s | ", str);
        g_free(str);
        g_date_time_unref(datetime);
        // g_print("%s\n", data->message);

    }
}

void
nc_queue_display_header (int y, int x)
{
    mvprintw(y,  x,"%s", "PortIn  From          To                   Delay   Msgs  Next");
    mvprintw(y+1,x,"%s", "------  ------------  ------------------  ------  -----  -------------------------");
}

// FIXME: The following breaks if a queue has not data in it.
void
nc_queue_display_mv (int y, int x, RtQueue *rtqueue)
{
    guint size;
    RtData *data;
    GDateTime *datetime;
    GQueue *queue = rtqueue->queue;

    gchar *dst = "";
    gchar *str;

    dst = g_strnfill (TEXTBUF,' ');
    g_snprintf(dst, TEXTBUF, "%s:%d",
               rtqueue->target.address,
               rtqueue->target.port);
    size = g_queue_get_length (queue);
    data = g_queue_peek_head (queue);
    datetime = g_date_time_new_from_unix_local (data->timein/1000000);
    str = g_date_time_format (datetime, "%Y/%m/%d %H:%M:%S %z");

    mvprintw(y,x,"%6d  %-12s  %-18s  %6d  %5d  %s",
             rtqueue->port_in,
             rtqueue->target.name,
             dst,
             rtqueue->delay,
             size,
             str
        );
    g_free(str);
    g_free(dst);
    g_date_time_unref(datetime);
}

int
main (int    argc,
      char **argv)
{
    RtQueue rtqueue_in, rtqueue_out;

    RtData *data = NULL;

    GQueue *queue;
    queue = g_queue_new();

    rt_socket_open("*", 4478);

    // Test data
    data = g_slice_alloc(sizeof(RtData));
    data->timein = g_get_real_time();
    data->message = g_strdup ("first");
    g_queue_push_tail (queue, data);

    data = g_slice_alloc(sizeof(RtData));
    data->timein = g_get_real_time();
    data->message = g_strdup ("second");
    g_queue_push_tail (queue, data);
    // g_print("Messages Queued\n");

    // g_print("Display Message Queued\n");
    // rt_queue_display(queue);

    // g_print("Remove first element\n");
    // data = g_queue_pop_head(queue);
    // g_free(data);

    // g_print("Display Message Queued\n");
    // rt_queue_display(queue);

    int ch;

    initscr();
    raw();
    keypad(stdscr, TRUE);
    noecho();

    // TODO: Temporary rtesting data. This should go into configuration file.
    // Messages to mars
    rtqueue_out.port_in        = 4478;
    rtqueue_out.target.name    = "mars-alpha";
    rtqueue_out.target.address = "10.1.1.1";
    rtqueue_out.target.port    = 4478;
    rtqueue_out.delay          = 225;
    rtqueue_out.queue          = queue; // g_queue_new();

    // Messages from mars
    rtqueue_in.port_in         = 4479;
    rtqueue_in.target.name     = "earth-alpha";
    rtqueue_in.target.address  = "10.1.1.2";
    rtqueue_in.target.port     = 4478;
    rtqueue_in.delay           = 225;
    rtqueue_in.queue           = queue; // g_queue_new();

    printw("Queues");
    nc_queue_display_header (2,0);
    nc_queue_display_mv (4,0,&rtqueue_out);
    nc_queue_display_mv (5,0,&rtqueue_in);

    refresh();
    ch=getch();

    endwin();

    // g_print("Free list and it's elements\n");
    // g_slist_free_full(list, g_free);

    return 0;
}
