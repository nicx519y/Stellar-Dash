#include <assert.h>
#include <stdio.h>
#include "webhid_fast_link.h"
#include "../../RF_PHY_Hop/TX/USB/usb_board_link_port_ch585.h"

static whf_link_t host, device;
typedef struct { uint32_t position; unsigned wrap, race, reads; } dma_fixture_t;
static bool dma_wrap(void *p) { return ((dma_fixture_t *)p)->wrap != 0u; }
static void dma_clear(void *p) { ((dma_fixture_t *)p)->wrap=0u; }
static uint32_t dma_position(void *p) {
    dma_fixture_t *f=p;
    uint32_t result=f->position;
    ++f->reads;
    if(f->race) { f->race=0u; f->wrap=1u; f->position=12u; }
    return result;
}
static uint8_t block[WHF_BLOCK_BYTES], saved[WHF_BLOCK_BYTES], report[WEBHID_REPORT_BYTES];
static uint16_t transfer(whf_link_t *tx, whf_link_t *rx) {
    uint16_t n=whf_prepare(tx,block);
    assert(n && n<=4095u);
    whf_commit(tx,block);
    assert(whf_accept(rx,block,n));
    return n;
}
int main(void) {
    unsigned i; uint16_t n;
    uint32_t wraps=0u;
    dma_fixture_t dma={USB_SPI_RX_DMA_BYTES-8u,0u,1u,0u};
    /* DMA wraps during the register snapshot: do not mix old epoch/cursor. */
    assert(usb_spi_rx_dma_produced(&wraps,&dma,dma_wrap,dma_clear,dma_position)==USB_SPI_RX_DMA_BYTES+12u);
    assert(dma.reads==2u && wraps==USB_SPI_RX_DMA_BYTES);
    /* Repeated reads must not manufacture bytes or count the wrap twice. */
    assert(usb_spi_rx_dma_produced(&wraps,&dma,dma_wrap,dma_clear,dma_position)==USB_SPI_RX_DMA_BYTES+12u);
    dma.position=12u; dma.wrap=1u;
    assert(usb_spi_rx_dma_produced(&wraps,&dma,dma_wrap,dma_clear,dma_position)==2u*USB_SPI_RX_DMA_BYTES+12u);
    /* The modulo cursor may match after a complete ring; monotonic bytes
     * distinguish that from no new data. Unsigned subtraction survives wrap. */
    wraps=UINT32_MAX-USB_SPI_RX_DMA_BYTES+1u; dma.position=8u; dma.wrap=1u;
    const uint32_t consumed=wraps;
    assert(usb_spi_rx_dma_produced(&wraps,&dma,dma_wrap,dma_clear,dma_position)-consumed==USB_SPI_RX_DMA_BYTES+8u);
    /* Hardware case: IRQ has acknowledged the wrap before the consumer
     * samples NOW=164. Its epoch must be retained; pending flag is now zero. */
    wraps=USB_SPI_RX_DMA_BYTES; dma.position=164u; dma.wrap=0u;
    assert(usb_spi_rx_dma_produced(&wraps,&dma,dma_wrap,dma_clear,dma_position)-3200u==1056u);
    /* Real linker addresses must work with DMA register offsets and aliases. */
    assert(usb_spi_rx_dma_position(0xE9B8u, 0x2000E998u, 0u) == 32u);
    assert(usb_spi_rx_dma_position(0x2000E9B8u, 0x2000E998u, 0u) == 32u);
    assert(usb_spi_rx_dma_position(0xE998u + USB_SPI_RX_DMA_BYTES, 0x2000E998u, 32u) == 0u);
    assert(usb_spi_rx_dma_position(0xE997u, 0x2000E998u, 32u) == 32u);
    assert(usb_spi_rx_dma_position(0xE999u + USB_SPI_RX_DMA_BYTES, 0x2000E998u, 32u) == 32u);
    whf_init(&host,42u); whf_init(&device,42u);
    for(i=0;i<7u;++i) { memset(report,(int)i,sizeof(report)); assert(whf_enqueue(&host,report)); }
    assert(!whf_enqueue(&host,report));
    n=transfer(&host,&device); memcpy(saved,block,n);
    assert(device.rx_accepted==3u);
    assert(whf_accept(&device,saved,n)); assert(device.rx_accepted==3u);
    transfer(&host,&device); transfer(&host,&device);
    assert(device.rx_accepted==7u);
    transfer(&device,&host); assert(host.tx_acked==7u);
    assert(whf_enqueue(&host,report));
    assert(whf_prepare(&host,block)==0u); /* ACK alone cannot manufacture capacity. */
    for(i=0;i<7u;++i) { const uint8_t *p=whf_peek(&device); assert(p && p[0]==i && p[1023]==i); whf_release(&device); }
    transfer(&device,&host); assert(host.peer_limit==15u);
    transfer(&host,&device); assert(device.rx_accepted==8u);
    whf_init(&host,19u); whf_init(&device,19u);
    memset(report,0,sizeof(report)); report[1]=WEBHID_REPORT_IMAGE_DATA;
    for(i=0;i<7u;++i) assert(whf_enqueue(&host,report));
    assert(!whf_enqueue(&host,report));
    report[1]=WEBHID_REPORT_SECURE_REQUEST;
    assert(whf_enqueue(&host,report)); assert(!whf_enqueue(&host,report));
    transfer(&host,&device); transfer(&host,&device); transfer(&host,&device);
    assert(device.rx_accepted==8u); /* Reserved slot can carry control under full data load. */
    whf_init(&host,7u); whf_init(&device,7u);
    n=whf_prepare(&host,block); whf_commit(&host,block);
    block[8]^=1u; assert(!whf_accept(&device,block,n)); assert(device.crc_errors==1u);
    block[8]^=1u; assert(whf_accept(&device,block,n));
    whf_put32(block+4,6u); whf_put32(block+28,whf_crc(block,n));
    assert(!whf_accept(&device,block,n)); assert(device.failed);
    whf_init(&host,7u); whf_init(&device,7u);
    n=transfer(&host,&device);
    whf_put32(block+20,7u); whf_put32(block+28,whf_crc(block,n));
    assert(!whf_accept(&device,block,n)); /* Same block number cannot carry different ACK/credit. */
    whf_init(&host,7u); whf_init(&device,7u);
    n=whf_prepare(&host,block);
    whf_put32(block+16,1u); whf_put32(block+28,whf_crc(block,n));
    assert(!whf_accept(&device,block,n)); /* ACK for a report never sent. */
    whf_init(&host,7u); host.tx_produced=WHF_COUNTER_LIMIT;
    assert(!whf_enqueue(&host,report));
    host.tx_block=WHF_COUNTER_LIMIT; assert(!whf_prepare(&host,block));
    puts("WebHID V2 SPI window/CRC/epoch/order tests passed");
    return 0;
}
