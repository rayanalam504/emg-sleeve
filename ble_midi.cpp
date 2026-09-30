#include <atomic>
#include <cstdio>
#include "btstack.h"
#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"
#include "config.h"
#include "ble_midi.h"
#include "midi_service.h"  // generated from midi_service.gatt

#define MIDI_VALUE_HANDLE ATT_CHARACTERISTIC_7772E5DB_3868_4112_A1A9_F2669D106BF3_01_VALUE_HANDLE
#define MIDI_CCC_HANDLE   ATT_CHARACTERISTIC_7772E5DB_3868_4112_A1A9_F2669D106BF3_01_CLIENT_CONFIGURATION_HANDLE

// Advert: flags + the 128-bit MIDI service UUID (bytes reversed, as BLE sends them).
static const uint8_t adv_data[] = {
    0x02, BLUETOOTH_DATA_TYPE_FLAGS, 0x06,
    0x11, BLUETOOTH_DATA_TYPE_COMPLETE_LIST_OF_128_BIT_SERVICE_CLASS_UUIDS,
    0x00, 0xC7, 0xC4, 0x4E, 0xE3, 0x6C, 0x51, 0xA7, 0x33, 0x4B, 0xE8, 0xED, 0x5A, 0x0E, 0xB8, 0x03,
};

// Scan response: the name doesn't fit in the 31-byte advert next to the UUID.
static const uint8_t scan_resp_data[] = {
    0x0B, BLUETOOTH_DATA_TYPE_COMPLETE_LOCAL_NAME, 'E', 'M', 'G', ' ', 'S', 'l', 'e', 'e', 'v', 'e',
};

static hci_con_handle_t con_handle = HCI_CON_HANDLE_INVALID;
static bool notify_enabled = false;
static std::atomic<int> pending{-1};  // latest CC value not yet sent, -1 = nothing to send

static btstack_packet_callback_registration_t hci_cb, l2cap_cb, sm_cb;
static async_when_pending_worker_t send_worker;

static void print_interval(const char *what, uint16_t units) {
    // units of 1.25 ms
    printf("%s, interval %u.%02u ms\n", what, units * 125 / 100, 25 * (units & 3));
}

// Runs in the BTstack context whenever ble_midi_send_cc() has queued a value.
static void send_worker_fn(async_context_t *, async_when_pending_worker_t *) {
    if (notify_enabled && pending.load() >= 0) att_server_request_can_send_now_event(con_handle);
}

// The stack has room for one notification: send the latest value.
static void send_pending() {
    int v = pending.exchange(-1);
    if (v < 0 || !notify_enabled) return;
    uint32_t ts = to_ms_since_boot(get_absolute_time()) % 8192;
    uint8_t pkt[5] = {
        (uint8_t)(0x80 | ((ts >> 7) & 0x3F)),
        (uint8_t)(0x80 | (ts & 0x7F)),
        (uint8_t)(0xB0 | MIDI_CH),
        (uint8_t)MIDI_CC,
        (uint8_t)v,
    };
    att_server_notify(con_handle, MIDI_VALUE_HANDLE, pkt, sizeof(pkt));
}

