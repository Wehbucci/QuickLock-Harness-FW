/*
 * ble_hal.c — the ONE translation unit that talks to NimBLE.
 *
 * Everything stack-specific (ble_gap_*, ble_gattc_*, ble_hs_*, ble_store_*,
 * nimble_port_*) lives here, behind the ble_hal.h interface. GAP/GATT callbacks
 * run on the NimBLE HOST task; they do only the minimum — reduce the event to a
 * ble_inbound_msg_t and post it to the sink queue, which wakes the BLE task.
 * No application state machine logic lives here (HARNESS_BLE_TASK.md sec 3, 8).
 *
 * Serves: F13, F15, F17, F18, F21, F22, F23, F24, F36.
 */

#include "ble_hal.h"
#include "ble_contract.h"
#include "config.h"
#include "ql_log.h"

#include <string.h>
#include "nvs_flash.h"

/* NimBLE host — these headers appear in NO other file in the project. */
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_store.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"

QL_LOG_TAG("ble_hal");

/* Provided by the NimBLE store/config module; wires bonds to NVS. NimBLE ships
 * no public header for this, so declare it exactly as the IDF examples do. */
extern void ble_store_config_init(void);

/* -------------------------------------------------------------------------- */
/* Contract UUIDs, built from the little-endian byte lists in ble_contract.h.  */
/* This is the only place the stack-agnostic byte lists meet a NimBLE type.     */
/* -------------------------------------------------------------------------- */
static const ble_uuid128_t s_svc_uuid = BLE_UUID128_INIT(QL_SVC_UUID_BYTES_LE);
static const ble_uuid128_t s_cmd_uuid = BLE_UUID128_INIT(QL_CHR_COMMAND_UUID_BYTES_LE);
static const ble_uuid128_t s_id_uuid  = BLE_UUID128_INIT(QL_CHR_IDENTITY_UUID_BYTES_LE);

/* -------------------------------------------------------------------------- */
/* HAL state                                                                   */
/* -------------------------------------------------------------------------- */
static QueueHandle_t s_sink = NULL;   /* where inbound messages go */
static uint8_t s_own_addr_type = BLE_OWN_ADDR_PUBLIC;
static volatile bool s_synced = false;

/* Cached GATT handles for the active connection, filled by the discovery chain.
 * A handle of 0 means "not yet discovered" (0 is never a valid ATT handle). */
static struct {
    uint16_t conn_handle;
    uint16_t svc_start;
    uint16_t svc_end;
    uint16_t command_val;
    uint16_t command_cccd;
    uint16_t identity_val;
} s_gatt;

static void gatt_cache_reset(void)
{
    memset(&s_gatt, 0, sizeof(s_gatt));
    s_gatt.conn_handle = BLE_HS_CONN_HANDLE_NONE;
}

/* -------------------------------------------------------------------------- */
/* Inbound message plumbing                                                    */
/* -------------------------------------------------------------------------- */

/*
 * post_inbound — enqueue a fully-formed message to the app task. Called from the
 * NimBLE host task context, so it must not block: zero timeout, drop+log if the
 * queue is somehow full (would indicate the BLE task is wedged).
 */
static void post_inbound(const ble_inbound_msg_t *msg)
{
    if (s_sink == NULL) {
        QL_LOGE("no event sink set; dropping inbound kind=%d", (int)msg->kind);
        return;
    }
    if (xQueueSend(s_sink, msg, 0) != pdTRUE) {
        QL_LOGE("inbound queue full; dropping kind=%d", (int)msg->kind);
    }
}

/* Convenience for the common "kind + conn + status" case. */
static void post_simple(ble_inbound_kind_t kind, uint16_t conn_handle, int status)
{
    ble_inbound_msg_t msg = {0};
    msg.kind = kind;
    msg.conn_handle = conn_handle;
    msg.status = status;
    post_inbound(&msg);
}

