// The following was taken from
// https://wiki.gnome.org/Projects/JsonGlib

#include <json-glib/json-glib.h>
#include <json-glib/json-gobject.h>

#define FOO_TYPE_OBJECT (foo_object_get_type ())

/* usual GObject boilerplate */

typedef enum { /*< prefix=FOO_BLAH >*/
    FOO_BLAH_ALPHA,
    FOO_BLAH_BRAVO,
    FOO_BLAH_CHARLIE
} FooBlahEnum;

static const gchar *foo_object_json = "
{
\"bar\"  : 42,
\"baz\"  : true,
        \"blah\" : \"bravo\"
}
";

int
main (int   argc,
      char *argv[])
{
  FooObject *foo;
  gint bar_int;
  gboolean baz_boolean;
  FooBlahEnum blah_enum;
  GError *error;

  error = NULL;
  foo = json_construct_gobject (FOO_TYPE_OBJECT, foo_object_json, -1, &error);
  if (error)
    g_error ("Unable to create instance: %s", error->message);

  /* FooObject has three properties: bar, blah and baz */
  g_object_get (G_OBJECT (foo),
                "bar", &bar_int,
                "baz", &baz_boolean,
                "blah", &blah_enum,
                NULL);

  g_object_unref (foo);

  return EXIT_SUCCESS;
}
