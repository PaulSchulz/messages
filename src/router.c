
// router

// This program implements store and forward routing queues of UDP
// messages. It will listen on configurd UDP ports and queue the
// contents of any packets received in queues for further processing.

// Required for networking code with GNU libc. Is not portable to other libc.
#include <errno.h>

// GLib headers
#include <glib.h>
#include <gio/gio.h>
#include <gmodule.h>

// Gtk
#include <gtk/gtk.h>

#define BUFSIZE 1024
#define TEXTBUF 256
#define STRSIZE 32

// #define DEBUG
#ifdef DEBUG
#define   D(...) g_printerr(__VA_ARGS__);
#else
#define   D(...)
#endif

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

// Pre-declarations
void rt_queue_display(RtQueue *rtqueue);

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
rt_queue_message_handler (GSocket *gSock, GIOCondition condition, RtQueue* rtqueue_p)
{
    GError         *error = NULL;
    GSocketAddress *gsRmtAddr = NULL;
    GInetAddress   *gsAddr = NULL;
    gchar          *rmtHost = NULL;
    gchar          *stamp = "";
    gssize         gss_receive = 0;

    gchar       message[BUFSIZE];
    gssize      size;
    const gchar *timestamp;
    gchar       *peer;

    RtData *data;

    D("[DEBUG] Receivng UDP packet - Condition: %s\n",
      skn_gio_condition_to_string(condition));
    D("[DEBUG]   on queue %s\n", rtqueue_p->name);

    // FIXME: Is this still required? Can this be fixed
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
                                         message,
                                         sizeof(message),
                                         NULL,
                                         &error);

    if (error != NULL) {
        // gss_receive = Number of bytes read, or 0 if the connection was closed by the peer, or -1 on error
        g_error("[ERROR] g_socket_receive_from() => %s", error->message);
        g_clear_error(&error);
        return (G_SOURCE_CONTINUE);
    }

    // Terminate message string.
    message[gss_receive] = '\0';

    D("[DEBUG] Received UDP packet from client - %ld bytes\n", gss_receive);
    D("[DEBUG] Message: %s\n", message);

    data = g_slice_alloc(sizeof(RtData));
    data->timein = g_get_real_time();
    data->message = g_strdup (message);
    g_queue_push_tail (rtqueue_p->queue, data);

    // DEBUG
    rt_queue_display(rtqueue_p);

    return (G_SOURCE_CONTINUE);
}

void
rt_queue_open (RtQueue * rtqueue_p)
{
    guint16 port;
    GSocket *gSock;
    GInetAddress *anyAddr;
    GSocketAddress *gsAddr;
    GSource *gSource;
    guint gSourceId;

    GError *error = NULL;

    g_print("[QUEUE] Open:%s port_in:%d target:%s:%s:%d\n",
            rtqueue_p->name,
            rtqueue_p->port_in,
            rtqueue_p->target.name,
            rtqueue_p->target.address,
            rtqueue_p->target.port);

    port = rtqueue_p->port_in;

    // Create networking socket for UDP

    gSock = g_socket_new(G_SOCKET_FAMILY_IPV4,
                         G_SOCKET_TYPE_DATAGRAM,
                     G_SOCKET_PROTOCOL_UDP,
                     &error);
    if (error != NULL) {
        g_error("g_socket_new() => %s", error->message);
        g_clear_error(&error);
        exit(EXIT_FAILURE);
    }

// FIXME: Add ability to select network address to listen on (eg. if address)
D("[DEBUG] - Create networking address for reception\n");
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
                           (GSourceFunc) rt_queue_message_handler,
                           // its really a GSocketSourceFunc
                           rtqueue_p,
                           NULL);

    D("[DEBUG] - Listening on * %d\n", port);

    gSourceId = g_source_attach (gSource, NULL);
    g_print("[QUEUE] Listening:%s port:%d\n",
            rtqueue_p->name,
            port);
}

//////////////////////////////////////////////////////////////////////////////
// Text Display

// DEBUG: This is a debugging only function at the moment.
void
rt_queue_display(RtQueue *rtqueue)
{
    guint size,i;
    RtData *data;

    GDateTime *datetime;

    size = g_queue_get_length (rtqueue->queue);
    D("[DEBUG] Queue: %s\n", rtqueue->name);
    D("[DEBUG] queue length: %d\n", size);

    for (i=0; i<size; i++){
        data = g_queue_peek_nth (rtqueue->queue, i);

        // g_print("timein:  %8ld  \n", data->timein/1000000);
        datetime = g_date_time_new_from_unix_local (data->timein/1000000);
        gchar *str = g_date_time_format (datetime, "%Y/%m/%d %H:%M:%S %z");
        D("[DEBUG]   %s | %s\n", str, data->message);
        g_free(str);
        g_date_time_unref(datetime);
    }
}

// Global Data
GArray *queues;     // Array of Queues

//////////////////////////////////////////////////////////////////////////////
int
main (int    argc,
      char **argv)
{
    RtQueue rtqueue;
    RtData *data = NULL;
    GQueue *queue;

    // Setup Queues
    queue = g_queue_new();
    queues = g_array_new (FALSE, FALSE, sizeof(RtQueue));

    //widgets->queues = queues;
    // TODO: Allow the queues to be configured via a config file.
    rtqueue.name           = "echo-10s";
    rtqueue.port_in        = 4479;
    rtqueue.target.name    = "earth-echo";
    rtqueue.target.address = "10.1.1.83";
    rtqueue.target.port    = 4478;
    rtqueue.queue          = g_queue_new();
    rtqueue.delay          = 10;
    g_array_append_val (queues, rtqueue);

    rtqueue.name           = "mars-alpha";
    rtqueue.port_in        = 4479;
    rtqueue.target.name    = "mars-alpha";
    rtqueue.target.address = "10.1.1.193";
    rtqueue.target.port    = 4478;
    rtqueue.queue          = g_queue_new();
    rtqueue.delay          = 0;
    g_array_append_val (queues, rtqueue);

    rtqueue.name           = "earth-alpha";
    rtqueue.port_in        = 4480;
    rtqueue.target.name    = "earth-alpha";
    rtqueue.target.address = "10.1.1.83";
    rtqueue.target.port    = 4478;
    rtqueue.queue          = g_queue_new();
    rtqueue.delay          = 0;
    g_array_append_val (queues, rtqueue);

    D("[DEBUG] Open router queue and UDP socket for receiving messages\n");
    D("[DEBUG] Number of queues: %d\n", queues->len);
    g_print("Here!\n");
    for(int i=0; i<queues->len; i++){
        rt_queue_open(&g_array_index(queues, RtQueue, i));
    }

    D("[DEBUG] Starting gtk_main\n");

    gtk_main ();

    return 0;
}