/* -------------------------------------------------------------------------- */
/* GATT discovery chain (async, host-task context)                             */
/* Each step's completion callback kicks the next; only the final step posts    */
/* BLE_IN_DISC_DONE / BLE_IN_DISC_FAILED to the app.                            */
/* -------------------------------------------------------------------------- */

static int on_dsc_disc(uint16_t conn_handle, const struct ble_gatt_error *error,
                       uint16_t chr_val_handle, const struct ble_gatt_dsc *dsc,
                       void *arg);
static int on_chr_disc(uint16_t conn_handle, const struct ble_gatt_error *error,
                       const struct ble_gatt_chr *chr, void *arg);

/* Step 1 completion: service found -> discover its characteristics. */
static int on_svc_disc(uint16_t conn_handle, const struct ble_gatt_error *error,
                       const struct ble_gatt_svc *service, void *arg)
{
    (void)arg;
    if (error->status == 0 && service != NULL) {
        s_gatt.svc_start = service->start_handle;
        s_gatt.svc_end = service->end_handle;
        QL_LOGI("discovered QuickLock service, handles [0x%04x..0x%04x]",
                service->start_handle, service->end_handle);
        return 0;
    }
    if (error->status == BLE_HS_EDONE) {
        if (s_gatt.svc_start == 0) {
            QL_LOGE("QuickLock service not present on peer");
            post_simple(BLE_IN_DISC_FAILED, conn_handle, BLE_HS_ENOENT);
            return 0;
        }
        int rc = ble_gattc_disc_all_chrs(conn_handle, s_gatt.svc_start,
                                         s_gatt.svc_end, on_chr_disc, NULL);
        if (rc != 0) {
            QL_LOGE("disc_all_chrs kickoff failed; rc=%d", rc);
            post_simple(BLE_IN_DISC_FAILED, conn_handle, rc);
        }
        return 0;
    }
    QL_LOGE("service discovery error; status=%d", error->status);
    post_simple(BLE_IN_DISC_FAILED, conn_handle, error->status);
    return 0;
}

/* Step 2 completion: match Command/Identity by UUID, then discover Command's
 * descriptors to locate the CCCD (0x2902). */
static int on_chr_disc(uint16_t conn_handle, const struct ble_gatt_error *error,
                       const struct ble_gatt_chr *chr, void *arg)
{
    (void)arg;
    if (error->status == 0 && chr != NULL) {
        if (ble_uuid_cmp(&chr->uuid.u, &s_cmd_uuid.u) == 0) {
            s_gatt.command_val = chr->val_handle;
            QL_LOGI("  Command char val_handle=0x%04x props=0x%02x",
                    chr->val_handle, chr->properties);
        } else if (ble_uuid_cmp(&chr->uuid.u, &s_id_uuid.u) == 0) {
            s_gatt.identity_val = chr->val_handle;
            QL_LOGI("  Identity char val_handle=0x%04x props=0x%02x",
                    chr->val_handle, chr->properties);
        }
        return 0;
    }
    if (error->status == BLE_HS_EDONE) {
        if (s_gatt.command_val == 0 || s_gatt.identity_val == 0) {
            QL_LOGE("missing char(s): command=0x%04x identity=0x%04x",
                    s_gatt.command_val, s_gatt.identity_val);
            post_simple(BLE_IN_DISC_FAILED, conn_handle, BLE_HS_ENOENT);
            return 0;
        }
        /* Search the Command characteristic's descriptor range for the CCCD.
         * Identity is read-only (no CCCD), so within this service the only
         * 0x2902 belongs to Command. */
        int rc = ble_gattc_disc_all_dscs(conn_handle, s_gatt.command_val,
                                         s_gatt.svc_end, on_dsc_disc, NULL);
        if (rc != 0) {
            QL_LOGE("disc_all_dscs kickoff failed; rc=%d", rc);
            post_simple(BLE_IN_DISC_FAILED, conn_handle, rc);
        }
        return 0;
    }
    QL_LOGE("characteristic discovery error; status=%d", error->status);
    post_simple(BLE_IN_DISC_FAILED, conn_handle, error->status);
    return 0;
}

