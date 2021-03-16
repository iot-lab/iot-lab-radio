#include <stdio.h>
#include <string.h>

#include "log.h"
#include "thread.h"
#include "net/gnrc/netreg.h"
#include "shell.h"
#include "net/gnrc.h"
#include "shell_commands.h"
#include "xtimer.h"
#include "panic.h"
#include "checksum/crc16_ccitt.h"
#define ENABLE_DEBUG (0)
#include "debug.h"

#if defined(MODULE_OD) && ENABLE_DEBUG
#include "od.h"
#define HEX_DUMP(d, l, w) od_hex_dump(d, l, w);
#else
#define HEX_DUMP(d, l, w)
#endif

#define NODE_ID_NONE ""
#define CHANNEL_NONE 0x00ffu
// 2 bytes for signed values
#define POWER_NONE 0xff01
#define PKT_SEND_OK 1
#define PKT_SEND_ERROR 0
#ifndef PKT_MAX_RECV
#define PKT_MAX_RECV 1024
#endif
// [Magic(2), CRC(2), len(node-id)(2), node-id(2), pkt_num(2),
//  power(2), channel(2), pkt_size(2)]
// = 16 bytes
#define PKT_MIN_SEND_SIZE 16
// 127-11 = 116 bytes (at86rf231 driver)
#define PKT_MAX_SEND_SIZE IEEE802154_FRAME_LEN_MAX-11
#define PKT_MAX_SEND_DELAY 100
#define RECV_QUEUE_SIZE (16)
#define MAGIC 0xCEADu

static kernel_pid_t iface = 4;

extern int _gnrc_netif_config(int argc, char **argv);

enum mode_t {
    MODE_TX,
    MODE_RX
};

/* Radio configuration */
static struct {
    int16_t power;
    uint16_t channel;
    char* node_id;
    uint16_t pkt_size;
    uint16_t nb_pkt;
    uint16_t delay;
    enum mode_t radio_mode;
}
conf;

uint8_t send_logger[PKT_MAX_RECV];

/*
 * This structure will be used to store packet reception information. 
 */
typedef struct {
    uint8_t lqi;
    int8_t rssi;
    uint16_t pkt_num;
} recv_info_t;

typedef struct {
    char* node_id;
    int16_t power;
    uint16_t channel;
    uint16_t pkt_size;
    int nb_pkt;
    int nb_generic_error;
    int nb_magic_error;
    int nb_crc_error;
    int nb_control_error;
    recv_info_t recv[PKT_MAX_RECV];
} recv_logger_t;

recv_logger_t recv_logger;

int16_t jamming = 0;

static void recv_logger_init(recv_logger_t* self)
{
    self->node_id = NODE_ID_NONE;
    self->channel = CHANNEL_NONE;
    self->power = POWER_NONE;
    self->nb_pkt = 0;
    self->nb_generic_error = 0;
    self->nb_magic_error = 0;
    self->nb_crc_error = 0;
    self->nb_control_error = 0;
}

static uint16_t get_crc(uint8_t* data, unsigned int size)
{
    return crc16_ccitt_calc(data, size);
}

static void recv_logger_show(recv_logger_t* self)
{
    printf("{\"nb_pkt\":%i,\"nb_generic_error\":%i,\"nb_magic_error\":%i,"
	       "\"nb_crc_error\":%i,\"nb_control_error\":%i,\"power\":%i,"
	       "\"channel\":%i,\"node_id\":\"%s\",\"recv\":[",
	       self->nb_pkt, self->nb_generic_error, self->nb_magic_error,
	       self->nb_crc_error, self->nb_control_error, self->power,
           self->channel, self->node_id);
    int i;
    for (i=0; i<self->nb_pkt; i++) {
        if (i > 0)
            printf(",");
        recv_info_t* recv_info = &self->recv[i];
        printf("{\"pkt_num\":%i,\"rssi\":%d,\"lqi\":%i}",
              recv_info->pkt_num, recv_info->rssi, recv_info->lqi);
    }
    printf("]}\n");
}

