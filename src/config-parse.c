// The following was taken from
// https://wiki.gnome.org/Projects/JsonGlib

#include <stdlib.h>
#include <glib-object.h>
#include <json-glib/json-glib.h>

int
main (int argc, char *argv[])
{
  JsonParser *parser;
  JsonNode *root;
  GError *error;

  if (argc < 2)
  {
      g_print ("Usage: ./config-parse <filename.json>\n");
      return EXIT_FAILURE;
  }

  parser = json_parser_new ();

  error = NULL;
  json_parser_load_from_file (parser, argv[1], &error);
  if (error)
    {
      g_print ("Unable to parse `%s': %s\n", argv[1], error->message);
      g_error_free (error);
      g_object_unref (parser);
      return EXIT_FAILURE;
    }

  root = json_parser_get_root (parser);

  // manipulate the object tree and then exit */
  JsonReader *reader = json_reader_new (json_parser_get_root (parser));
  json_reader_read_member (reader, "description");
  const char *desc = json_reader_get_string_value (reader);
  json_reader_end_member (reader);

  g_print ("Description: %s\n", desc);

  g_object_unref (parser);

  return EXIT_SUCCESS;
}
