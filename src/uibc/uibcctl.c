
#include "uibcctl.h"
#include <locale.h>
#include <glib.h>
#include <getopt.h>
#include "config.h"
#include "util.h"
//#include "ctl.h"

int portno = -1;
gchar* host;
bool is_daemon = true;

void usage(gchar* prgname) {
  fprintf(stderr, "Usage:\n");
  fprintf(stderr, "   %s <hostname> <port>\n", prgname);
  fprintf(stderr, "or define host and port on ini file\n");
}

/*
 * cmd: quit/exit
 */

//static int cmd_quit(char **args, unsigned int n)
//{
//	cli_exit();
//	return 0;
//}
//
//static const struct cli_cmd cli_cmds[] = {
//	{ "info",	NULL,	CLI_M,	CLI_LESS,	1,	cmd_show,	"Show detailed information" },
//	{ "quit",	NULL,	CLI_Y,	CLI_MORE,	0,	cmd_quit,	"Quit program" },
//	{ "exit",	NULL,	CLI_Y,	CLI_MORE,	0,	cmd_quit,	NULL },
//	{ "event",	NULL,	CLI_Y,	CLI_MORE,	0,	cmd_event,	"launch an event: <type>,<count>,<id>,<x>,<y>" },
//	{ "help",	NULL,	CLI_M,	CLI_MORE,	0,	NULL,		"Print help" },
//	{ },
//};

void cli_fn_help()
{
	/*
	 * 80-char barrier:
	 *      01234567890123456789012345678901234567890123456789012345678901234567890123456789
	 */
	fprintf(stderr, "%s [OPTIONS...] <hostname> <port>\n\n"
	       "Manage the User Input Back Channel.\n"
	       "  -h --help                      Show this help\n"
	       "     --help-commands             Show available commands\n"
	       "     --version                   Show package version\n"
	       "     --daemon                    Run in background\n"
	       "     --log-level <lvl>           Maximum level for log messages\n"
	       "     --host                      Defines the host\n"
	       "  -p --port <port>               Defines the Port\n"
	       "\n"
	       , program_invocation_short_name);
	/*
	 * 80-char barrier:
	 *      01234567890123456789012345678901234567890123456789012345678901234567890123456789
	 */
}