/* Step 3 completion: CCCD located -> discovery done. */
static int on_dsc_disc(uint16_t conn_handle, const struct ble_gatt_error *error,
                       uint16_t chr_val_handle, const struct ble_gatt_dsc *dsc,
                       void *arg)
{
    (void)arg;
    (void)chr_val_handle;
    if (error->status == 0 && dsc != NULL) {
        if (dsc->uuid.u.type == BLE_UUID_TYPE_16 &&
            dsc->uuid.u16.value == QL_CCCD_UUID16) {
            s_gatt.command_cccd = dsc->handle;
            QL_LOGI("  Command CCCD handle=0x%04x", dsc->handle);
        }
        return 0;
    }
    if (error->status == BLE_HS_EDONE) {
        if (s_gatt.command_cccd == 0) {
            /* No CCCD: notifications cannot be enabled. Treat as failure so the
             * state machine drops the peer rather than silently missing button
             * presses (F13). */
            QL_LOGE("Command CCCD (0x2902) not found");
            post_simple(BLE_IN_DISC_FAILED, conn_handle, BLE_HS_ENOENT);
            return 0;
        }
        QL_LOGI("discovery complete (command/identity/CCCD cached)");
        post_simple(BLE_IN_DISC_DONE, conn_handle, 0);
        return 0;
    }
    QL_LOGE("descriptor discovery error; status=%d", error->status);
    post_simple(BLE_IN_DISC_FAILED, conn_handle, error->status);
    return 0;
}

/* -------------------------------------------------------------------------- */
/* GATT read/write completion callbacks                                        */
/* -------------------------------------------------------------------------- */

/* Identity read completed: copy the token bytes into the inbound payload. */
static int on_identity_read(uint16_t conn_handle, const struct ble_gatt_error *error,
                            struct ble_gatt_attr *attr, void *arg)
{
    (void)arg;
    ble_inbound_msg_t msg = {0};
    msg.kind = BLE_IN_IDENTITY_READ;
    msg.conn_handle = conn_handle;
    msg.status = error->status;
    if (error->status == 0 && attr != NULL && attr->om != NULL) {
        uint16_t n = OS_MBUF_PKTLEN(attr->om);
        if (n > BLE_INBOUND_PAYLOAD_MAX) {
            n = BLE_INBOUND_PAYLOAD_MAX;
        }
        os_mbuf_copydata(attr->om, 0, n, msg.payload);
        msg.payload_len = (uint8_t)n;
    }
    post_inbound(&msg);
    return 0;
}

/* State write-back (F17) completed. */
static int on_state_write(uint16_t conn_handle, const struct ble_gatt_error *error,
                          struct ble_gatt_attr *attr, void *arg)
{
    (void)attr;
    (void)arg;
    post_simple(BLE_IN_WRITE_DONE, conn_handle, error->status);
    return 0;
}

/* CCCD write (subscribe) completed. */
static int on_cccd_write(uint16_t conn_handle, const struct ble_gatt_error *error,
                         struct ble_gatt_attr *attr, void *arg)
{
    (void)attr;
    (void)arg;
    post_simple(BLE_IN_SUBSCRIBED, conn_handle, error->status);
    return 0;
}

/* -------------------------------------------------------------------------- */
/* Advertisement filtering (scan callback context)                             */
/* -------------------------------------------------------------------------- */

/*
 * adv_has_quicklock_service — true if the advertisement's 128-bit UUID list
 * contains the QuickLock service. Filtering here (not in the app) keeps all
 * NimBLE parsing inside the HAL. We check the ADV data's complete/incomplete
 * 128-bit UUID list; the peripheral MUST put the service UUID in the ADV (not
 * only the scan response) or we will not match it (see TESTING_WITHOUT_FOB.md).
 */