static void send_logger_show(int nb_pkt, int nb_error, char* node_id,
                             int16_t power, uint16_t channel)
{
    printf("{\"nb_pkt\":%u,\"nb_error\":%u,\"node_id\":\"%s\",\"power\":%i,"
           "\"channel\":%i,\"send\":[",
           nb_pkt, nb_error, node_id, power, channel);
    int i;
    for (i=0; i<nb_pkt; i++) {
        if (i > 0)
            printf(",");
        printf("{\"pkt_num\":%i,\"pkt_send\":%i}", i, send_logger[i]);
    }
    printf("]}\n");
}

int send_one_packet(int num)
{
    uint8_t addr[GNRC_NETIF_L2ADDR_MAXLEN];
    gnrc_pktsnip_t *pkt, *hdr;
    gnrc_netif_hdr_t *nethdr;
    uint8_t flags = 0x00;
    
    // This is used to store 8 bit packed values
    uint8_t raw_data[PKT_MAX_SEND_SIZE];
    uint8_t* data = raw_data;
    // Fills out a packet with packet size and num value
    memset(data, num, conf.pkt_size);
    // Add magic number (eg. ADCE)
    uint16_t magic = MAGIC;
    memcpy(data, &magic, sizeof(magic));
    data += sizeof(uint16_t);
    // Add zero out the space for CRC
    uint16_t crc = 0;
    uint8_t* crc_data = data;
    memcpy(data, &crc, sizeof(crc));
    data += sizeof(uint16_t);
    // Add sender node id length
    uint16_t node_id_length = strlen(conf.node_id);
    memcpy(data, &node_id_length, sizeof(node_id_length));
    data += sizeof(node_id_length);
    // Add sender node id
    memcpy(data, conf.node_id, node_id_length);
    data[node_id_length] = '\0';
    data += node_id_length + 1;
    // Add packet num value
    uint16_t pkt_num = num;
    memcpy(data, &pkt_num, sizeof(pkt_num));
    data += sizeof(pkt_num);
    // Add radio power
    int16_t power = conf.power;
    memcpy(data, &power, sizeof(power));
    data += sizeof(power);
    // Add channel radio
    uint16_t channel = conf.channel;
    memcpy(data, &channel, sizeof(channel));
    data += sizeof(channel);
    // Add packet size
    *data = conf.pkt_size;
    // Add CRC
    crc = get_crc(raw_data, conf.pkt_size);
    memcpy(crc_data, &crc, sizeof(crc));
    DEBUG("Sended packet:\n");
    HEX_DUMP(raw_data, conf.pkt_size, OD_WIDTH_DEFAULT);

    // send broadcast packet
    flags |= GNRC_NETIF_HDR_FLAGS_BROADCAST;

    /* put packet together */
    pkt = gnrc_pktbuf_add(NULL, raw_data, conf.pkt_size,
                          GNRC_NETTYPE_UNDEF);
    if (pkt == NULL) {
        LOG_ERROR("gnrc_pktbuf_add failed\n");
        return 1;
    }
    hdr = gnrc_netif_hdr_build(NULL, 0, addr, 0);
    if (hdr == NULL) {
        LOG_ERROR("gnrc_netif_hdr_build failed\n");
        gnrc_pktbuf_release(pkt);
        return 1;
    }
    LL_PREPEND(pkt, hdr);
    nethdr = (gnrc_netif_hdr_t *)hdr->data;
    nethdr->flags = flags;
    /* and send it */
    if (gnrc_netapi_send(iface, pkt) < 1) {
        LOG_ERROR("gnrc_netapi_send failed\n");
        gnrc_pktbuf_release(pkt);
        send_logger[num] = PKT_SEND_ERROR;
        return 1;
    }
    send_logger[num] = PKT_SEND_OK;
    return 0;
}


