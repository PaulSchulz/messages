// messages
// An application for sending and receiving packet based messages

// Notes:
// Using example from:
// - https://skoona.github.io/skn_rpi-display-services/doc/html/cmd_d_s_8c_source.html

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

//
typedef struct {
    gchar   name[STRSIZE];
    gchar   address[STRSIZE];
    gint    port;
} targetData;

// Data structure for passing Widget pointers to callbacks
typedef struct {
    GtkWidget *messagesTextView;
    GtkTextBuffer *messagesTextBuffer;

    GtkWidget *messageTextView;
    GtkTextBuffer *messageTextBuffer;

    GtkWidget *timestamp;
    GtkWidget *target;
    GtkWidget *rtt;
    GtkWidget *statusIcon;
    GtkWidget *statusLabel;
    GtkWidget *send;
    GtkWidget *over;

    guint     gSourceId;
    gchar     packetData[BUFSIZE];
} appWidgets;

targetData target = { .name = "cashew",
                      .address = "10.1.1.193",
                      .port = 4478
};

appWidgets widgetData;

//////////////////////////////////////////////////////////////////////////////
// Low level UI function

void
echo_line (appWidgets *widgets, gchar *line)
{
    //GtkTextBuffer *messages;
    GtkTextIter messagesIter;

    // DEBUG: printf("%s\n", line);
    g_printerr("[DEBUG] Get end iter in messagesTextBuffer\n");
    gtk_text_buffer_get_end_iter(GTK_TEXT_BUFFER(widgets->messagesTextBuffer),&messagesIter);

    g_printerr("[DEBUG] Insert text\n");
    gtk_text_buffer_insert (GTK_TEXT_BUFFER(widgets->messagesTextBuffer), &messagesIter, line, -1);

    g_printerr("[DEBUG] Insert newline\n");
    gtk_text_buffer_insert (GTK_TEXT_BUFFER(widgets->messagesTextBuffer), &messagesIter, "\n", -1);
}

void
echo_message (appWidgets *widgets,
              const gchar *timestamp,
              char *target,
              gchar *direction,
              gchar *message)
{
    gchar text[BUFSIZE];

    // TODO: Handle multiple line messages.
    sprintf(text, "%-8s  %-2s  %-12s  %s", timestamp, direction, target, message);

    echo_line(widgets,text);
}

