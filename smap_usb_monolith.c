#include <types.h>
#include <irx.h>
#include <usbd.h>
#include <stdio.h>
#include <sysclib.h>
#include <thbase.h>
#include <thevent.h>
#include <sysmem.h>

#define USB_VENDOR_ASIX      0x0b95
#define USB_PRODUCT_AX88772  0x772a

#define AX_CMD_WRITE_NODE_ID     0x04
#define AX_CMD_WRITE_RX_CTL      0x10
#define AX_CMD_WRITE_MEDIUM_MODE 0x1b
#define AX_CMD_SHARE_NIC_RESET   0x20

#define MAX_PACKET_SIZE      1518
#define AX_RX_HEADER_SIZE    4
#define RING_BUFFER_SIZE     (MAX_PACKET_SIZE * 8)

static int usb_device_id = -1;
static int pipe_in = -1;
static int pipe_out = -1;
static int tx_event_flag = -1;
static int rx_thread_id = -1;

static u8 *tx_buffer = NULL;
static u8 *usb_rx_raw = NULL;
static u8 *ring_buffer = NULL;
static u32 ring_head = 0;
static u32 ring_tail = 0;

static u8 local_mac_address[6] = {0x00, 0x06, 0xdc, 0x11, 0x22, 0x33};

IRX_ID("smap_driver", 1, 1);

static void usb_rx_callback(int rc, int length, void *arg) {
    int *received = (int *)arg;
    *received = length;
    WakeupThread(rx_thread_id);
}

static void iop_rx_worker(void *arg) {
    int received_len = 0;
    while (1) {
        if (usb_device_id == -1 || pipe_in == -1) {
            DelayThread(50000);
            continue;
        }
        int ret = UsbBulkTransfer(pipe_in, usb_rx_raw, MAX_PACKET_SIZE + AX_RX_HEADER_SIZE, usb_rx_callback, &received_len);
        if (ret == 0) {
            SleepThread();
            if (received_len > AX_RX_HEADER_SIZE) {
                u16 packet_len = (usb_rx_raw[4] | (usb_rx_raw[5] << 8)) & 0x07ff;
                if (packet_len > 0 && packet_len <= MAX_PACKET_SIZE) {
                    u32 next_head = (ring_head + packet_len + 2) % RING_BUFFER_SIZE;
                    if (next_head != ring_tail) {
                        ring_buffer[ring_head] = packet_len & 0xFF;
                        ring_buffer[(ring_head + 1) % RING_BUFFER_SIZE] = (packet_len >> 8) & 0xFF;
                        for (int i = 0; i < packet_len; i++) {
                            ring_buffer[(ring_head + 2 + i) % RING_BUFFER_SIZE] = usb_rx_raw[AX_RX_HEADER_SIZE + i];
                        }
                        ring_head = next_head;
                    }
                }
            }
        }
        DelayThread(1000); 
    }
}

static int asix_write_cmd(int devId, u8 cmd, u16 value, u16 index, u16 size, void *data) {
    return UsbControlTransfer(0, 0, 0x40, cmd, value, index, size, data, NULL, NULL);
}

static int asix_init_network(int devId) {
    u16 medium_mode = 0x018c;
    u16 rx_ctl = 0x0088;
    asix_write_cmd(devId, AX_CMD_SHARE_NIC_RESET, 0, 0, 0, NULL);
    DelayThread(10000);
    asix_write_cmd(devId, AX_CMD_WRITE_NODE_ID, 0, 0, 6, local_mac_address);
    asix_write_cmd(devId, AX_CMD_WRITE_MEDIUM_MODE, medium_mode, 0, 0, NULL);
    asix_write_cmd(devId, AX_CMD_WRITE_RX_CTL, rx_ctl, 0, 0, NULL);
    return 0;
}

static int usbnet_probe(int devId) {
    UsbDeviceDescriptor *dd = UsbGetDeviceDescriptor(devId);
    if (dd->idVendor == USB_VENDOR_ASIX && dd->idProduct == USB_PRODUCT_AX88772) {
        usb_device_id = devId;
        pipe_in = UsbOpenEndpoint(devId, NULL);
        pipe_out = UsbOpenEndpoint(devId, NULL);
        asix_init_network(devId);
        return 1;
    }
    return 0;
}

static void usbnet_disconnect(int devId) {
    if (usb_device_id == devId) {
        usb_device_id = -1;
        pipe_in = -1;
        pipe_out = -1;
    }
}

UsbDriver usb_combo_driver = { NULL, NULL, USB_VENDOR_ASIX, USB_PRODUCT_AX88772, usbnet_probe, usbnet_disconnect };

int SmapInit(void) {
    iop_event_t event = {0, 0, 0};
    tx_event_flag = CreateEventFlag(&event);
    tx_buffer = AllocSysMemory(ALLOC_FIRST, MAX_PACKET_SIZE, NULL);
    usb_rx_raw = AllocSysMemory(ALLOC_FIRST, MAX_PACKET_SIZE + AX_RX_HEADER_SIZE, NULL);
    ring_buffer = AllocSysMemory(ALLOC_FIRST, RING_BUFFER_SIZE, NULL);
    if (!tx_buffer || !usb_rx_raw || !ring_buffer) return -1;
    iop_thread_t thread_param;
    thread_param.attr = TH_C;
    thread_param.thread = iop_rx_worker;
    thread_param.priority = 40;
    thread_param.stacksize = 2048;
    rx_thread_id = CreateThread(&thread_param);
    StartThread(rx_thread_id, NULL);
    if (UsbRegisterDriver(&usb_combo_driver) < 0) return -1;
    return 0;
}

static void usb_tx_callback(int rc, int length, void *arg) {
    u32 bits;
    SetEventFlag(tx_event_flag, 1);
}

int SmapXmit(void *packet, int length) {
    if (usb_device_id == -1 || pipe_out == -1) return -1;
    if (length > MAX_PACKET_SIZE) length = MAX_PACKET_SIZE;
    memcpy(tx_buffer, packet, length);
    u32 bits;
    ClearEventFlag(tx_event_flag, 0, &bits);
    int ret = UsbBulkTransfer(pipe_out, tx_buffer, length, usb_tx_callback, NULL);
    if (ret == 0) {
        WaitEventFlag(tx_event_flag, 1, 1, &bits);
        return length;
    }
    return 0;
}

int SmapReceive(void *packet_buffer, int max_len) {
    if (ring_head == ring_tail) return 0;
    u16 packet_len = ring_buffer[ring_tail] | (ring_buffer[(ring_tail + 1) % RING_BUFFER_SIZE] << 8);
    if (packet_len > max_len) {
        ring_tail = (ring_tail + packet_len + 2) % RING_BUFFER_SIZE;
        return 0;
    }
    for (int i = 0; i < packet_len; i++) {
        ((u8 *)packet_buffer)[i] = ring_buffer[(ring_tail + 2 + i) % RING_BUFFER_SIZE];
    }
    ring_tail = (ring_tail + packet_len + 2) % RING_BUFFER_SIZE;
    return packet_len;
}

int _start(int argc, char **argv) {
    return MODULE_RESIDENT_END;
}