static void send_packets(void)
{
    recv_logger_init(&recv_logger);
    /* Enter TX mode */
    conf.radio_mode = MODE_TX;
    /* Send nb_pkt packets and count errors */
    int failure_count = 0;
    int i;
    for (i = 0; i < conf.nb_pkt; i++) {
        if (send_one_packet(i))
            failure_count++;
        xtimer_usleep(1000 * conf.delay);
    }
    /* restart RX */
    conf.radio_mode = MODE_RX;
    send_logger_show(conf.nb_pkt, failure_count, conf.node_id,
                     conf.power, conf.channel);
}

void* jamming_thread(void *arg)
{
    (void) arg;
    /* Enter TX mode */
    conf.radio_mode = MODE_TX;
    /* Send nb_pkt packets and count errors */
    int i=0;
    while(jamming==1){
      send_one_packet(i%100);
      i++;
      xtimer_usleep(1000 * conf.delay);
    }
    /* restart RX */
    conf.radio_mode = MODE_RX;
    return NULL;
}

char node_id[32];

static void recv_logger_add(recv_logger_t* self,
			    uint8_t* data, int size, int8_t rssi, uint8_t lqi)
{
    DEBUG("Payload size: %i\n", size);
    // Unknown packet type
    if (size <= 16) {
        self->nb_generic_error++;
        return;
    }
    uint8_t* raw_data = data;
    uint16_t magic;
    memcpy(&magic, data, sizeof(magic));
    if (magic != MAGIC) {
        self->nb_magic_error ++;
        return;
    }
    data += sizeof(magic);
    uint16_t crc;
    memcpy(&crc, data, sizeof(crc));
    memset(data, 0, sizeof(crc));
    uint16_t crc_data = get_crc(raw_data, size);
    // Bad CRC
    if (crc_data != crc) {
        self->nb_crc_error ++;
        return;
    }
    data += sizeof(crc_data);
    uint16_t node_id_length;
    memcpy(&node_id_length, data, sizeof(node_id_length));
    data += sizeof(node_id_length);
    memcpy(&node_id, data, node_id_length);
    node_id[node_id_length] = '\0';
    data += node_id_length + 1;
    DEBUG("Node id: %s\n", node_id);
    uint16_t pkt_num;
    memcpy(&pkt_num, data, sizeof(pkt_num));
    data += sizeof(pkt_num);
    DEBUG("Packet num: %i\n", pkt_num);

    int16_t power;
    memcpy(&power, data, sizeof(power));
    data += sizeof(power);
    DEBUG("Radio power: %i\n", power);

    uint16_t channel;
    memcpy(&channel, data, sizeof(channel));
    data += sizeof(channel);
    DEBUG("Radio channel: %i\n", channel);

    //  packet size != payload size
    uint8_t pkt_size = *data;
    DEBUG("packet size: %i\n", pkt_size);
    if (pkt_size != size) {
        self->nb_control_error ++;
        return;
    }

    if (strcmp(self->node_id,NODE_ID_NONE) != 0) {
        // sender id is updated
        if (strcmp(self->node_id, node_id) != 0) {
            self->nb_control_error ++;
            return;
        }
    } else {
        self->node_id = node_id;
        self->pkt_size = pkt_size;
        self->power = power;
        self->channel = channel;
    }

    // packet size, channel, power value is updated
    if ((self->pkt_size != pkt_size) ||
        (self->channel != channel) ||
        (self->power != power)) {
        self->nb_control_error ++;
        return;
    }

    recv_info_t* recv_info = &self->recv[self->nb_pkt];
    recv_info->pkt_num = pkt_num;
    recv_info->rssi = rssi;
    recv_info->lqi = lqi;
    self->nb_pkt++;
}


