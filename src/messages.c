// messages
// An application for sending and receiving packet based messages

// Notes:
// Using example from:
// - https://skoona.github.io/skn_rpi-display-services/doc/html/cmd_d_s_8c_source.html

// Required for networking code with GNU libc. Is not portable to other libc.
#include <errno.h>

// glib
#include <glib.h>
#include <gio/gio.h>
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

// Data structure for passing Widget pointers to callbacks
typedef struct {
    GtkWidget *messagesTextView;
    GtkTextBuffer *messagesTextBuffer;

    GtkWidget *targetsTextView;
    GtkTextBuffer *targetsTextBuffer;

    GtkWidget *messageTextView;
    GtkWidget *messageTextBuffer;

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

//////////////////////////////////////////////////////////////////////////////
// Low level UI function

void
echo_line (appWidgets *widgets, gchar *line)
{
    GtkTextBuffer *messages;
    GtkTextIter messagesIter;

    // DEBUG: printf("%s\n", line);
    gtk_text_buffer_get_end_iter(GTK_TEXT_BUFFER(widgets->messagesTextBuffer),&messagesIter);
    gtk_text_buffer_insert (GTK_TEXT_BUFFER(widgets->messagesTextBuffer), &messagesIter, line, -1);
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
    sprintf(text, "%-8s  %-12s  %-2s  %s", timestamp, target, direction, message);

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
        value = "G_IO_IN: There is data to read.";
        break;
    case G_IO_OUT:
        value = "G_IO_OUT: Data can be written (without blocking).";
        break;
    case G_IO_PRI:
        value = "G_IO_PRI: There is urgent data to read.";
        break;
    case G_IO_ERR:
        value = "G_IO_ERR: Error condition.";
        break;
    case G_IO_HUP:
        value = "G_IO_HUP: Hung up (the connection has been broken, usually for pipes and sockets).";
        break;
    case G_IO_NVAL:
        value = "G_IO_NVAL: Invalid request. The file descriptor is not open.";
        break;
    default:
        value = "Unknown GIOCondition!";
    }

    return (value);
 }

// New
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

    g_print("Received UDP packet from client! - Condition: %s\n", skn_gio_condition_to_string(condition));

    if ((condition & G_IO_HUP) || (condition & G_IO_ERR) || (condition & G_IO_NVAL)) {  /* SHUTDOWN THE MAIN LOOP */
        g_message("DisplayService::cb_udp_request_handler(error) G_IO_HUP => %s\n", skn_gio_condition_to_string(condition));
//g_main_loop_quit(pctrl->loop);
        return ( G_SOURCE_REMOVE );
    }

    if (condition != G_IO_IN) {
        g_message("DisplayService::cb_udp_request_handler(error) NOT G_IO_IN => %s\n", skn_gio_condition_to_string(condition));
        return (G_SOURCE_CONTINUE);
    }

    /*
     * If socket times out before reading data any operation will error with 'G_IO_ERROR_TIMED_OUT'.
     */

    gss_receive = g_socket_receive_from (gSock, &gsRmtAddr, widgets->packetData, sizeof(widgets->packetData), NULL, &error);

    if (error != NULL) { // gss_receive = Number of bytes read, or 0 if the connection was closed by the peer, or -1 on error
        g_error("g_socket_receive_from() => %s", error->message);
        g_clear_error(&error);
        return (G_SOURCE_CONTINUE);
    }

    g_print("Get's here.\n");
    /*
      if (gss_receive > 0 ) {
      if (G_IS_INET_SOCKET_ADDRESS(gsRmtAddr) ) {
      gsAddr = g_inet_socket_address_get_address( G_INET_SOCKET_ADDRESS(gsRmtAddr) );
      if ( G_IS_INET_ADDRESS(gsAddr) ) {
      g_object_ref(gsAddr);
      rmtHost = g_resolver_lookup_by_address (pctrl->resolver, gsAddr, NULL, NULL);
      if (NULL == rmtHost) {
      rmtHost = g_inet_address_to_string ( gsAddr);
      }
      }
      }
      pctrl->ch_read[gss_receive] = 0;
g_snprintf(pctrl->ch_request, sizeof(pctrl->ch_request), "[%s]MSG From=%s, Msg=%s", stamp, rmtHost, pctrl->ch_read);
g_free(rmtHost);
        g_snprintf(pctrl->ch_response, sizeof(pctrl->ch_response), "%d %s", 202, "Accepted");
    } else {
        g_snprintf(pctrl->ch_request, sizeof(pctrl->ch_request), "%s", "Error: Input not Usable");
        g_snprintf(pctrl->ch_response, sizeof(pctrl->ch_response), "%d %s", 406, "Not Acceptable");
    }

    g_socket_send_to (gSock, gsRmtAddr, pctrl->ch_response, strlen(pctrl->ch_response), NULL, &error);
    if (error != NULL) {  // gss_send = Number of bytes written (which may be less than size ), or -1 on error
        g_error("g_socket_send_to() => %s", error->message);
        g_clear_error(&error);
    }

    g_free(stamp);
    g_print("%s\n", pctrl->ch_request);

    if ( G_IS_INET_ADDRESS(gsAddr) )
        g_object_unref(gsAddr);

    if (G_IS_INET_SOCKET_ADDRESS(gsRmtAddr) )
        g_object_unref(gsRmtAddr);

*/

    // updateClock(widgets);

    g_print("Get's here2.\n");

    // timestamp = gtk_label_get_text(GTK_LABEL(widgets->timestamp));
    timestamp = "now()";
    target = "mars-alpha";

    g_print("Get's here3.\n");

    echo_message(widgets, timestamp, target, "<-", widgets->packetData);

    g_print("Received UDP packet from client! %ld bytes\n", gss_receive);
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
    gchar *target;

    g_print("Received UDP packet from client!\n");

    // Buffer is not null terminated.
    g_io_channel_read_chars (source,message,1024,&size,NULL);
    g_io_channel_flush (source,NULL);

    printf("Get's here\n");

    message[size] = 0; // null terminator

    g_print("Message was: \"%s\"\n", message);

    updateClock(widgets);
    timestamp = gtk_label_get_text(GTK_LABEL(widgets->timestamp));
    target = "mars-alpha";
    echo_message(widgets, timestamp, target, "<-", message);

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