static bool adv_has_quicklock_service(const uint8_t *data, uint8_t len)
{
    struct ble_hs_adv_fields fields;
    if (ble_hs_adv_parse_fields(&fields, data, len) != 0) {
        return false;
    }
    for (int i = 0; i < fields.num_uuids128; i++) {
        if (ble_uuid_cmp(&fields.uuids128[i].u, &s_svc_uuid.u) == 0) {
            return true;
        }
    }
    return false;
}

/*
 * log_pairing_method — say, in the log, whether this bond was formed with LE
 * Secure Connections or fell back to LEGACY pairing (F36).
 *
 * Why this lookup is worth doing: sec_state reports encrypted/authenticated/
 * bonded, and NONE of those distinguish LESC from legacy — the two are
 * indistinguishable to the application once the link is up. F36 cites Secure
 * Connections *by name*, and legacy Just Works is materially weaker (it is
 * open to passive eavesdropping during the pairing exchange), so an
 * "encrypted=1" line is NOT evidence the requirement is met. Both sides ask
 * for SC (ble_hs_cfg.sm_sc = 1 here; the fob's core sets lesc = 1), but what
 * was actually NEGOTIATED is a runtime fact, and until now confirming it meant
 * an air sniffer.
 *
 * The stored bond record keeps the answer in its `sc` bit. Read it back and
 * state the result plainly — a spec-compliance finding the team needs belongs
 * in the log, not in an afternoon with a sniffer.
 *
 * Note `authenticated == 0` is EXPECTED and is not a failure: Just Works
 * cannot provide MITM protection (the fob has no display or keypad). The
 * pairing-mode window, not MITM, is what stops a stranger bonding.
 */
static void log_pairing_method(const struct ble_gap_conn_desc *desc)
{
    struct ble_store_key_sec key = {0};
    struct ble_store_value_sec value;

    key.peer_addr = desc->peer_id_addr;
    int rc = ble_store_read_peer_sec(&key, &value);
    if (rc != 0) {
        QL_LOGW("could not read the bond record to confirm the pairing method; "
                "rc=%d (F36 remains unconfirmed)", rc);
        return;
    }

    if (value.sc) {
        QL_LOGI("F36 OK: bonded with LE Secure Connections (ECDH); "
                "key_size=%d authenticated=%d (0 is expected for Just Works)",
                value.key_size, (int)value.authenticated);
    } else {
        QL_LOGW("F36 RISK: this bond used LEGACY pairing, NOT Secure "
                "Connections (key_size=%d). Report it; do not work around it.",
                value.key_size);
    }
}