int send_pkt_cmd(int argc, char **argv)
{
    if (argc != 5) {
        printf("Usage: %s <str:node_id> <int:pkt_size> <int:nb_pkt> <int:delay_ms>\n", argv[0]);
        return 1;
    }
    conf.node_id = argv[1];

    int ret = sscanf(argv[2], "%"SCNu16, &conf.pkt_size) +
    sscanf(argv[3], "%"SCNu16, &conf.nb_pkt) +
    sscanf(argv[4], "%"SCNu16, &conf.delay);

    if ((ret == 3) && (strcmp(conf.node_id, NODE_ID_NONE) != 0)) {
        if (conf.pkt_size < PKT_MIN_SEND_SIZE ||
            conf.pkt_size > PKT_MAX_SEND_SIZE) {
             printf("Invalid packet size (%i<size<%i)\n",
                    PKT_MIN_SEND_SIZE,
                    PKT_MAX_SEND_SIZE);
            return 1;
        }
        if (conf.nb_pkt < 1 || conf.nb_pkt > PKT_MAX_RECV) {
            printf("Invalid number of packets (0<nb_pkt<=%i)\n",
                   PKT_MAX_RECV);
            return 1;
        }
        if (conf.delay < 1 || conf.delay > PKT_MAX_SEND_DELAY) {
            printf("Invalid delay (1<=delay<%i)\n",
                   PKT_MAX_SEND_DELAY);
            return 1;
        }
        send_packets();
        return 0;
    }
    return 1;
}


char jamming_thread_stack[512+256];

int radio_jamming_cmd(int argc, char **argv)
{
    if (argc != 5) {
        printf("Usage: %s <state:start|stop> <str:node_id> <int:pkt_size> <int:delay_ms>\n", argv[0]);
        return 1;
    }
    char* state = argv[1];
    if (!(strcmp(state, "start") == 0 || strcmp(state, "stop") == 0 )) {
       printf("Invalid state arg %s: <start|stop>\n", state);
       return 1;
    }

    conf.node_id = argv[2];
    int ret = sscanf(argv[3], "%"SCNu16, &conf.pkt_size) +
    sscanf(argv[4], "%"SCNu16, &conf.delay);

    if ((ret == 2) && (strcmp(conf.node_id, NODE_ID_NONE) != 0)) {
        if (conf.pkt_size < PKT_MIN_SEND_SIZE ||
            conf.pkt_size > PKT_MAX_SEND_SIZE) {
             printf("Invalid packet size (%i<size<%i)\n",
                    PKT_MIN_SEND_SIZE,
                    PKT_MAX_SEND_SIZE);
            return 1;
        }
        if (conf.delay < 1 || conf.delay > PKT_MAX_SEND_DELAY) {
            printf("Invalid delay (1<=delay<%i)\n",
                   PKT_MAX_SEND_DELAY);
            return 1;
        }
        if (strcmp(state, "start") == 0) {
          if(jamming == 1) {
            printf("Error, already jamming. Stop before\n");
            return 1;
          } else {
            jamming = 1;
            thread_create(jamming_thread_stack, sizeof(jamming_thread_stack),
                          THREAD_PRIORITY_MAIN + 1, THREAD_CREATE_STACKTEST,
                          jamming_thread, NULL, "jamming_thread");
            return 0;
          }
        } else {
          jamming = 0;
          return 0;
        }
    }
    return 1;
}

int show_cmd(int argc, char **argv)
{
    if (argc >1) {
        printf("Usage: %s\n", argv[0]);
        return 1;
    }
    recv_logger_show(&recv_logger);
    printf("{\"ack\":\"show\"}\n");
    return 0;
}

int clear_cmd(int argc, char **argv)
{
    if (argc >1) {
        printf("Usage: %s\n", argv[0]);
        return 1;
    }
    recv_logger_init(&recv_logger);
    printf("{\"ack\":\"clear\"}\n");
    return 0;
}

int ifconfig_cmd(char* radio_cmd, int argc, char **argv) 
{
    if (argc < 2) {
        printf("Usage: %s <int:%s>\n", argv[0], radio_cmd);
        return 1;
    }
    if (strcmp(radio_cmd, "power") == 0) {
        sscanf(argv[1], "%"SCNd16, &conf.power);
    } else {
        sscanf(argv[1], "%"SCNu16, &conf.channel);
    }
    char iface_cmd[10];
    snprintf(iface_cmd, 10, "%hu", iface);
    char * ifconfig_argv[5] = {
        [0] = "ifconfig",
        [1] = iface_cmd,
        [2] = "set",
        [3] = radio_cmd,
        [4] = argv[1]
    };
    _gnrc_netif_config(5, ifconfig_argv);
    printf("{\"ack\":\"%s\"}\n", radio_cmd);
    return 0;
}