static int
send_message (char *target, char *buffer)
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
    hostname = "cashew.lan";
    portno = atoi("8400");
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

    // build the server's Internet address
    bzero((char *) &serveraddr, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    bcopy((char *)server->h_addr,
          (char *)&serveraddr.sin_addr.s_addr, server->h_length);
    serveraddr.sin_port = htons(portno);

    // build message
    sprintf(buf, "%s", buffer);

    /* send the message to the server */
    serverlen = sizeof(serveraddr);
    n = sendto(sockfd, buf, strlen(buf), 0,
               (struct sockaddr *) &serveraddr, serverlen);
    if (n < 0)
        fprintf(stderr,"ERROR in sendto");

    return 0;
}

//////////////////////////////////////////////////////////////////////////////
// callback functions

static void
clickedSend(GtkButton *button,
            appWidgets *widgets)
{
    const gchar *timestamp;
    gchar *target;

    GtkTextIter start_iter,end_iter;
    gchar *text;
    GtkTextIter messagesIter;

    updateClock(widgets);

    timestamp = gtk_label_get_text(GTK_LABEL(widgets->timestamp));

    target = "mars-alpha";

    gtk_text_buffer_get_start_iter(GTK_TEXT_BUFFER(widgets->messageTextBuffer), &start_iter);
    gtk_text_buffer_get_end_iter(GTK_TEXT_BUFFER(widgets->messageTextBuffer), &end_iter);
    text = gtk_text_buffer_get_text(GTK_TEXT_BUFFER(widgets->messageTextBuffer), &start_iter, &end_iter, TRUE);

    send_message("", text);
    echo_message(widgets, timestamp, target, "->", text);
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

    send_message("", "[OVER]\n");
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

    appWidgets *widgets = g_slice_new(appWidgets);

    gtk_init (0, NULL);

    //////
    GError *error = NULL;

    /* create the new socketservice */
    GSocketService * service = g_socket_service_new ();

    /* connect to the port */
    g_socket_listener_add_inet_port (G_SOCKET_LISTENER(service),
                                    8400, /* your port goes here */
                                    NULL,
                                    &error);

    g_signal_connect (service, "incoming", G_CALLBACK (incoming_callback), widgets);

/* start the socket service */
    g_socket_service_start (service);
    g_print ("Waiting for TCP client!\n");

///// New UDP Code /////
    guint16 gUDPPort = 8400;
//GError *error;
    GSocket *gSock;
    GInetAddress *anyAddr;
    GSocketAddress *gsAddr;
    GSource *gSource;
    guint gSourceId;

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

    g_socket_bind(gSock, gsAddr, TRUE, &error);
    if (error != NULL) {
        g_error("g_socket_bind() => %s", error->message);
        g_clear_error(&error);
        exit(EXIT_FAILURE);
    }

/* Create and Add socket to gmain loop for service (i.e. polling socket)   */
    gSource = g_socket_create_source (gSock, G_IO_IN, NULL);
    g_source_set_callback (gSource,
                           (GSourceFunc) cb_udp_request_handler,
                           &widgets, NULL); // its really a GSocketSourceFunc
    widgets->gSourceId = gSourceId = g_source_attach (gSource, NULL);

    //  g_socket_service_start (serviceUDP);
    g_print ("Waiting for UDP client!\n");

  ////
    send_message ("ping\n","cashew.lan:8400");

    builder = gtk_builder_new_from_file ("../glade/messages.glade");
    window = gtk_builder_get_object (builder , "window");

    widgets->messagesTextView   = GTK_WIDGET(gtk_builder_get_object(builder, "messagesTextView"));
    widgets->messagesTextBuffer = GTK_TEXT_BUFFER(gtk_builder_get_object(builder, "messagesTextBuffer"));
    widgets->targetsTextView    = GTK_WIDGET(gtk_builder_get_object(builder, "targetsTextView"));
    widgets->targetsTextBuffer  = GTK_TEXT_BUFFER(gtk_builder_get_object(builder, "targetsTextBuffer"));
    widgets->messageTextView    = GTK_WIDGET(gtk_builder_get_object(builder, "messageTextView"));
    widgets->messageTextBuffer  = GTK_WIDGET(gtk_builder_get_object(builder, "messageTextBuffer"));
    widgets->timestamp          = GTK_WIDGET(gtk_builder_get_object(builder, "timestamp"));
    widgets->target             = GTK_WIDGET(gtk_builder_get_object(builder, "target"));
    widgets->rtt                = GTK_WIDGET(gtk_builder_get_object(builder, "rtt"));
    widgets->statusIcon         = GTK_WIDGET(gtk_builder_get_object(builder, "statusIcon"));
    widgets->statusLabel        = GTK_WIDGET(gtk_builder_get_object(builder, "statusLabel"));
    widgets->send               = GTK_WIDGET(gtk_builder_get_object(builder, "send"));
    widgets->over               = GTK_WIDGET(gtk_builder_get_object(builder, "over"));

    messagesTextBuffer = (GtkTextBuffer*)gtk_builder_get_object (builder, "messagesTextBuffer");
    targetsTextBuffer  = (GtkTextBuffer*)gtk_builder_get_object (builder, "targetsTextBuffer");
    messageTextBuffer  = (GtkTextBuffer*)gtk_builder_get_object (builder, "messageTextBuffer");

    // gtk_builder_connect_signals (builder, widgets);
    g_signal_connect (window,       "destroy", G_CALLBACK (close_window), NULL);
    g_signal_connect (widgets->send, "clicked", G_CALLBACK (clickedSend),  widgets);
    g_signal_connect (widgets->over, "clicked", G_CALLBACK (clickedOver),  widgets);

    gtk_widget_show_all (GTK_WIDGET (window));

    gtk_text_buffer_set_text (messagesTextBuffer, "", -1);

    // Splash
    echo_line(widgets, "Welcome to Messages!");
    echo_line(widgets, "--------------------");
    echo_line(widgets, "This program is designed to send and receive messages in");
    echo_line(widgets, "plain UDP packets.");
    echo_line(widgets, "Select a target, and send a message.");
    echo_line(widgets, "");

    echo_line(widgets,"[Mon,  5 Apr 2021] +0930");
    echo_message(widgets, "00:00:00", "mars-alpha", "->", "How is the potato experiment going?");
    echo_message(widgets, "00:00:00", "local",      "--", "System restart");

    gtk_text_buffer_set_text (targetsTextBuffer, "", -1);
    gtk_text_buffer_get_end_iter (targetsTextBuffer, &targetsIter);
    gtk_text_buffer_insert (
        targetsTextBuffer,
        &targetsIter,
        "Name          Address:Port        Receive Action    Transmit Action   RTT(Ping)   ErrorRate  Jitter\n",
        -1);
    gtk_text_buffer_insert (
        targetsTextBuffer,
        &targetsIter,
        "----------    ----------------    --------------    ---------------   ----------  --------  -------\n",
        -1);
    gtk_text_buffer_insert (
        targetsTextBuffer,
        &targetsIter,
        "local         localhost:8400      echo              send\n",
        -1);
    gtk_text_buffer_insert (
        targetsTextBuffer,
        &targetsIter,
        "mars-alpha    mars-alpha:8400     echo              send-and-echo     7m30s(10s)\n",
        -1);
    gtk_text_buffer_insert (
        targetsTextBuffer,
        &targetsIter,
        "au            au.gateway:8400     echo              send-and-echo     98ms(10s)\n",
        -1);

    gtk_text_buffer_set_text (messageTextBuffer, "", -1);

    gtk_main ();

return 0;
}