/* -------------------------------------------------------------------------- */
/* Unified GAP event handler (host-task context)                               */
/* Used for both ble_gap_disc() and ble_gap_connect(); translate + post only.   */
/* -------------------------------------------------------------------------- */
static int ql_gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    switch (event->type) {

    case BLE_GAP_EVENT_DISC: {
        if (!adv_has_quicklock_service(event->disc.data, event->disc.length_data)) {
            return 0; /* not a QuickLock advertiser; ignore silently */
        }
        ble_inbound_msg_t msg = {0};
        msg.kind = BLE_IN_SCAN_MATCH;
        msg.rssi = event->disc.rssi;
        msg.addr.type = event->disc.addr.type;
        memcpy(msg.addr.val, event->disc.addr.val, BLE_HAL_ADDR_LEN);
        post_inbound(&msg);
        return 0;
    }

    case BLE_GAP_EVENT_CONNECT:
        /* status 0 == established; nonzero == attempt failed. Either way the app
         * decides what to do next. */
        post_simple(BLE_IN_CONNECTED, event->connect.conn_handle,
                    event->connect.status);
        return 0;

    case BLE_GAP_EVENT_DISCONNECT:
        /* Always surface the RAW reason so unexpected codes are visible during
         * bring-up. A supervision timeout is BLE_HS_HCI_ERR(BLE_ERR_CONN_SPVN_TMO)
         * == 0x200 + 0x08; the app classifies it (Mechanism A, F22). */
        QL_LOGW("disconnect; raw reason=0x%04x (%d)",
                event->disconnect.reason, event->disconnect.reason);
        gatt_cache_reset();
        post_simple(BLE_IN_DISCONNECTED, event->disconnect.conn.conn_handle,
                    event->disconnect.reason);
        return 0;

    case BLE_GAP_EVENT_ENC_CHANGE: {
        struct ble_gap_conn_desc desc;
        if (ble_gap_conn_find(event->enc_change.conn_handle, &desc) == 0) {
            QL_LOGI("enc change; status=%d encrypted=%d authenticated=%d bonded=%d",
                    event->enc_change.status, desc.sec_state.encrypted,
                    desc.sec_state.authenticated, desc.sec_state.bonded);
            /* The line above cannot distinguish LESC from legacy; this one can
             * (F36). Only meaningful once encryption actually succeeded. */
            if (event->enc_change.status == 0 && desc.sec_state.bonded) {
                log_pairing_method(&desc);
            }
        }
        post_simple(BLE_IN_ENC_CHANGED, event->enc_change.conn_handle,
                    event->enc_change.status);
        return 0;
    }

    case BLE_GAP_EVENT_NOTIFY_RX: {
        /* Only surface notifications from the Command characteristic. */
        if (event->notify_rx.attr_handle != s_gatt.command_val) {
            QL_LOGW("notify on unexpected handle 0x%04x (ignored)",
                    event->notify_rx.attr_handle);
            return 0;
        }
        ble_inbound_msg_t msg = {0};
        msg.kind = BLE_IN_NOTIFY_RX;
        msg.conn_handle = event->notify_rx.conn_handle;
        uint16_t n = OS_MBUF_PKTLEN(event->notify_rx.om);
        if (n > BLE_INBOUND_PAYLOAD_MAX) {
            n = BLE_INBOUND_PAYLOAD_MAX;
        }
        os_mbuf_copydata(event->notify_rx.om, 0, n, msg.payload);
        msg.payload_len = (uint8_t)n;
        post_inbound(&msg);
        return 0;
    }

    case BLE_GAP_EVENT_CONN_UPDATE: {
        struct ble_gap_conn_desc desc;
        if (ble_gap_conn_find(event->conn_update.conn_handle, &desc) == 0) {
            /* Log the NEGOTIATED params (raw units) so we can confirm the peer
             * honoured the request (F26 support, acceptance table). */
            QL_LOGI("conn update; status=%d itvl=%u latency=%u timeout=%u",
                    event->conn_update.status, desc.conn_itvl,
                    desc.conn_latency, desc.supervision_timeout);
        }
        post_simple(BLE_IN_CONN_UPDATED, event->conn_update.conn_handle,
                    event->conn_update.status);
        return 0;
    }

    case BLE_GAP_EVENT_REPEAT_PAIRING: {
        /* Peer wants to pair but a bond already exists for it. Delete the stale
         * bond and let pairing proceed; the pairing-window gate upstream governs
         * whether we should have connected at all. */
        struct ble_gap_conn_desc desc;
        if (ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc) == 0) {
            ble_store_util_delete_peer(&desc.peer_id_addr);
        }
        return BLE_GAP_REPEAT_PAIRING_RETRY;
    }

    case BLE_GAP_EVENT_MTU:
        QL_LOGI("MTU update; conn=%u mtu=%u",
                event->mtu.conn_handle, event->mtu.value);
        return 0;

    default:
        QL_LOGD("unhandled GAP event type=%d", event->type);
        return 0;
    }
}

/* -------------------------------------------------------------------------- */
/* Host lifecycle callbacks                                                    */
/* -------------------------------------------------------------------------- */

static void on_reset(int reason)
{
    QL_LOGE("NimBLE host reset; reason=%d", reason);
    s_synced = false;
}