int set_channel_cmd(int argc, char **argv)
{
    // 11 -> 26
    return ifconfig_cmd("channel", argc, argv);
}

int set_power_cmd(int argc, char **argv)
{
    // ["-17","-12","-9","-7","-5","-4","-3","-2",
    //  "-1","0","0.7","1.3","1.8","2.3","2.8","3"]
    return ifconfig_cmd("power", argc, argv);
}

static const shell_command_t shell_commands[] = {
    { "send", "send packets", send_pkt_cmd},
    { "jam", "radio jamming", radio_jamming_cmd},
    { "show", "show recv", show_cmd},
    { "clear", "clear recv", clear_cmd},
    { "channel", "change channel", set_channel_cmd},
    { "power", "change power", set_power_cmd},
    { NULL, NULL, NULL }
};

static void _recv_handle_pkt(gnrc_pktsnip_t *pkt)
{
    /// Iterate though snippets (Linked List)
    gnrc_pktsnip_t *snip = pkt;
    gnrc_pktsnip_t *payload = NULL;
    gnrc_netif_hdr_t *hdr = NULL;

    while(snip != NULL) {
        switch(snip->type)  {
            case GNRC_NETTYPE_UNDEF:
                // No specific network type -> payload
                payload = snip;
            break;
            case GNRC_NETTYPE_NETIF:
                /// Interface data -> header
                hdr = snip->data;
                (void) hdr;
                DEBUG("Received packet:\n");
                HEX_DUMP(payload->data, payload->size, OD_WIDTH_DEFAULT);
                recv_logger_add(&recv_logger, payload->data, payload->size,
                                hdr->rssi, hdr->lqi);
            break;
            default:

            break;
        }
        snip = snip->next;
    }
    gnrc_pktbuf_release(pkt);
}

/*
 * https://riot-os.org/api/group__net__gnrc.html. 
 */

void *recv_thread(void *arg)
{
    (void) arg;
    msg_t recv_thread_msg_queue[RECV_QUEUE_SIZE];
    msg_init_queue(recv_thread_msg_queue, RECV_QUEUE_SIZE);
    gnrc_pktsnip_t *pkt = NULL;
    gnrc_netreg_entry_t me_reg = GNRC_NETREG_ENTRY_INIT_PID(GNRC_NETREG_DEMUX_CTX_ALL,
                                                            sched_active_pid);
    gnrc_netreg_register(GNRC_NETTYPE_UNDEF, &me_reg);

    msg_t msg;
    while(1) {
        if(msg_receive(&msg) != 1) {
            LOG_ERROR("Unable to receive message\n");
            continue;
        }
        switch(msg.type) {
            case GNRC_NETAPI_MSG_TYPE_RCV:
                pkt = msg.content.ptr;
                _recv_handle_pkt(pkt);
            break;
        }
    }
    return NULL;
}

char recv_thread_stack[512+256];

int main(void)
{
    gnrc_netif_t *netif = NULL;

    while ((netif = gnrc_netif_iter(netif))) {
        iface = netif->pid;
        DEBUG("Using network interface: %u\n", iface);
    }
    recv_logger_init(&recv_logger);

    thread_create(recv_thread_stack, sizeof(recv_thread_stack),
                  THREAD_PRIORITY_MAIN + 2, THREAD_CREATE_STACKTEST,
                  recv_thread, NULL, "recv_thread");

    char line_buf[SHELL_DEFAULT_BUFSIZE];
    shell_run(shell_commands, line_buf, SHELL_DEFAULT_BUFSIZE);
    return 0;
}
