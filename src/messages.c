// messages

// An application for sending and receiving UDP packet based messages.
//
// Author: Paul Schulz <paul@mawsonlakes.org>
// License; GNU AFFERO GENERAL PUBLIC LICENSE v3, see LICENSE

// Required for networking code with GNU libc. Is not portable to other libc.
#include <errno.h>

// glib
#include <glib.h>
#include <gio/gio.h> // Needed for GSettings, ampngst other things
// #include <gnet.h> // Used for UDP Networking, but couldn't get this to work.
#include <gio/gnetworking.h> // What is this required for?

// Gtk
#include <gtk/gtk.h>

// Networking
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

// Maximum message size
#define BUFSIZE 1024
#define STRSIZE 32

#define DEBUG

#ifdef DEBUG
#define   D(...) g_printerr(__VA_ARGS__);
#else
#define   D(...)
#endif

// Peer data
typedef struct {
    const gchar *name;
    gchar       *address;
    gint         port;
} peerData;

// Data structure for passing Widget pointers to callbacks
typedef struct {
    GtkWidget *messagesTextView;
    GtkTextBuffer *messagesTextBuffer;

    GtkWidget *messageTextView;
    GtkTextBuffer *messageTextBuffer;

    GtkWidget *timestamp;
    GtkWidget *peer;
    GtkWidget *rtt;
    GtkWidget *statusIcon;
    GtkWidget *statusLabel;
    GtkWidget *send;
    GtkWidget *over;

    guint     gSourceId;
    gchar     packetData[BUFSIZE];
} appWidgets;

peerData peer = { .name = "cashew",
                  .address = "10.1.1.193",
                  .port = 4478
};

appWidgets widgetData;

GDateTime *datetime; // local time

//////////////////////////////////////////////////////////////////////////////
// Low level UI function

void
echo_line (appWidgets *widgets, gchar *line)
{
    //GtkTextBuffer *messages;
    GtkTextIter messagesIter;

    D("[DEBUG] echo_line()\n");
    gtk_text_buffer_get_end_iter(GTK_TEXT_BUFFER(widgets->messagesTextBuffer),&messagesIter);
    gtk_text_buffer_insert (GTK_TEXT_BUFFER(widgets->messagesTextBuffer), &messagesIter, line, -1);
    gtk_text_buffer_insert (GTK_TEXT_BUFFER(widgets->messagesTextBuffer), &messagesIter, "\n", -1);
}

// Uses 'echo_line' for basic line output
void
echo_message (appWidgets *widgets,
              const gchar *timestamp,
              const gchar *outdir,
              const gchar *peer,
              const gchar *indir,
              gchar *message)
{
    gchar text[BUFSIZE];

    // TODO: Handle multiple line messages.
    g_snprintf(text, BUFSIZE, "%-8s  %-2s %-12s %-2s  %s", timestamp, outdir, peer, indir, message);

    echo_line(widgets,text);
}

void
updateClock(appWidgets *widgets)
{
    GDateTime   *time;            // for storing current time and date
    gchar       *time_str;        // current time and date as a string

    time     = g_date_time_new_now_local();          // get the current time
    time_str = g_date_time_format(time, "%H:%M:%S"); // convert current time to string

    gtk_label_set_text(GTK_LABEL(widgets->timestamp), time_str);

    // free memory used by glib functions
    g_free(time_str);
    g_date_time_unref(time);
}

void
updateRTT(gchar *rtt_str, appWidgets *widgets)
{
    gtk_label_set_text(GTK_LABEL(widgets->rtt), rtt_str);
}

//////////////////////////////////////////////////////////////////////////////
// Low level networking functions

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

static gboolean
receive_message_handler (GSocket *gSock, GIOCondition condition, appWidgets *widgets)
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

    D("[DEBUG] Update clock\n");
    updateClock(widgets);

    D("[DEBUG] Get timestamp\n");
    timestamp = gtk_label_get_text(GTK_LABEL(widgets->timestamp));
    peer = "mars-alpha";

    D("[DEBUG] Dispay message peer: %s\n", peer);
    echo_message(&widgetData, timestamp, "  ", peer, "->", widgets->packetData);

    D("[DEBUG] Received UDP packet from client! %ld bytes\n", gss_receive);
    return (G_SOURCE_CONTINUE);

}