static void on_sync(void)
{
    /* Ensure we have a usable identity address (public preferred). */
    int rc = ble_hs_util_ensure_addr(0);
    if (rc != 0) {
        QL_LOGE("ble_hs_util_ensure_addr failed; rc=%d", rc);
        return;
    }
    rc = ble_hs_id_infer_auto(0, &s_own_addr_type);
    if (rc != 0) {
        QL_LOGE("ble_hs_id_infer_auto failed; rc=%d", rc);
        return;
    }
    s_synced = true;
    QL_LOGI("NimBLE host synced; own_addr_type=%d, %d bond(s) stored",
            s_own_addr_type, ble_hal_bond_count());
}

static void host_task(void *param)
{
    (void)param;
    QL_LOGI("NimBLE host task started");
    nimble_port_run();          /* returns only on nimble_port_stop() */
    nimble_port_freertos_deinit();
}

/* -------------------------------------------------------------------------- */
/* Security Manager configuration (BLE_CONTRACT.md section 5)                   */
/* -------------------------------------------------------------------------- */
static void configure_security(void)
{
    /* LE Secure Connections + bonding, Just Works (no MITM).
     *
     * HONEST LIMITATION: Just Works gives ECDH key agreement and an AES-
     * encrypted link, but NOT BLE "authenticated" (MITM) protection — the fob is
     * button-only, so no passkey/numeric-compare is possible. What actually stops
     * a stranger bonding their own fob is the pairing-mode window enforced in
     * ble_task, not MITM. (BLE_CONTRACT.md section 5.) */
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm = 0;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_io_cap = BLE_HS_IO_NO_INPUT_OUTPUT; /* selects Just Works */

    /* Distribute encryption + identity keys both ways so bonds persist and the
     * peer identity is resolvable across reconnects. */
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
}

/* -------------------------------------------------------------------------- */
/* Public API                                                                  */
/* -------------------------------------------------------------------------- */

esp_err_t ble_hal_init(void)
{
    gatt_cache_reset();

    /* NVS first: NimBLE stores PHY calibration and (with NVS persistence on) the
     * bond keys here. Re-init after erase if the partition is stale (F18). */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK) {
        QL_LOGE("nvs_flash_init failed; %s", esp_err_to_name(ret));
        return ret;
    }

    ret = nimble_port_init();  /* brings up controller + host */
    if (ret != ESP_OK) {
        QL_LOGE("nimble_port_init failed; %s", esp_err_to_name(ret));
        return ret;
    }

    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr; /* handle store overflow */
    configure_security();

    /* Give the local device a name (harmless for a central; useful in sniffers).
     * Guarded because the GAP service characteristic is optional in menuconfig. */
#if CONFIG_BT_NIMBLE_GAP_SERVICE
    ble_svc_gap_device_name_set("QuickLock-Harness");
#endif

    ble_store_config_init();   /* wire bond persistence to NVS */

    nimble_port_freertos_init(host_task);
    QL_LOGI("BLE HAL init done (IDF NimBLE host)");
    return ESP_OK;
}

void ble_hal_set_event_sink(QueueHandle_t sink)
{
    s_sink = sink;
}

bool ble_hal_is_ready(void)
{
    return s_synced;
}

int ble_hal_start_scan(void)
{
    struct ble_gap_disc_params params = {0};
    /* Passive scan: we only need the advertising data to match the service UUID
     * and read RSSI; we do not need scan responses. Duplicate filtering off so a
     * moving fob keeps producing RSSI-bearing reports. */
    params.passive = 1;
    params.filter_duplicates = 0;
    params.itvl = 0;   /* 0 -> stack default scan interval */
    params.window = 0; /* 0 -> stack default scan window */

    int rc = ble_gap_disc(s_own_addr_type, BLE_HS_FOREVER, &params,
                          ql_gap_event, NULL);
    if (rc != 0 && rc != BLE_HS_EALREADY) {
        QL_LOGE("ble_gap_disc failed; rc=%d", rc);
    }
    return rc;
}

