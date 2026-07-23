#include "rndis.h"

#include <string.h>

#include "board_cfg.h"
#include "dhserver.h"
#include "dnserver.h"
#include "lwip/apps/httpd.h"
#include "lwip/init.h"
#include "lwip/prot/udp.h"
#include "lwip/timeouts.h"
#include "lwip/udp.h"
#include "stm32h750xx.h"
#include "usb_board_link_c_api.h"

#define INIT_IP4(a,b,c,d) { PP_HTONL(LWIP_MAKEU32(a,b,c,d)) }
#define BOARD_LINK_NET_MTU 1500u

static struct netif netif_data;
static struct pbuf *received_frame;
static uint8_t tx_frame[BOARD_LINK_NET_MTU + 32u];

uint8_t board_link_network_mac_address[6] =
    {0x02u, 0x02u, 0x84u, 0x6Au, 0x96u, 0x00u};

static const ip4_addr_t ipaddr =
    INIT_IP4(WEBCONFIG_IP_FIRST,
             WEBCONFIG_IP_SECOND,
             WEBCONFIG_IP_THIRD,
             WEBCONFIG_IP_FOURTH);
static const ip4_addr_t netmask = INIT_IP4(255,255,255,0);
static const ip4_addr_t gateway = INIT_IP4(0,0,0,0);

static dhcp_entry_t entries[] = {
    {{0}, INIT_IP4(WEBCONFIG_IP_FIRST, WEBCONFIG_IP_SECOND,
                   WEBCONFIG_IP_THIRD, 2), 24 * 60 * 60},
    {{0}, INIT_IP4(WEBCONFIG_IP_FIRST, WEBCONFIG_IP_SECOND,
                   WEBCONFIG_IP_THIRD, 3), 24 * 60 * 60},
    {{0}, INIT_IP4(WEBCONFIG_IP_FIRST, WEBCONFIG_IP_SECOND,
                   WEBCONFIG_IP_THIRD, 4), 24 * 60 * 60},
};

static const dhcp_config_t dhcp_config = {
    .router = INIT_IP4(0,0,0,0),
    .port = 67,
    .dns = INIT_IP4(WEBCONFIG_IP_FIRST, WEBCONFIG_IP_SECOND,
                    WEBCONFIG_IP_THIRD, WEBCONFIG_IP_FOURTH),
    "local",
    sizeof(entries) / sizeof(entries[0]),
    entries
};

static err_t linkoutput_fn(struct netif *netif, struct pbuf *p)
{
    uint16_t length;
    (void)netif;
    if((p == NULL) || (p->tot_len > sizeof(tx_frame)))
    {
        return ERR_BUF;
    }
    length = pbuf_copy_partial(p, tx_frame, p->tot_len, 0u);
    if(length != p->tot_len)
    {
        return ERR_BUF;
    }
    return UsbBoardLink_NetworkSend(tx_frame, length) ? ERR_OK : ERR_USE;
}

static err_t ip4_output_fn(struct netif *netif,
                           struct pbuf *p,
                           const ip4_addr_t *addr)
{
    return etharp_output(netif, p, addr);
}

static err_t netif_init_cb(struct netif *netif)
{
    LWIP_ASSERT("netif != NULL", netif != NULL);
    netif->mtu = BOARD_LINK_NET_MTU;
    netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP |
                   NETIF_FLAG_LINK_UP | NETIF_FLAG_UP;
    netif->state = NULL;
    netif->name[0] = 'B';
    netif->name[1] = 'L';
    netif->linkoutput = linkoutput_fn;
    netif->output = ip4_output_fn;
    return ERR_OK;
}

static void receive_network_frame(const uint8_t *source, uint16_t size)
{
    struct pbuf *p;
    if((source == NULL) || (size == 0u) || (received_frame != NULL))
    {
        return;
    }
    p = pbuf_alloc(PBUF_RAW, size, PBUF_POOL);
    if((p != NULL) && (pbuf_take(p, source, size) == ERR_OK))
    {
        received_frame = p;
    }
    else if(p != NULL)
    {
        pbuf_free(p);
    }
}

static void init_lwip(void)
{
    struct netif *netif = &netif_data;
    lwip_init();
    netif->hwaddr_len = sizeof(board_link_network_mac_address);
    memcpy(netif->hwaddr,
           board_link_network_mac_address,
           sizeof(board_link_network_mac_address));
    netif->hwaddr[5] ^= 0x01u;
    netif = netif_add(netif,
                      &ipaddr,
                      &netmask,
                      &gateway,
                      NULL,
                      netif_init_cb,
                      ip4_input);
    netif_set_default(netif);
}

bool dns_query_proc(const char *name, ip4_addr_t *addr)
{
    if((name != NULL) && (strcmp(name, WEBCONFIG_DOMAIN_NAME) == 0))
    {
        *addr = ipaddr;
        return true;
    }
    return false;
}

static void service_traffic(void)
{
    if(received_frame != NULL)
    {
        struct pbuf *frame = received_frame;
        received_frame = NULL;
        if(ethernet_input(frame, &netif_data) != ERR_OK)
        {
            pbuf_free(frame);
        }
    }
    sys_check_timeouts();
}

int rndis_init(void)
{
    UsbBoardLink_SetNetworkReceiveCallback(receive_network_frame);
    init_lwip();
    while(!netif_is_up(&netif_data))
    {
    }
    while(dhserv_init(&dhcp_config) != ERR_OK)
    {
    }
    while(dnserv_init(&ipaddr, 53, dns_query_proc) != ERR_OK)
    {
    }
    httpd_init();
    return 0;
}

void rndis_task(void)
{
    UsbBoardLink_Process();
    service_traffic();
}

sys_prot_t sys_arch_protect(void)
{
    return 0;
}

void sys_arch_unprotect(sys_prot_t value)
{
    (void)value;
}

uint32_t sys_now(void)
{
    return HAL_GetTick();
}

void safe_pbuf_free(struct pbuf *p)
{
    if(p != NULL)
    {
        pbuf_free(p);
    }
}
