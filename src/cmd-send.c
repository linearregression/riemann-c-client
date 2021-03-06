/* riemann-c-client -- Riemann C client library
 * Copyright (C) 2013, 2014, 2015  Gergely Nagy <algernon@madhouse-project.org>
 *
 * This library is free software: you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation, either version 3 of
 * the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

static void
help_send (void)
{
  printf ("Sending events (send command):\n"
          "==============================\n"
          "\n"
          " Options:\n"
          "  -s, --state=STATE                 Set the state of the event.\n"
          "  -S, --service=SERVICE             Set the service sending the event.\n"
          "  -h, --host=HOST                   Set the origin host of the event.\n"
          "  -D, --description=DESCRIPTION     Set the description of the event.\n"
          "  -a, --attribute=KEY=VALUE         Add a new attribute to the event.\n"
          "  -t, --tag=TAG                     Add a tag to the event.\n"
          "  -i, --metric-sint64=METRIC        Set the 64bit integer metric of the event.\n"
          "  -d, --metric-d=METRIC             Set the double metric of the event.\n"
          "  -f, --metric-f=METRIC             Set the float metric of the event.\n"
          "  -L, --ttl=TTL                     Set the TTL of the event.\n"
          "\n"
          "  -T, --tcp                         Send the message over TCP (default).\n"
          "  -U, --udp                         Send the message over UDP.\n"
          "  -G, --tls                         Send the message over TLS.\n"
          "  -o, --option option=value         Set a client option to a given value.\n"
          "\n"
          "  -0, --stdin                       Read metric/state from STDIN continuously.\n"
          "\n"
          "  -?, --help                        This help screen.\n");
}

static int
_send_continously (riemann_client_type_t client_type,
                   riemann_client_t *client,
                   riemann_event_t *source_event)
{
  char buffer[1024];
  int e = 0;

  while (fgets (buffer, sizeof (buffer) - 1, stdin) != NULL)
    {
      riemann_message_t *response, *message;
      riemann_event_t *event;
      char *endptr;
      double d;
      long long int l;

      buffer[strlen (buffer) - 1] = '\0';
      if (strlen (buffer) == 0)
        continue;

      event = riemann_event_clone (source_event);

      errno = 0;
      l = strtoll (buffer, &endptr, 10);
      if (((l == 0) && (endptr == buffer)) || (errno == ERANGE) ||
          (endptr[0] != '\0' && endptr[0] != ' '))
        {
          d = strtod (buffer, &endptr);
          if (((d == 0) && (endptr == buffer)) || (errno == ERANGE) ||
              (endptr[0] != '\0' && endptr[0] != ' '))
            riemann_event_set_one (event,
                                   STATE, buffer);
          else
            {
              riemann_event_set_one (event, METRIC_D, (double) d);
              if (endptr[0] != '\0')
                riemann_event_set_one (event, STATE, endptr + 1);
            }
        }
      else
        {
          riemann_event_set_one (event, METRIC_S64, (int64_t) l);
          if (endptr[0] != '\0')
            riemann_event_set_one (event, STATE, endptr + 1);
        }

      message = riemann_message_create_with_events (event, NULL);
      e = riemann_client_send_message_oneshot (client, message);

      if (e != 0)
        break;

      if (client_type == RIEMANN_CLIENT_UDP)
        continue;

      response = riemann_client_recv_message (client);
      if (!response)
        {
          fprintf (stderr, "Error when asking for a message receipt: %s\n",
                   strerror (errno));
          e = -EPROTO;
          break;
        }

      if (response->ok != 1)
        {
          fprintf (stderr, "Message receipt failed: %s\n", response->error);
          riemann_message_free (response);
          e = -EPROTO;
          break;
        }

      riemann_message_free (response);
    }

  riemann_event_free (source_event);
  return e;
}

static int
client_send (int argc, char *argv[])
{
  riemann_event_t *event;
  riemann_message_t *response;
  riemann_client_t *client;
  riemann_client_type_t client_type = RIEMANN_CLIENT_TCP;
  const char *host = "localhost";
  int port = 5555, c, e, exit_status = EXIT_SUCCESS;
  struct
  {
    char *cafn;
    char *certfn;
    char *keyfn;
  } tls = {NULL, NULL, NULL};
  int stdin = 0;

  event = riemann_event_new ();

  riemann_event_set_one (event, ATTRIBUTES,
                         riemann_attribute_create ("x-client", "riemann-c-client"),
                         NULL);

  while (1)
    {
      int option_index = 0;
      static struct option long_options[] = {
        {"state", required_argument, NULL, 's'},
        {"service", required_argument, NULL, 'S'},
        {"host", required_argument, NULL, 'h'},
        {"description", required_argument, NULL, 'D'},
        {"tag", required_argument, NULL, 't'},
        {"attribute", required_argument, NULL, 'a'},
        {"metric-sint64", required_argument, NULL, 'i'},
        {"metric-d", required_argument, NULL, 'd'},
        {"metric-f", required_argument, NULL, 'f'},
        {"tcp", no_argument, NULL, 'T'},
        {"udp", no_argument, NULL, 'U'},
        {"tls", no_argument, NULL, 'G'},
        {"ttl", required_argument, NULL, 'L'},
        {"option", required_argument, NULL, 'o'},
        {"stdin", no_argument, NULL, '0'},
        {"help", no_argument, NULL, '?'},
        {"version", no_argument, NULL, 'V'},
        {NULL, 0, NULL, 0}
      };

      c = getopt_long (argc, argv, "s:S:h:D:a:t:i:d:f:?VUTGL:o:0",
                       long_options, &option_index);

      if (c == -1)
        break;

      switch (c)
        {
        case 's':
          riemann_event_set_one (event, STATE, optarg);
          break;

        case 'S':
          riemann_event_set_one (event, SERVICE, optarg);
          break;

        case 'h':
          riemann_event_set_one (event, HOST, optarg);
          break;

        case 'D':
          riemann_event_set_one (event, DESCRIPTION, optarg);
          break;

        case 'a':
          {
            char *key = optarg, *value;

            value = strchr (optarg, '=');
            if (!value)
              {
                fprintf (stderr, "Invalid attribute specification: %s\n", optarg);
                return EXIT_FAILURE;
              }
            value[0] = '\0';
            value++;

            riemann_event_attribute_add
              (event, riemann_attribute_create (key, value));

            break;
          }

        case 't':
          riemann_event_tag_add (event, optarg);
          break;

        case 'i':
          riemann_event_set_one (event, METRIC_S64, (int64_t) atoll (optarg));
          break;

        case 'd':
          riemann_event_set_one (event, METRIC_D, (double) atof (optarg));
          break;

        case 'f':
          riemann_event_set_one (event, METRIC_F, (float) atof (optarg));
          break;

        case 'T':
          client_type = RIEMANN_CLIENT_TCP;
          break;

        case 'U':
          client_type = RIEMANN_CLIENT_UDP;
          break;

        case 'G':
          client_type = RIEMANN_CLIENT_TLS;
          break;

        case 'L':
          riemann_event_set_one (event, TTL, (float) atof (optarg));
          break;

        case 'o':
          if (strncmp (optarg, "cafile=", strlen ("cafile=")) == 0)
            tls.cafn = &optarg[strlen ("cafile=")];
          else if (strncmp (optarg, "certfile=", strlen ("certfile=")) == 0)
            tls.certfn = &optarg[strlen ("certfile=")];
          else if (strncmp (optarg, "keyfile=", strlen ("keyfile=")) == 0)
            tls.keyfn = &optarg[strlen ("keyfile=")];
          else
            {
              fprintf (stderr, "Unknown client option: %s\n", optarg);
              return EXIT_FAILURE;
            }

          break;

        case '0':
          stdin = 1;
          break;

        case '?':
          help_display (argv[0], help_send);
          return EXIT_SUCCESS;

        case 'V':
          version_display (0, NULL);
          return EXIT_SUCCESS;

        default:
          fprintf(stderr, "Unknown option: %c\n", c);
          help_display (argv[0], help_send);
          return EXIT_FAILURE;
        }
    }

  optind++;

  if (optind < argc)
    {
      host = argv[optind];

      if (optind + 1 < argc)
        port = atoi (argv[optind + 1]);
    }

  if (argc - optind > 2)
    {
      fprintf (stderr, "Too many arguments!\n");
      help_display (argv[0], help_send);
      return EXIT_FAILURE;
    }

  client = riemann_client_create
    (client_type, host, port,
     RIEMANN_CLIENT_OPTION_TLS_CA_FILE, tls.cafn,
     RIEMANN_CLIENT_OPTION_TLS_CERT_FILE, tls.certfn,
     RIEMANN_CLIENT_OPTION_TLS_KEY_FILE, tls.keyfn,
     RIEMANN_CLIENT_OPTION_NONE);
  if (!client)
    {
      fprintf (stderr, "Unable to connect: %s\n", (char *)strerror (errno));
      exit_status = EXIT_FAILURE;
      goto end;
    }

  if (stdin)
    {
      e = _send_continously (client_type, client, event);

      if (e != 0)
        {
          fprintf (stderr, "Error sending message: %s\n", (char *)strerror (-e));
          exit_status = EXIT_FAILURE;
          goto end;
        }
    }
  else
    {
      e = riemann_client_send_message_oneshot
        (client, riemann_message_create_with_events (event, NULL));
      if (e != 0)
        {
          fprintf (stderr, "Error sending message: %s\n", (char *)strerror (-e));
          exit_status = EXIT_FAILURE;
          goto end;
        }

      if (client_type == RIEMANN_CLIENT_UDP)
        goto end;

      response = riemann_client_recv_message (client);
      if (!response)
        {
          fprintf (stderr, "Error when asking for a message receipt: %s\n",
                   strerror (errno));
          exit_status = EXIT_FAILURE;
          goto end;
        }

      if (response->ok != 1)
        {
          fprintf (stderr, "Message receipt failed: %s\n", response->error);
          exit_status = EXIT_FAILURE;
        }

      riemann_message_free (response);
    }

 end:
  riemann_client_free (client);

  return exit_status;
}