static int parse_argv(int argc, char *argv[])
{
	enum {
		ARG_VERSION = 0x100,
		ARG_LOG_LEVEL,
		ARG_DAEMON,
		ARG_HOST,
		ARG_HELP_COMMANDS,
	};
	static const struct option options[] = {
		{ "help",		no_argument,		NULL,	'h' },
		{ "help-commands",	no_argument,		NULL,	ARG_HELP_COMMANDS },
		{ "version",		no_argument,		NULL,	ARG_VERSION },
		{ "log-level",		required_argument,	NULL,	ARG_LOG_LEVEL },
		{ "daemon",		no_argument,		NULL,	ARG_DAEMON },
		{ "host",		required_argument,	NULL,	ARG_HOST },
		{ "port",		required_argument,	NULL,	'p' },
		{}
	};
	int c;

	while ((c = getopt_long(argc, argv, "he:p:", options, NULL)) >= 0) {
		switch (c) {
		case 'h':
			cli_fn_help();
			return 0;
		case ARG_HELP_COMMANDS:
//			return cli_help(cli_cmds, 20);
			cli_fn_help();
			return 0;
		case ARG_VERSION:
			puts(PACKAGE_STRING);
			return 0;
		case ARG_LOG_LEVEL:
			log_max_sev = log_parse_arg(optarg);
			break;
		case ARG_HOST:
			host = optarg;
			break;
		case ARG_DAEMON:
			is_daemon = true;
			break;
		case 'p':
			portno = atoi(optarg);
			break;
		case '?':
			return -EINVAL;
		}
	}

	return 1;
}
int main(int argc, char *argv[]) {
  bool free_host = false;
  struct hostent *server;
  int sockfd;
  struct sockaddr_in serv_addr;
  char buffer[256];
  int r;

  setlocale(LC_ALL, "");

  GKeyFile* gkf = load_ini_file();

  log_max_sev = LOG_INFO;

  gchar** autocmds_free = NULL;
  if (gkf) {
    gchar* log_level;
    log_level = g_key_file_get_string (gkf, "uibcctl", "log-level", NULL);
    if (log_level) {
      log_max_sev = log_parse_arg(log_level);
      g_free(log_level);
    }
    if (g_key_file_has_key (gkf, "uibcctl", "daemon", NULL)) {
      is_daemon = g_key_file_get_boolean (gkf, "uibcctl", "daemon", NULL);
    }
    host = g_key_file_get_string (gkf, "uibcctl", "host", NULL);
    if (host) {
      host = log_parse_arg(log_level);
      free_host = true;
    }
    if (g_key_file_has_key (gkf, "uibcctl", "port", NULL)) {
      portno = g_key_file_get_uint64 (gkf, "uibcctl", "port", NULL);
    }
  }

  r = parse_argv(argc, argv);
  if (r < 0)
    return EXIT_FAILURE;
  if (!r)
    return EXIT_SUCCESS;

  gchar* prgname = argv[0];
  gboolean has_port = portno != -1;
  gboolean has_host = host != NULL;
  if (has_host) {
    if (has_port) {
      //Everything defined
    } else {
      if (argc < 1) {
        usage(prgname);
        return EXIT_FAILURE;
      } else {
        portno = atoi(argv[1]);
      }
    }
  } else {
    if (has_port) {
      if (argc < 1) {
        usage(prgname);
        return EXIT_FAILURE;
      } else {
        host = argv[1];
      }
    } else {
      if (argc < 2) {
        usage(prgname);
        return EXIT_FAILURE;
      } else {
        host = argv[1];
        portno = atoi(argv[1]);
      }
    }
  }

  server = gethostbyname(host);

  log_info("server %s port %d", server, portno);

  if (server == NULL) {
    perror("no such host");
    return EXIT_FAILURE;
  }

  sockfd = socket(AF_INET, SOCK_STREAM, 0);

  if (sockfd < 0) {
    perror("ERROR opening socket");
    return EXIT_FAILURE;
  }

  bzero((char *) &serv_addr, sizeof(serv_addr));
  serv_addr.sin_family = AF_INET;
  bcopy(server->h_addr, (char *)&serv_addr.sin_addr.s_addr, server->h_length);
  serv_addr.sin_port = htons(portno);

  if (connect(sockfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
    perror("ERROR connecting");
    return EXIT_FAILURE;
  }

  //TODO: Add miracle TUI interface

  while(1) {
    if (!is_daemon) {
      printf("enter event <type>,<count>,<id>,<x>,<y>: ");
    }
    bzero(buffer, 256);
    fgets(buffer, 255, stdin);
    if (buffer == NULL) {
      break;
    }
    if (!is_daemon) {
      printf("input: %s", buffer);
    }
    char type = buffer[0];
    UibcMessage uibcmessage;
    if (type == '0' || type == '1') {
      uibcmessage = buildUibcMessage(GENERIC_TOUCH_DOWN, buffer, 1, 1);
    } else if (type == '3' || type == '4') {
      uibcmessage = buildUibcMessage(GENERIC_KEY_DOWN, buffer, 1, 1);
    } else {
      if (!is_daemon) {
        printf("unknow event type: %s", buffer);
      }
      continue;
    }

    r = sendUibcMessage(&uibcmessage, sockfd);
    if (r) {
      return r;
    }
  }

  if (free_host)
    g_free(host);

  close(sockfd);
  return EXIT_SUCCESS;
}

const char *int2binary(int x, int padding)
{
  char *b;
  int min_padding = x > 0 ? floor(log2(x)) : 0;
  if (padding < min_padding) {
    padding = min_padding;
  }

  b = (char *)malloc (sizeof (char *) * padding + 1);
  strcpy(b, "");

  int z;
  for (z = pow(2,padding); z > 0; z >>= 1) {
    strcat(b, ((x & z) == z) ? "1" : "0");
  }

  return b;
}

int sendUibcMessage(UibcMessage* uibcmessage, int sockfd) {
  ssize_t n;

  printf("sending %zu bytes\n", uibcmessage->m_PacketDataLen);

  n = write(sockfd, uibcmessage->m_PacketData , uibcmessage->m_PacketDataLen);

  if (n < 0) {
    perror("ERROR writing to socket");
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}

UibcMessage buildUibcMessage(MessageType type,
    const char* inEventDesc,
    double widthRatio,
    double heightRatio) {
  UibcMessage uibcmessage;
  uibcmessage.m_PacketData = NULL;
  uibcmessage.m_PacketDataLen = 0;
  uibcmessage.m_DataValid = false;

  switch (type) {
    case GENERIC_TOUCH_DOWN:
    case GENERIC_TOUCH_UP:
    case GENERIC_TOUCH_MOVE:
      getUIBCGenericTouchPacket(inEventDesc,
          &uibcmessage,
          widthRatio,
          heightRatio);
      break;

    case GENERIC_KEY_DOWN:
    case GENERIC_KEY_UP:
      getUIBCGenericKeyPacket(inEventDesc, &uibcmessage);
      break;

    case GENERIC_ZOOM:
      getUIBCGenericZoomPacket(inEventDesc, &uibcmessage);
      break;

    case GENERIC_VERTICAL_SCROLL:
    case GENERIC_HORIZONTAL_SCROLL:
      getUIBCGenericScalePacket(inEventDesc, &uibcmessage);
      break;

    case GENERIC_ROTATE:
      getUIBCGenericRotatePacket(inEventDesc, &uibcmessage);
      break;
  };
  return uibcmessage;
}


// format: "typeId, number of pointers, pointer Id1, X coordnate, Y coordinate, pointer Id2, X coordnate, Y coordnate,..."
void getUIBCGenericTouchPacket(const char *inEventDesc,
    UibcMessage* uibcmessage,
    double widthRatio,
    double heightRatio) {
  log_info("getUIBCGenericTouchPacket (%s)", inEventDesc);
  char* outData;
  int32_t typeId = 0;
  int32_t numberOfPointers = 0;
  size_t uibcBodyLen = 0;
  int32_t genericPacketLen = 0;
  int32_t temp;
  size_t size;

  char** splitedStr = str_split((char*)inEventDesc, ",", &size);

  if ((int)size - 5 < 0 || ((int)size - 5) % 3 != 0) {
    log_error("getUIBCGenericTouchPacket (%s)", "bad input event");
    return;
  }
  int offset_split = 0;
  typeId = atoi(*(splitedStr + offset_split++));
  numberOfPointers = atoi(*(splitedStr + offset_split++));

  genericPacketLen = numberOfPointers * 5 + 1;
  uibcBodyLen = genericPacketLen + 7; // Generic header length = 7
  //Padding to even number
  uibcBodyLen = (uibcBodyLen % 2 == 0) ? uibcBodyLen : (uibcBodyLen + 1);

  outData = (char*)malloc(uibcBodyLen);
  // UIBC header Octets
  int offset_header = 0;
  //Version (3 bits),T (1 bit),Reserved(8 bits),Input Category (4 bits)
  outData[offset_header++] = 0x00; // 000 0 0000
  outData[offset_header++] = 0x00; // 0000 0000

  //Length(16 bits)
  outData[offset_header++] = (uibcBodyLen >> 8) & 0xFF; // first 8 bytes
  outData[offset_header++] = uibcBodyLen & 0xFF; // last 8 bytes

  //Generic Input Body Format
  outData[offset_header++] = typeId & 0xFF; // Tyoe ID, 1 octet

  // Length, 2 octets
  outData[offset_header++] = (genericPacketLen >> 8) & 0xFF; // first 8 bytes
  outData[offset_header++] = genericPacketLen & 0xFF; //last 8 bytes

  // Number of pointers, 1 octet
  outData[offset_header++] = numberOfPointers & 0xFF; 

  int offset = 0;
  log_info("getUIBCGenericTouchPacket numberOfPointers=[%d]\n", numberOfPointers);
  for (int i = 0; i < numberOfPointers; i++) {
    int splitoffset = offset_split + (i * 3);
    temp = atoi(*(splitedStr + splitoffset++));
    offset = offset_header + ( i * 5);
    outData[offset++] = temp & 0xFF;
    log_info("getUIBCGenericTouchPacket PointerId=[%d]\n", temp);

    temp = atoi(*(splitedStr + splitoffset++));
    temp = (int32_t)((double)temp / widthRatio);
    log_info("getUIBCGenericTouchPacket X-coordinate=[%d]\n", temp);
    outData[offset++] = (temp >> 8) & 0xFF;
    outData[offset++] = temp & 0xFF;

    temp = atoi(*(splitedStr + splitoffset++));
    temp = (int32_t)((double)temp / heightRatio);
    log_info("getUIBCGenericTouchPacket Y-coordinate=[%d]\n", temp);
    outData[offset++] = (temp >> 8) & 0xFF;
    outData[offset++] = temp & 0xFF;
  }

  while (offset <= uibcBodyLen) {
    outData[offset++] = 0x00;
  }

  for (int i = 0; i < size; i++) {
    free(*(splitedStr + i));
  }
  free(splitedStr);

  binarydump(outData, uibcBodyLen);
  uibcmessage->m_DataValid = true;
  uibcmessage->m_PacketData = outData;
  uibcmessage->m_PacketDataLen = uibcBodyLen;
}

void hexdump(void *_data, size_t len)
{
  unsigned char *data = _data;
  size_t count;

  int line = 15;
  for (count = 0; count < len; count++) {
    if ((count & line) == 0) {
      fprintf(stderr,"%04zu: ", count);
    }
    fprintf(stderr,"%02x ", *data);
    data++;
    if ((count & line) == line) {
      fprintf(stderr,"\n");
    }
  }
  if ((count & line) != 0) {
    fprintf(stderr,"\n");
  }
}

void binarydump(void *_data, size_t len)
{
  unsigned char *data = _data;
  size_t count;

  int line = 7;
  for (count = 0; count < len; count++) {
    if ((count & line) == 0) {
      fprintf(stderr,"%04zu: ", count);
    }
    fprintf(stderr,"%s ", int2binary(*data, 8));
    data++;
    if ((count & line) == line) {
      fprintf(stderr,"\n");
    }
  }
  if ((count & line) != 0) {
    fprintf(stderr,"\n");
  }
}
// format: "typeId, Key code 1(0x00), Key code 2(0x00)"
void getUIBCGenericKeyPacket(const char *inEventDesc,
    UibcMessage* uibcmessage) {
  log_info("getUIBCGenericKeyPacket (%s)", inEventDesc);
  char* outData = uibcmessage->m_PacketData;
  int32_t typeId = 0;
  int32_t uibcBodyLen = 0;
  int32_t genericPacketLen = 0;
  int32_t temp = 0;
  size_t size;
  char** splitedStr = str_split((char*)inEventDesc, ",", &size);

  if (size > 0) {

    if (((int)size) % 3 != 0) {
        log_error("getUIBCGenericKeyPacket (%s)", "bad input event");
        return;
    }
    int i;
    for (i = 0; i < size; i++) {
      log_info("getUIBCGenericKeyPacket splitedStr tokens=[%s]\n", *(splitedStr + i));

      switch (i) {
        case 0: {
                  typeId = atoi(*(splitedStr + i));
                  log_info("getUIBCGenericKeyPacket typeId=[%d]\n", typeId);
                  genericPacketLen = 5;
                  uibcBodyLen = genericPacketLen + 7; // Generic header length = 7
                  outData = (char*)malloc(uibcBodyLen + 1);
                  // UIBC header
                  outData[0] = 0x00; //Version (3 bits),T (1 bit),Reserved(8 bits),Input Category (4 bits)
                  outData[1] = 0x00; //Version (3 bits),T (1 bit),Reserved(8 bits),Input Category (4 bits)
                  outData[2] = (uibcBodyLen >> 8) & 0xFF; //Length(16 bits)
                  outData[3] = uibcBodyLen & 0xFF; //Length(16 bits)
                  //Generic Input Body Format
                  outData[4] = typeId & 0xFF; // Tyoe ID, 1 octet
                  outData[5] = (genericPacketLen >> 8) & 0xFF; // Length, 2 octets
                  outData[6] = genericPacketLen & 0xFF; // Length, 2 octets
                  outData[7] = 0x00; // reserved
                  break;
                }
        case 1: {
                  sscanf(*(splitedStr + i), " 0x%04X", &temp);
                  if (temp == 0) {
                    outData[8] = 0x00;
                    outData[9] = 0x00;
                  }
                  log_info("getUIBCGenericKeyPacket key code 1=[%d]\n", temp);
                  outData[8] = (temp >> 8) & 0xFF;
                  outData[9] = temp & 0xFF;

                  break;
                }
        case 2: {
                  sscanf(*(splitedStr + i), " 0x%04X", &temp);
                  if (temp == 0) {
                    outData[10] = 0x00;
                    outData[11] = 0x00;
                  }
                  log_info("getUIBCGenericKeyPacket key code 2=[%d]\n", temp);
                  outData[10] = (temp >> 8) & 0xFF;
                  outData[11] = temp & 0xFF;

                  break;
                }
        default: {
                 }
                 break;
      }
      free(*(splitedStr + i));
    }
  }
  free(splitedStr);

  binarydump(outData, uibcBodyLen);
  uibcmessage->m_DataValid = true;
  uibcmessage->m_PacketData = outData;
  uibcmessage->m_PacketDataLen = uibcBodyLen;
}

// format: "typeId,  X coordnate, Y coordnate, integer part, fraction part"
void getUIBCGenericZoomPacket(const char *inEventDesc, UibcMessage* uibcmessage) {
  log_info("getUIBCGenericZoomPacket (%s)", inEventDesc);
  char* outData = uibcmessage->m_PacketData;
  int32_t typeId;
  int32_t uibcBodyLen, genericPacketLen;
  int32_t xCoord, yCoord, integerPart, FractionPart;
  size_t size;

  char** splitedStr = str_split((char*)inEventDesc, ",", &size);

  if (splitedStr) {
    int i;
    for (i = 0; * (splitedStr + i); i++) {
      //log_info("getUIBCGenericZoomPacket splitedStr tokens=[%s]\n", *(splitedStr + i));

      switch (i) {
        case 0: {
                  typeId = atoi(*(splitedStr + i));
                  //log_info("getUIBCGenericZoomPacket typeId=[%d]\n", typeId);

                  genericPacketLen = 6;
                  uibcBodyLen = genericPacketLen + 7; // Generic herder leh = 7
                  outData = (char*)malloc(uibcBodyLen + 1);
                  // UIBC header
                  outData[0] = 0x00; //Version (3 bits),T (1 bit),Reserved(8 bits),Input Category (4 bits)
                  outData[1] = 0x00; //Version (3 bits),T (1 bit),Reserved(8 bits),Input Category (4 bits)
                  outData[2] = (uibcBodyLen >> 8) & 0xFF; //Length(16 bits)
                  outData[3] = uibcBodyLen & 0xFF; //Length(16 bits)
                  //Generic Input Body Format
                  outData[4] = typeId & 0xFF; // Tyoe ID, 1 octet
                  outData[5] = (genericPacketLen >> 8) & 0xFF; // Length, 2 octets
                  outData[6] = genericPacketLen & 0xFF; // Length, 2 octets
                  break;
                }

        case 1: {
                  xCoord = atoi(*(splitedStr + i));
                  outData[7] = (xCoord >> 8) & 0xFF;
                  outData[8] = xCoord & 0xFF;
                  log_info("getUIBCGenericZoomPacket xCoord=[%d]\n", xCoord);
                  break;
                }
        case 2: {
                  yCoord = atoi(*(splitedStr + i));
                  log_info("getUIBCGenericZoomPacket yCoord=[%d]\n", yCoord);
                  break;
                }
        case 3: {
                  integerPart = atoi(*(splitedStr + i));
                  outData[11] = integerPart & 0xFF;
                  //log_info("getUIBCGenericZoomPacket integerPart=[%d]\n", integerPart);
                  break;
                }
        case 4: {
                  FractionPart = atoi(*(splitedStr + i));
                  outData[12] = FractionPart & 0xFF;
                  //log_info("getUIBCGenericZoomPacket FractionPart=[%d]\n", FractionPart);

                  break;
                }
        default: {
                   break;
                 }
      }

      free(*(splitedStr + i));
    }
    free(splitedStr);
  }
  //hexdump(outData, uibcBodyLen);
  uibcmessage->m_DataValid = true;
  uibcmessage->m_PacketDataLen = uibcBodyLen;
}

// format: "typeId,  unit, direction, amount to scroll"
void getUIBCGenericScalePacket(const char *inEventDesc, UibcMessage* uibcmessage) {
  log_info("getUIBCGenericScalePacket (%s)", inEventDesc);
  char* outData = uibcmessage->m_PacketData;
  int32_t typeId;
  int32_t uibcBodyLen, genericPacketLen;
  int32_t temp;
  size_t size;
  char** splitedStr = str_split((char*)inEventDesc, ",", &size);

  if (splitedStr) {
    int i;
    for (i = 0; * (splitedStr + i); i++) {
      //log_info("getUIBCGenericScalePacket splitedStr tokens=[%s]\n", *(splitedStr + i));

      switch (i) {
        case 0: {
                  typeId = atoi(*(splitedStr + i));
                  //log_info("getUIBCGenericScalePacket typeId=[%d]\n", typeId);
                  genericPacketLen = 2;
                  uibcBodyLen = genericPacketLen + 7; // Generic herder leh = 7
                  outData = (char*)malloc(uibcBodyLen + 1);
                  // UIBC header
                  outData[0] = 0x00; //Version (3 bits),T (1 bit),Reserved(8 bits),Input Category (4 bits)
                  outData[1] = 0x00; //Version (3 bits),T (1 bit),Reserved(8 bits),Input Category (4 bits)
                  outData[2] = (uibcBodyLen >> 8) & 0xFF; //Length(16 bits)
                  outData[3] = uibcBodyLen & 0xFF; //Length(16 bits)
                  //Generic Input Body Format
                  outData[4] = typeId & 0xFF; // Tyoe ID, 1 octet
                  outData[5] = (genericPacketLen >> 8) & 0xFF; // Length, 2 octets
                  outData[6] = genericPacketLen & 0xFF; // Length, 2 octets
                  outData[7] = 0x00; // Clear the byte
                  outData[8] = 0x00; // Clear the byte
                  /*
                     B15B14; Scroll Unit Indication bits.
                     0b00; the unit is a pixel (normalized with respect to the WFD Source display resolution that is conveyed in an RTSP M4 request message).
                     0b01; the unit is a mouse notch (where the application is responsible for representing the number of pixels per notch).
                     0b10-0b11; Reserved.

                     B13; Scroll Direction Indication bit.
                     0b0; Scrolling to the right. Scrolling to the right means the displayed content being shifted to the left from a user perspective.
                     0b1; Scrolling to the left. Scrolling to the left means the displayed content being shifted to the right from a user perspective.

B12:B0; Number of Scroll bits.
Number of units for a Horizontal scroll.
*/
                  break;
                }
        case 1: {
                  temp = atoi(*(splitedStr + i));
                  //log_info("getUIBCGenericScalePacket unit=[%d]\n", temp);
                  outData[7] = (temp >> 8) & 0xFF;
                  break;
                }
        case 2: {
                  temp = atoi(*(splitedStr + i));
                  //log_info("getUIBCGenericScalePacket direction=[%d]\n", temp);
                  outData[7] |= ((temp >> 10) & 0xFF);
                  break;

                }
        case 3: {
                  temp = atoi(*(splitedStr + i));
                  //log_info("getUIBCGenericScalePacket amount to scroll=[%d]\n", temp);
                  outData[7] |= ((temp >> 12) & 0xFF);
                  outData[8] = temp & 0xFF;

                  break;
                }
        default: {
                   break;
                 }
      }

      free(*(splitedStr + i));
    }

    free(splitedStr);
  }
  //hexdump(outData, uibcBodyLen);
  uibcmessage->m_DataValid = true;
  uibcmessage->m_PacketDataLen = uibcBodyLen;
}

// format: "typeId,  integer part, fraction part"
void getUIBCGenericRotatePacket(const char * inEventDesc, UibcMessage* uibcmessage) {
  log_info("getUIBCGenericRotatePacket (%s)", inEventDesc);
  char* outData = uibcmessage->m_PacketData;
  int32_t typeId;
  int32_t uibcBodyLen, genericPacketLen;
  int32_t integerPart, FractionPart;
  size_t size;

  char** splitedStr = str_split((char*)inEventDesc, ",", &size);

  if (splitedStr) {
    int i;
    for (i = 0; * (splitedStr + i); i++) {
      //log_info("getUIBCGenericRotatePacket splitedStr tokens=[%s]\n", *(splitedStr + i));

      switch (i) {
        case 0: {
                  typeId = atoi(*(splitedStr + i));
                  //log_info("getUIBCGenericRotatePacket typeId=[%d]\n", typeId);
                  genericPacketLen = 2;
                  uibcBodyLen = genericPacketLen + 7; // Generic herder leh = 7
                  outData = (char*)malloc(uibcBodyLen + 1);
                  // UIBC header
                  outData[0] = 0x00; //Version (3 bits),T (1 bit),Reserved(8 bits),Input Category (4 bits)
                  outData[1] = 0x00; //Version (3 bits),T (1 bit),Reserved(8 bits),Input Category (4 bits)
                  outData[2] = (uibcBodyLen >> 8) & 0xFF; //Length(16 bits)
                  outData[3] = uibcBodyLen & 0xFF; //Length(16 bits)
                  //Generic Input Body Format
                  outData[4] = typeId & 0xFF; // Tyoe ID, 1 octet
                  outData[5] = (genericPacketLen >> 8) & 0xFF; // Length, 2 octets
                  outData[6] = genericPacketLen & 0xFF; // Length, 2 octets
                  break;
                }
        case 1: {
                  integerPart = atoi(*(splitedStr + i));
                  outData[7] = integerPart & 0xFF;
                  //log_info("getUIBCGenericRotatePacket integerPart=[%d]\n", integerPart);
                  break;
                }
        case 2: {
                  FractionPart = atoi(*(splitedStr + i));
                  outData[8] = FractionPart & 0xFF;
                  //log_info("getUIBCGenericRotatePacket FractionPart=[%d]\n", FractionPart);

                  break;
                }
        default: {
                   break;
                 }
      }
      free(*(splitedStr + i));
    }

    free(splitedStr);
  }
  //hexdump(outData, uibcBodyLen);
  uibcmessage->m_DataValid = true;
  uibcmessage->m_PacketDataLen = uibcBodyLen;
}


char** str_split(char* pStr, const char* pDelim, size_t* size) {
  char** result    = 0;
  size_t count     = 0;
  char* tmp        = pStr;
  char* tmpStr     = NULL;
  char* last_comma = 0;

  asprintf(&tmpStr, "%s", pStr);

  /* Count how many elements will be extracted. */
  while (*tmp) {
    if (*pDelim == *tmp) {
      count++;
      last_comma = tmp;
    }
    tmp++;
  }

  /* Add space for trailing token. */
  count += last_comma < (pStr + strlen(pStr) - 1) ? 1 : 0;

  result = (char**)malloc(sizeof(char*) * count);

  *size = count;

  tmp = tmpStr = strdup(pStr);
  size_t idx  = 0;
  char* token;
  while ((token = strsep(&tmp, pDelim)) != NULL) {
    * (result + idx++) = strdup(token);
  }
  free(tmpStr);
  return result;
}