static int
send_message (peerData peer, char *buffer)
{
    char   buf[BUFSIZE];
    char   *hostname;
    int    sockfd;
    int    portno;
    int    n;
    int    serverlen;
    struct sockaddr_in  serveraddr;
    struct hostent      *server;

    char   myhostname[STRSIZE];

    // Networking peer
    hostname = peer.address;
    portno   = peer.port;

    // Open Socket for writing
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0)
        fprintf(stderr,"ERROR opening socket") ;

    // gethostbyname: get the server's DNS entry
    server = gethostbyname(hostname);
    if (server == NULL) {
        fprintf(stderr,"ERROR, no such host as %s\n", hostname);
        exit(0);
    }

    // Build the server's Internet address
    bzero((char *) &serveraddr, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    bcopy((char *)server->h_addr,
          (char *)&serveraddr.sin_addr.s_addr, server->h_length);
    serveraddr.sin_port = htons(portno);

    // Build message
    n = g_snprintf(buf, BUFSIZE, "%s", buffer);
    buf[n] = '\0';

    /* Send the packet */
    serverlen = sizeof(serveraddr);
    n = sendto(sockfd, buf, strlen(buf), 0,
               (struct sockaddr *) &serveraddr, serverlen);
    if (n < 0)
        fprintf(stderr,"ERROR in sendto");

    // DEBUG
    D("[DEBUG] Packet sent: %s %s:%d\n",
      peer.name,
      peer.address,
      peer.port);
}

//////////////////////////////////////////////////////////////////////////////
// callback functions

static void
clickedSend(GtkButton *button,
            appWidgets *widgets)
{
    const gchar *timestamp;
    // gchar *peer;

    GtkTextIter start_iter,end_iter;
    gchar *text;
    GtkTextIter messagesIter;

    updateClock(widgets);

    timestamp = gtk_label_get_text(GTK_LABEL(widgets->timestamp));

    gtk_text_buffer_get_start_iter(GTK_TEXT_BUFFER(widgets->messageTextBuffer), &start_iter);
    gtk_text_buffer_get_end_iter(GTK_TEXT_BUFFER(widgets->messageTextBuffer), &end_iter);
    text = gtk_text_buffer_get_text(GTK_TEXT_BUFFER(widgets->messageTextBuffer), &start_iter, &end_iter, TRUE);

    // FIXME: peer is currently a global structure
    send_message(peer, text);
    echo_message(widgets, timestamp, "->", peer.name , "  ", text);
    gtk_text_buffer_set_text (GTK_TEXT_BUFFER(widgets->messageTextBuffer), "", -1);

    gtk_label_set_text(GTK_LABEL(widgets->statusLabel), "Sent");

    updateRTT("7h29m",widgets);

// FIXME: text might be a memory leak
}

static void
clickedOver(GtkButton *button,
            appWidgets *widgets)
{
    gtk_label_set_text(GTK_LABEL(widgets->statusLabel), "Over");
    clickedSend(button,widgets);

    // FIXME: target is currently a global structure
    send_message(peer, "[OVER]\n");
}


//////////////////////////////////////////////////////////////////////////////

static void
activate (GtkApplication* app,
          gpointer        user_data)
{
  GtkWidget *window;

  window = gtk_application_window_new (app);
  gtk_window_set_title (GTK_WINDOW (window), "Messages");
  gtk_window_set_default_size (GTK_WINDOW (window), 800, 600);
  gtk_widget_show_all (window);
}

static void
close_window (GtkWidget *widget)
{
    gtk_main_quit ();
}