static void packet_handler(uint8_t packet_type, uint16_t, uint8_t *packet, uint16_t) {
    if (packet_type != HCI_EVENT_PACKET) return;

    switch (hci_event_packet_get_type(packet)) {
    case BTSTACK_EVENT_STATE: {
        if (btstack_event_state_get_state(packet) != HCI_STATE_WORKING) return;
        bd_addr_t addr;
        gap_local_bd_addr(addr);
        printf("BLE up (%s), advertising as EMG Sleeve\n", bd_addr_to_str(addr));
        bd_addr_t null_addr = {0};
        gap_advertisements_set_params(160, 160, 0, 0, null_addr, 0x07, 0x00);  // every 100 ms, connectable
        gap_advertisements_set_data(sizeof(adv_data), (uint8_t *)adv_data);
        gap_scan_response_set_data(sizeof(scan_resp_data), (uint8_t *)scan_resp_data);
        gap_advertisements_enable(1);
        break;
    }
    case HCI_EVENT_META_GAP:
        if (hci_event_gap_meta_get_subevent_code(packet) != GAP_SUBEVENT_LE_CONNECTION_COMPLETE) break;
        if (gap_subevent_le_connection_complete_get_status(packet) != ERROR_CODE_SUCCESS) break;
        con_handle = gap_subevent_le_connection_complete_get_connection_handle(packet);
        print_interval("connected", gap_subevent_le_connection_complete_get_conn_interval(packet));
        // Ask for the minimum: 6 x 1.25 ms = 7.5 ms, no slave latency, 2 s supervision timeout.
        gap_request_connection_parameter_update(con_handle, 6, 6, 0, 200);
        break;
    case HCI_EVENT_LE_META:
        if (hci_event_le_meta_get_subevent_code(packet) != HCI_SUBEVENT_LE_CONNECTION_UPDATE_COMPLETE) break;
        print_interval("connection updated", hci_subevent_le_connection_update_complete_get_conn_interval(packet));
        break;
    case L2CAP_EVENT_CONNECTION_PARAMETER_UPDATE_RESPONSE:
        printf("7.5 ms interval request %s\n",
               l2cap_event_connection_parameter_update_response_get_result(packet) == 0 ? "accepted" : "rejected");
        break;
    case SM_EVENT_JUST_WORKS_REQUEST:
        printf("pairing requested, accepting (Just Works)\n");
        sm_just_works_confirm(sm_event_just_works_request_get_handle(packet));
        break;
    case SM_EVENT_PAIRING_COMPLETE:
        printf("pairing complete, status %u\n", sm_event_pairing_complete_get_status(packet));
        break;
    case HCI_EVENT_DISCONNECTION_COMPLETE:
        con_handle = HCI_CON_HANDLE_INVALID;
        notify_enabled = false;
        printf("disconnected, advertising again\n");
        gap_advertisements_enable(1);
        break;
    default:
        break;
    }
}

// Kept separate from packet_handler: the ATT server also re-forwards HCI events
// such as disconnect, which would otherwise be handled twice.
static void att_packet_handler(uint8_t packet_type, uint16_t, uint8_t *packet, uint16_t) {
    if (packet_type == HCI_EVENT_PACKET && hci_event_packet_get_type(packet) == ATT_EVENT_CAN_SEND_NOW) send_pending();
}

static uint16_t att_read_cb(hci_con_handle_t, uint16_t att_handle, uint16_t offset, uint8_t *buffer, uint16_t buffer_size) {
    if (att_handle == MIDI_CCC_HANDLE) {
        uint16_t v = notify_enabled ? GATT_CLIENT_CHARACTERISTICS_CONFIGURATION_NOTIFICATION : 0;
        return att_read_callback_handle_little_endian_16(v, offset, buffer, buffer_size);
    }
    return 0;  // reading the MIDI characteristic returns an empty value
}

static int att_write_cb(hci_con_handle_t connection_handle, uint16_t att_handle, uint16_t, uint16_t, uint8_t *buffer, uint16_t buffer_size) {
    // Incoming MIDI (writes to the MIDI characteristic itself) is ignored.
    if (att_handle != MIDI_CCC_HANDLE || buffer_size < 2) return 0;
    con_handle = connection_handle;
    notify_enabled = little_endian_read_16(buffer, 0) == GATT_CLIENT_CHARACTERISTICS_CONFIGURATION_NOTIFICATION;
    printf("MIDI notifications %s\n", notify_enabled ? "on" : "off");
    if (notify_enabled && pending.load() >= 0) att_server_request_can_send_now_event(con_handle);
    return 0;
}

void ble_midi_init() {
    l2cap_init();
    sm_init();
    sm_set_io_capabilities(IO_CAPABILITY_NO_INPUT_NO_OUTPUT);  // Just Works if macOS asks to pair
    sm_set_authentication_requirements(0);                     // no bonding
    att_server_init(profile_data, att_read_cb, att_write_cb);

    hci_cb.callback = &packet_handler;
    hci_add_event_handler(&hci_cb);
    l2cap_cb.callback = &packet_handler;
    l2cap_add_event_handler(&l2cap_cb);
    sm_cb.callback = &packet_handler;
    sm_add_event_handler(&sm_cb);
    att_server_register_packet_handler(&att_packet_handler);

    send_worker.do_work = send_worker_fn;
    async_context_add_when_pending_worker(cyw43_arch_async_context(), &send_worker);

    hci_power_control(HCI_POWER_ON);
}

void ble_midi_send_cc(uint8_t value) {
    pending.store(value);
    async_context_set_work_pending(cyw43_arch_async_context(), &send_worker);
}
