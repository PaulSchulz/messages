// router

// An application to implement store and forward routing of UDP messages,
// including processing of packets. eg. adding delay.

// Required for networking code with GNU libc. Is not portable to other libc.
#include <errno.h>

// GLib headers
#include <glib.h>
//#include <gmodule.h>

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

        //g_date_set_julian (date, data->timein/1000000/(24*3600));
        //g_date_set_julian (date, 1618929470);
        g_print("timein:  %8ld  \n", data->timein/1000000);
        //datetime = g_date_time_new_now_utc ();
        datetime = g_date_time_new_from_unix_local (data->timein/1000000);
        //g_print("%02d:%02d:%04d | ", datetime->day, datetime->month, datetime->year);
        gchar *str = g_date_time_format (datetime, "%Y/%m/%d %H:%M:%S %z");
        g_print("%s | ", str);
        g_free(str);
        g_print("%s\n", data->message);

        g_date_time_unref(datetime);
    }
}

int
main (int    argc,
      char **argv)
{
    RtData *data = NULL;

    GQueue *queue;
    queue = g_queue_new();

    data = g_slice_alloc(sizeof(RtData));
    data->timein = g_get_real_time();
    data->message = g_strdup ("first");
    g_queue_push_tail (queue, data);

    data = g_slice_alloc(sizeof(RtData));
    data->timein = g_get_real_time();
    data->message = g_strdup ("second");
    g_queue_push_tail (queue, data);

    g_print("Messages Queued\n");

    g_print("Display Message Queued\n");
    rt_queue_display(queue);

    g_print("Remove first element\n");
    data = g_queue_pop_head(queue);
    // g_free(data);

    g_print("Display Message Queued\n");
    rt_queue_display(queue);

    g_print("Free list and it's elements\n");
    // g_slist_free_full(list, g_free);
}