void
updateClock(appWidgets *widgets)
{
    GDateTime   *time;            // for storing current time and date
    gchar       *time_str;        // current time and date as a string

    time     = g_date_time_new_now_local();                // get the current time
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

// COMMENT: These are in the style of GIO. Maybe they can be included in the GIO
// library at a later date, and extend the network support to UDP traffic in a
// sensible way.
//
// See: gnet-udp for alternatives.

static gssize
g_socket_input_dgram_read (GSocket       *socket,
                           void          *buffer,
                           gsize          count,
                           GCancellable  *cancellable,
                           GError       **error)
{
    return g_socket_receive (socket,
                             buffer,
                             count,
                             cancellable,
                             error);
}

// Available on UNIX style OS
// #ifdef G_OS_UNIX
static int
g_socket_input_dgram_get_fd (GSocket *socket)
{
  return g_socket_get_fd (socket);
}
//#endif

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

// FIXME - This function was taken from an example and needs to be renamed.
static gboolean
cb_udp_request_handler (GSocket *gSock, GIOCondition condition, appWidgets *widgets)
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
    gchar *target;

    // FIXME - The following is a workaround as 'widgets' is not passed properly
    // and I don't know why. Implemented via a global variable 'widgetData'.
    widgets = &widgetData;

    // DEBUG
    g_printerr("[DEBUG] Received UDP packet - Condition: %s\n",
               skn_gio_condition_to_string(condition));

    if ((condition & G_IO_HUP) || (condition & G_IO_ERR) || (condition & G_IO_NVAL)) {
        /* SHUTDOWN THE MAIN LOOP */
        g_printerr("[DEBUG] DisplayService::cb_udp_request_handler(error) G_IO_HUP => %s\n",
                  skn_gio_condition_to_string(condition));
        //g_main_loop_quit(pctrl->loop);
        return ( G_SOURCE_REMOVE );
    }

    if (condition != G_IO_IN) {
        g_printerr("[DEBUG] DisplayService::cb_udp_request_handler(error) NOT G_IO_IN => %s\n",
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

    g_printerr("[DEBUG] Update clock\n");
    updateClock(widgets);

    g_printerr("[DEBUG] Get timestamp\n");
    timestamp = gtk_label_get_text(GTK_LABEL(widgets->timestamp));
    //timestamp = "now()";
    target = "mars-alpha";

    g_printerr("[DEBUG] Dispay message.\n");
    echo_message(&widgetData, timestamp, target, "<-", widgets->packetData);

    // DEBUG
    g_printerr("[DEBUG] Received UDP packet from client! %ld bytes\n", gss_receive);
    return (G_SOURCE_CONTINUE);

}

//////////////////////////////////////////////////////////////////////////////
// network functions

// FIXME: This is a hack to get UDP data to be accepted in glib but it doesn't
// work correctly. Will only receive a single message, then hangs.
gboolean
incomingUDP_callback (GIOChannel *source,
                      GIOCondition condition,
                      appWidgets *widgets)
{
    gchar message[1024];
    gssize size;
    const gchar *timestamp;
    // gchar *target;

    // g_print("Received UDP packet from client!\n");

    // Buffer is not null terminated.
    g_io_channel_read_chars (source,message,1024,&size,NULL);
    g_io_channel_flush (source,NULL);

    printf("Get's here\n");

    message[size] = 0; // null terminator

    g_print("Message was: \"%s\"\n", message);

    updateClock(widgets);
    timestamp = gtk_label_get_text(GTK_LABEL(widgets->timestamp));
    //target = "mars-alpha";
    echo_message(widgets, timestamp, target.name, "<-", message);

    printf("Get's here2\n");

    return TRUE;
}

gboolean
incoming_callback  (GSocketService *service,
                    GSocketConnection *connection,
                    GObject *source_object,
                    appWidgets *widgets)
{
    gchar message[1024];
    gssize size;
    const gchar *timestamp;
    gchar *target;

    g_print("Received Connection from client!\n");

    // Buffer is not null terminated.
    GInputStream * istream = g_io_stream_get_input_stream (G_IO_STREAM(connection));
    size = g_input_stream_read (istream, message, 1024, NULL, NULL);
    message[size] = 0; // null terminator

    g_print("Message was: \"%s\"\n", message);

    updateClock(widgets);
    timestamp = gtk_label_get_text(GTK_LABEL(widgets->timestamp));
    target = "mars-alpha";
    echo_message(widgets, timestamp, target, "<-", message);

    return FALSE;
}

// Low level networking code.
// Open a UDP network socket, send packet, then close socket.
static int
send_message (targetData target, char *buffer)
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

    // Networking target
    hostname = target.address;
    portno = target.port;
    // portno = atoi("8400");
    // sethostname(myhostname,STRSIZE);

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
    sprintf(buf, "%s", buffer);

    /* Send the packet */
    serverlen = sizeof(serveraddr);
    n = sendto(sockfd, buf, strlen(buf), 0,
               (struct sockaddr *) &serveraddr, serverlen);
    if (n < 0)
        fprintf(stderr,"ERROR in sendto");

    // DEBUG
    g_printerr("[DEBUG] Packet sent: %s %s:%d\n",
               target.name,
               target.address,
               target.port);
    return 0;
}

//////////////////////////////////////////////////////////////////////////////
// callback functions

static void
clickedSend(GtkButton *button,
            appWidgets *widgets)
{
    const gchar *timestamp;
    // gchar *target;

    GtkTextIter start_iter,end_iter;
    gchar *text;
    GtkTextIter messagesIter;

    updateClock(widgets);

    timestamp = gtk_label_get_text(GTK_LABEL(widgets->timestamp));

    //target = "mars-alpha";

    gtk_text_buffer_get_start_iter(GTK_TEXT_BUFFER(widgets->messageTextBuffer), &start_iter);
    gtk_text_buffer_get_end_iter(GTK_TEXT_BUFFER(widgets->messageTextBuffer), &end_iter);
    text = gtk_text_buffer_get_text(GTK_TEXT_BUFFER(widgets->messageTextBuffer), &start_iter, &end_iter, TRUE);

    // FIXME: target is currently a global structure
    send_message(target, text);
    echo_message(widgets, timestamp, target.name , "->", text);
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
    send_message(target, "[OVER]\n");
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
    GVariant *targetSetting;

    GError *error = NULL;

    g_print("[DEBUG] main()\n");
    gtk_init (0, NULL);

    // Settings
    gsettings = g_settings_new("org.mawsonlakes.messages");

    portSetting = g_settings_get_value (gsettings, "port");
    gUDPPort = atoi(g_variant_print(portSetting,FALSE));
    g_print("[DEBUG] - Waiting for UDP packets on port %d (from gSettings)\n", gUDPPort);

    targetSetting = g_settings_get_value (gsettings, "target");
    g_print("[DEBUG] - Default message target: (from gSettings)\n");

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

    anyAddr = g_inet_address_new_any(G_SOCKET_FAMILY_IPV4);
    gsAddr = g_inet_socket_address_new(anyAddr, gUDPPort);

    // Bind address to socket
    g_socket_bind(gSock, gsAddr, TRUE, &error);
    if (error != NULL) {
        g_error("g_socket_bind() => %s", error->message);
        g_clear_error(&error);
        exit(EXIT_FAILURE);
    }

    // Create and add socket to gmain loop for UDP service.
    gSource = g_socket_create_source (gSock, G_IO_IN, NULL);
    g_source_set_callback (gSource,
                           (GSourceFunc) cb_udp_request_handler,
                           // its really a GSocketSourceFunc
                           &widgets,
                           NULL);

    widgets->gSourceId = gSourceId = g_source_attach (gSource, NULL);

////
    send_message (target, "ping");

    g_print("[DEBUG] - Read GTK builder file\n");
    builder = gtk_builder_new_from_file ("../glade/messages.glade");

    g_print("[DEBUG] - Get pointers to GTK Objects\n");
    window = gtk_builder_get_object (builder , "window");
    widgets->messagesTextView   = GTK_WIDGET(gtk_builder_get_object(builder, "messagesTextView"));
    widgets->messagesTextBuffer = GTK_TEXT_BUFFER(gtk_builder_get_object(builder, "messagesTextBuffer"));
    widgets->messageTextView    = GTK_WIDGET(gtk_builder_get_object(builder, "messageTextView"));
    widgets->messageTextBuffer  = GTK_TEXT_BUFFER(gtk_builder_get_object(builder, "messageTextBuffer"));
    widgets->timestamp          = GTK_WIDGET(gtk_builder_get_object(builder, "timestamp"));
    widgets->target             = GTK_WIDGET(gtk_builder_get_object(builder, "target"));
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
    g_print("[DEBUG] Display welcome message\n");
    echo_line(widgets, "Welcome to Messages - an Interplanetary Messaging App!");
    echo_line(widgets, "------------------------------------------------------");
    echo_line(widgets, "This program is designed to send and receive messages with");
    echo_line(widgets, "plain UDP packets. Send a message from textbox at the bottom..");
    echo_line(widgets, "");
    char buf[80];
    sprintf(buf, "Listening for messages on all network interfaces on UDP port %d.", gUDPPort);
    echo_line(widgets, buf);
    echo_line(widgets, "");


    echo_line(widgets,"[Mon,  5 Apr 2021] +0930");
    echo_message(widgets, "--:--:--", "local", "--", "System restart");

    gtk_text_buffer_set_text (messageTextBuffer, "", -1);

    gtk_main ();

return 0;
}