int ble_hal_stop_scan(void)
{
    int rc = ble_gap_disc_cancel();
    if (rc != 0 && rc != BLE_HS_EALREADY) {
        QL_LOGW("ble_gap_disc_cancel; rc=%d", rc);
    }
    return rc;
}

int ble_hal_connect(const ble_hal_addr_t *peer)
{
    /* Cancel any scan first; the controller cannot scan and initiate at once. */
    ble_gap_disc_cancel();

    ble_addr_t addr = {0};
    addr.type = peer->type;
    memcpy(addr.val, peer->val, BLE_HAL_ADDR_LEN);

    /* 30 s connect timeout; NULL params -> stack defaults for the initial link.
     * We tighten interval/latency/timeout afterwards via a param-update request
     * (ble_hal_request_conn_params), which is the portable way to hit the
     * BLE_CONTRACT.md targets. */
    int rc = ble_gap_connect(s_own_addr_type, &addr, 30000, NULL,
                            ql_gap_event, NULL);
    if (rc != 0) {
        QL_LOGE("ble_gap_connect failed; rc=%d", rc);
    }
    return rc;
}

int ble_hal_disconnect(uint16_t conn_handle)
{
    int rc = ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    if (rc != 0 && rc != BLE_HS_ENOTCONN) {
        QL_LOGW("ble_gap_terminate; rc=%d", rc);
    }
    return rc;
}

int ble_hal_start_security(uint16_t conn_handle)
{
    int rc = ble_gap_security_initiate(conn_handle);
    if (rc != 0) {
        QL_LOGE("ble_gap_security_initiate failed; rc=%d", rc);
    }
    return rc;
}

int ble_hal_request_conn_params(uint16_t conn_handle)
{
    /* THE UNIT CONVERSION, in one clearly commented place (BLE_CONTRACT.md sec 4):
     *   connection interval is in 1.25 ms units:  1000 ms / 1.25 = 800
     *   supervision timeout is in  10  ms units:  4000 ms / 10   = 400
     *   peripheral latency is a plain event count.
     * Mixing these up silently produces a link nothing like the design. */
    struct ble_gap_upd_params params = {0};
    params.itvl_min = (CONN_INTERVAL_MS * 4) / 5;   /* ms -> 1.25 ms units (=*0.8) */
    params.itvl_max = (CONN_INTERVAL_MS * 4) / 5;
    params.latency = PERIPHERAL_LATENCY;
    params.supervision_timeout = SUPERVISION_MS / 10; /* ms -> 10 ms units */

    /* BLE spec constraint the controller enforces:
     *   supervision_timeout(ms) > (1 + latency) * conn_interval_max(ms) * 2
     * Validate so a bad config is caught at the source, not as a silent reject. */
    uint32_t min_timeout_ms =
        (uint32_t)(1 + PERIPHERAL_LATENCY) * CONN_INTERVAL_MS * 2;
    if ((uint32_t)SUPERVISION_MS <= min_timeout_ms) {
        QL_LOGE("bad conn params: supervision %d ms <= required %u ms; "
                "raise SUPERVISION_MS or lower latency",
                SUPERVISION_MS, (unsigned)min_timeout_ms);
        return BLE_HS_EINVAL;
    }

    QL_LOGI("requesting conn params: itvl=%u (x1.25ms) latency=%u timeout=%u (x10ms)",
            params.itvl_max, params.latency, params.supervision_timeout);
    int rc = ble_gap_update_params(conn_handle, &params);
    if (rc != 0) {
        QL_LOGW("ble_gap_update_params; rc=%d (peer may not honour it)", rc);
    }
    return rc;
}