int
main (int    argc,
      char **argv)
{
    // Use GtkBuilder to create interface. Use Glade to edit configuration file.

    GtkBuilder    *builder;
    GObject       *window , *button;
    GtkTextBuffer *messagesTextBuffer;
    GtkTextBuffer *targetsTextBuffer;
    GtkTextBuffer *messageTextBuffer;

    GtkTextIter iter;
    GtkTextIter targetsIter;

    // Settings object
    GSettings *gsettings;

    // appWidgets *widgets = g_slice_new(appWidgets);
    appWidgets *widgets = &widgetData;

    // UDP Networking
    guint16 gUDPPort = 8400; // Sanity check, should never be seen.
    GSocket *gSock;
    GInetAddress *anyAddr;
    GSocketAddress *gsAddr;
    GSource *gSource;
    guint gSourceId;

    // Settings
    GVariant *portSetting;
    GVariant *peerSetting;

    GError *error = NULL;

    D("[DEBUG] main()\n");
    gtk_init (0, NULL);

    // Settings
    D("[DEBUG] - Reading settings from GSettings\n");
    gsettings = g_settings_new("org.mawsonlakes.messages");

    portSetting = g_settings_get_value (gsettings, "port");
    gUDPPort = atoi(g_variant_print(portSetting,FALSE));
    D("[DEBUG] - Waiting for UDP packets on port %d (from gSettings)\n", gUDPPort);

    // DEBUG - The following 'g_variant_get' creates a buffer which will be the
    // source of a memory leak if not released.
    peerSetting = g_settings_get_value (gsettings, "peer");
    peer.name = g_variant_get_string(peerSetting,NULL);
    D("[DEBUG] - Default message peer: %s (from gSettings)\n", peer.name);

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
    anyAddr = g_inet_address_new_any(G_SOCKET_FAMILY_IPV4);
    gsAddr = g_inet_socket_address_new(anyAddr, gUDPPort);

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
                           (GSourceFunc) receive_message_handler,
                           // its really a GSocketSourceFunc
                           &widgets,
                           NULL);

    widgets->gSourceId = gSourceId = g_source_attach (gSource, NULL);

    D("[DEBUG] - Read GTK builder file\n");
    builder = gtk_builder_new_from_file ("../glade/messages.glade");

    D("[DEBUG] - Get pointers to GTK Objects\n");
    window = gtk_builder_get_object (builder , "window");
    widgets->messagesTextView   = GTK_WIDGET(gtk_builder_get_object(builder, "messagesTextView"));
    widgets->messagesTextBuffer = GTK_TEXT_BUFFER(gtk_builder_get_object(builder, "messagesTextBuffer"));
    widgets->messageTextView    = GTK_WIDGET(gtk_builder_get_object(builder, "messageTextView"));
    widgets->messageTextBuffer  = GTK_TEXT_BUFFER(gtk_builder_get_object(builder, "messageTextBuffer"));
    widgets->timestamp          = GTK_WIDGET(gtk_builder_get_object(builder, "timestamp"));
    widgets->peer               = GTK_WIDGET(gtk_builder_get_object(builder, "peerEntry"));
    widgets->rtt                = GTK_WIDGET(gtk_builder_get_object(builder, "rtt"));
    widgets->statusIcon         = GTK_WIDGET(gtk_builder_get_object(builder, "statusIcon"));
    widgets->statusLabel        = GTK_WIDGET(gtk_builder_get_object(builder, "statusLabel"));
    widgets->send               = GTK_WIDGET(gtk_builder_get_object(builder, "send"));
    widgets->over               = GTK_WIDGET(gtk_builder_get_object(builder, "over"));

    messagesTextBuffer = (GtkTextBuffer*)gtk_builder_get_object (builder, "messagesTextBuffer");
    messageTextBuffer  = (GtkTextBuffer*)gtk_builder_get_object (builder, "messageTextBuffer");

    // gtk_builder_connect_signals (builder, widgets);
    g_signal_connect (window,       "destroy", G_CALLBACK (close_window), NULL);
    g_signal_connect (widgets->send, "clicked", G_CALLBACK (clickedSend),  widgets);
    g_signal_connect (widgets->over, "clicked", G_CALLBACK (clickedOver),  widgets);

    gtk_widget_show_all (GTK_WIDGET (window));

    gtk_text_buffer_set_text (messagesTextBuffer, "", -1);

    // Splash
    D("[DEBUG] - Display welcome message\n");
    echo_line(widgets, "Welcome to Messages");
    echo_line(widgets," A best effort, unreliable messaging application ");
    echo_line(widgets, "------------------------------------------------------");
    echo_line(widgets, "This program is designed to send and receive messages with");
    echo_line(widgets, "UDP packets.");
    echo_line(widgets, "");
    char buf[80];
    g_snprintf(buf, BUFSIZE,
               "Listening for messages on all network interfaces on UDP port %d.",
               gUDPPort);
    echo_line(widgets, buf);
    echo_line(widgets, "");

    // Add timestamp
    datetime = g_date_time_new_from_unix_local (g_get_real_time()/1000000);
    gchar *str = g_date_time_format (datetime, "[%a, %d %b %Y] %H:%M:%S %z");
    D("[DEBUG] - Current time: %s\n", str);
    echo_line(widgets,str);
    g_free(str);
    g_date_time_unref(datetime);

    updateClock(widgets);

    D("[DEBUG] - Send ping to peer\n");
    send_message (peer, "ping");

    D("[DEBUG] - Clear messageTextBuffer\n");
    gtk_text_buffer_set_text (messageTextBuffer, "", -1);

    gtk_main ();

return 0;
}