int ble_hal_discover(uint16_t conn_handle)
{
    /* Fresh cache for this connection's discovery. */
    gatt_cache_reset();
    s_gatt.conn_handle = conn_handle;
    QL_LOGI("starting service discovery on conn=%u", conn_handle);
    int rc = ble_gattc_disc_svc_by_uuid(conn_handle, &s_svc_uuid.u,
                                        on_svc_disc, NULL);
    if (rc != 0) {
        QL_LOGE("ble_gattc_disc_svc_by_uuid failed; rc=%d", rc);
    }
    return rc;
}

int ble_hal_read_identity(uint16_t conn_handle)
{
    if (s_gatt.identity_val == 0) {
        QL_LOGE("read_identity before discovery");
        return BLE_HS_ENOTCONN;
    }
    int rc = ble_gattc_read(conn_handle, s_gatt.identity_val,
                           on_identity_read, NULL);
    if (rc != 0) {
        QL_LOGE("ble_gattc_read(identity) failed; rc=%d", rc);
    }
    return rc;
}

int ble_hal_subscribe_command(uint16_t conn_handle)
{
    if (s_gatt.command_cccd == 0) {
        QL_LOGE("subscribe before CCCD discovered");
        return -1;
    }
    /* Write 0x0001 (little-endian) to the CCCD to enable notifications (F13). */
    uint16_t value = QL_CCCD_NOTIFY_ENABLE;
    int rc = ble_gattc_write_flat(conn_handle, s_gatt.command_cccd,
                                  &value, sizeof(value), on_cccd_write, NULL);
    if (rc != 0) {
        QL_LOGE("CCCD write failed; rc=%d", rc);
    }
    return rc;
}

int ble_hal_write_state(uint16_t conn_handle, uint8_t state_byte)
{
    if (s_gatt.command_val == 0) {
        QL_LOGE("write_state before discovery");
        return BLE_HS_ENOTCONN;
    }
    /* Write-with-response to the Command characteristic; the ATT ack closes the
     * F13/F17 loop. */
    int rc = ble_gattc_write_flat(conn_handle, s_gatt.command_val,
                                  &state_byte, sizeof(state_byte),
                                  on_state_write, NULL);
    if (rc != 0) {
        QL_LOGE("state write-back failed; rc=%d", rc);
    }
    return rc;
}

int ble_hal_get_rssi(uint16_t conn_handle, int8_t *out_rssi)
{
    return ble_gap_conn_rssi(conn_handle, out_rssi);
}

bool ble_hal_reason_is_supervision_timeout(int reason)
{
    /* NimBLE reports the HCI reason wrapped: BLE_HS_HCI_ERR(hci_code). The LE
     * supervision timeout HCI code is BLE_ERR_CONN_SPVN_TMO (0x08). This is the
     * clean, deterministic "user walked out of range / fob died" signal (F22). */
    return reason == BLE_HS_HCI_ERR(BLE_ERR_CONN_SPVN_TMO);
}

/* --------------------------- bond management ---------------------------- */

int ble_hal_bond_count(void)
{
    int count = 0;
    /* Count stored PEER security records == number of bonded peers. */
    if (ble_store_util_count(BLE_STORE_OBJ_TYPE_PEER_SEC, &count) != 0) {
        return 0;
    }
    return count;
}

bool ble_hal_has_bond(void)
{
    return ble_hal_bond_count() > 0;
}

int ble_hal_delete_all_bonds(void)
{
    int rc = ble_store_clear();
    QL_LOGW("cleared all bonds; rc=%d", rc);
    return rc;
}

int ble_hal_delete_bond_of_conn(uint16_t conn_handle)
{
    struct ble_gap_conn_desc desc;
    int rc = ble_gap_conn_find(conn_handle, &desc);
    if (rc != 0) {
        QL_LOGW("delete_bond_of_conn: conn %u not found; rc=%d", conn_handle, rc);
        return rc;
    }
    rc = ble_store_util_delete_peer(&desc.peer_id_addr);
    QL_LOGW("deleted bond for rejected peer on conn %u; rc=%d", conn_handle, rc);
    return rc;
}
