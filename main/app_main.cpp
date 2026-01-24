/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <atomic>

#include <esp_err.h>
#include <nvs_flash.h>

#include <esp_matter.h>
#include <esp_matter_command.h>
#include <esp_matter_console.h>
#include <esp_matter_ota.h>

#include <common_macros.h>
#include <log_heap_numbers.h>

#include "app_priv.h"
#include "bs_log.h"

#if CHIP_DEVICE_CONFIG_ENABLE_THREAD
#include <platform/ESP32/OpenthreadLauncher.h>
#endif

#include <app/InteractionModelEngine.h>
#include <app/clusters/window-covering-server/window-covering-delegate.h>
#include <app/server/CommissioningWindowManager.h>
#include <app/server/Server.h>

#ifdef CONFIG_ENABLE_SET_CERT_DECLARATION_API
#include <esp_matter_providers.h>
#include <lib/support/Span.h>
#ifdef CONFIG_SEC_CERT_DAC_PROVIDER
#include <platform/ESP32/ESP32SecureCertDACProvider.h>
#elif defined(CONFIG_FACTORY_PARTITION_DAC_PROVIDER)
#include <platform/ESP32/ESP32FactoryDataProvider.h>
#endif
using namespace chip::DeviceLayer;
#endif

uint16_t window_covering_endpoint_id = 0;

using namespace esp_matter;
using namespace esp_matter::attribute;
using namespace chip::app::Clusters;

constexpr auto k_timeout_seconds = 300;

#ifdef CONFIG_ENABLE_SET_CERT_DECLARATION_API
extern const uint8_t cd_start[] asm("_binary_certification_declaration_der_start");
extern const uint8_t cd_end[] asm("_binary_certification_declaration_der_end");

const chip::ByteSpan cdSpan(cd_start, static_cast<size_t>(cd_end - cd_start));
#endif // CONFIG_ENABLE_SET_CERT_DECLARATION_API

#if CONFIG_ENABLE_ENCRYPTED_OTA
extern const char decryption_key_start[] asm("_binary_esp_image_encryption_key_pem_start");
extern const char decryption_key_end[] asm("_binary_esp_image_encryption_key_pem_end");

static const char *s_decryption_key = decryption_key_start;
static const uint16_t s_decryption_key_len = decryption_key_end - decryption_key_start;
#endif // CONFIG_ENABLE_ENCRYPTED_OTA

static void app_event_cb(const ChipDeviceEvent *event, intptr_t arg)
{
    switch (event->Type) {
    case chip::DeviceLayer::DeviceEventType::kInterfaceIpAddressChanged:
        BS_LOG_APP("Interface IP Address changed");
        break;

    case chip::DeviceLayer::DeviceEventType::kCommissioningComplete:
        BS_LOG_STATE("Commissioning complete");
        MEMORY_PROFILER_DUMP_HEAP_STAT("commissioning complete");
        break;

    case chip::DeviceLayer::DeviceEventType::kFailSafeTimerExpired:
        BS_LOG_WARN("Commissioning failed, fail safe timer expired");
        break;

    case chip::DeviceLayer::DeviceEventType::kCommissioningSessionStarted:
        BS_LOG_APP("Commissioning session started");
        break;

    case chip::DeviceLayer::DeviceEventType::kCommissioningSessionStopped:
        BS_LOG_APP("Commissioning session stopped");
        break;

    case chip::DeviceLayer::DeviceEventType::kCommissioningWindowOpened:
        BS_LOG_APP("Commissioning window opened");
        MEMORY_PROFILER_DUMP_HEAP_STAT("commissioning window opened");
        break;

    case chip::DeviceLayer::DeviceEventType::kCommissioningWindowClosed:
        BS_LOG_APP("Commissioning window closed");
        break;

    case chip::DeviceLayer::DeviceEventType::kFabricRemoved:
        {
            BS_LOG_APP("Fabric removed successfully");
            if (chip::Server::GetInstance().GetFabricTable().FabricCount() == 0)
            {
                chip::CommissioningWindowManager & commissionMgr = chip::Server::GetInstance().GetCommissioningWindowManager();
                constexpr auto kTimeoutSeconds = chip::System::Clock::Seconds16(k_timeout_seconds);
                if (!commissionMgr.IsCommissioningWindowOpen())
                {
                    /* After removing last fabric, this example does not remove the Wi-Fi credentials
                     * and still has IP connectivity so, only advertising on DNS-SD.
                     */
                    CHIP_ERROR err = commissionMgr.OpenBasicCommissioningWindow(kTimeoutSeconds,
                                                    chip::CommissioningWindowAdvertisement::kDnssdOnly);
                    if (err != CHIP_NO_ERROR)
                    {
                        BS_LOG_ERROR("Failed to open commissioning window, err:%" CHIP_ERROR_FORMAT, err.Format());
                    }
                }
            }
        break;
        }

    case chip::DeviceLayer::DeviceEventType::kFabricWillBeRemoved:
        BS_LOG_APP("Fabric will be removed");
        break;

    case chip::DeviceLayer::DeviceEventType::kFabricUpdated:
        BS_LOG_APP("Fabric is updated");
        break;

    case chip::DeviceLayer::DeviceEventType::kFabricCommitted:
        BS_LOG_APP("Fabric is committed");
        break;

    case chip::DeviceLayer::DeviceEventType::kBLEDeinitialized:
        BS_LOG_APP("BLE deinitialized and memory reclaimed");
        MEMORY_PROFILER_DUMP_HEAP_STAT("BLE deinitialized");
        break;

    default:
        break;
    }
}

// This callback is invoked when clients interact with the Identify Cluster.
// In the callback implementation, an endpoint can identify itself.
static esp_err_t app_identification_cb(identification::callback_type_t type, uint16_t endpoint_id, uint8_t effect_id,
                                       uint8_t effect_variant, void *priv_data)
{
    BS_LOG_APP("Identification callback: type: %u, effect: %u, variant: %u", type, effect_id, effect_variant);
    return ESP_OK;
}

enum class bs_wc_command_t : uint8_t {
    k_none = 0,
    k_up_or_open,
    k_down_or_close,
    k_go_to_lift_pct,
    k_stop_motion,
};

static std::atomic<bs_wc_command_t> s_pending_command(bs_wc_command_t::k_none);

class bs_window_covering_delegate : public chip::app::Clusters::WindowCovering::Delegate {
public:
    CHIP_ERROR HandleMovement(chip::app::Clusters::WindowCovering::WindowCoveringType type) override
    {
        (void)type;
        return CHIP_NO_ERROR;
    }

    CHIP_ERROR HandleStopMotion() override
    {
        app_driver_stop(window_covering_endpoint_id);
        return CHIP_NO_ERROR;
    }
};

static bs_window_covering_delegate s_wc_delegate;

static esp_err_t app_window_covering_command_pre_cb(const ConcreteCommandPath &command_path, TLVReader &tlv_data,
                                                    void *opaque_ptr)
{
    (void)opaque_ptr;
    if (command_path.mClusterId != WindowCovering::Id) {
        return ESP_OK;
    }

    switch (command_path.mCommandId) {
    case WindowCovering::Commands::UpOrOpen::Id:
        s_pending_command.store(bs_wc_command_t::k_up_or_open);
        BS_LOG_APP("Command: Open");
        break;
    case WindowCovering::Commands::DownOrClose::Id:
        s_pending_command.store(bs_wc_command_t::k_down_or_close);
        BS_LOG_APP("Command: Close");
        break;
    case WindowCovering::Commands::StopMotion::Id:
        s_pending_command.store(bs_wc_command_t::k_stop_motion);
        BS_LOG_APP("Command: Stop");
        app_driver_stop(window_covering_endpoint_id);
        break;
    case WindowCovering::Commands::GoToLiftPercentage::Id: {
        chip::app::Clusters::WindowCovering::Commands::GoToLiftPercentage::DecodableType command_data;
        CHIP_ERROR err = chip::app::DataModel::Decode(tlv_data, command_data);
        if (err == CHIP_NO_ERROR) {
            uint16_t pct100ths = command_data.liftPercent100thsValue;
            BS_LOG_APP("Command: GoToLiftPercentage %u.%02u%%", pct100ths / 100, pct100ths % 100);
        } else {
            BS_LOG_WARN("Command: GoToLiftPercentage decode failed: %" CHIP_ERROR_FORMAT, err.Format());
        }
        s_pending_command.store(bs_wc_command_t::k_go_to_lift_pct);
        break;
    }
    default:
        break;
    }
    return ESP_OK;
}

// This callback is called for every attribute update. The callback implementation shall
// handle the desired attributes and return an appropriate error code. If the attribute
// is not of your interest, please do not return an error code and strictly return ESP_OK.
static esp_err_t app_attribute_update_cb(attribute::callback_type_t type, uint16_t endpoint_id, uint32_t cluster_id,
                                         uint32_t attribute_id, esp_matter_attr_val_t *val, void *priv_data)
{
    esp_err_t err = ESP_OK;
    (void)priv_data;

    if (endpoint_id == window_covering_endpoint_id && cluster_id == WindowCovering::Id &&
        attribute_id == WindowCovering::Attributes::TargetPositionLiftPercent100ths::Id) {
        if (type == PRE_UPDATE) {
            bs_wc_command_t pending = s_pending_command.exchange(bs_wc_command_t::k_none);
            if (pending == bs_wc_command_t::k_up_or_open) {
                val->val.u16 = 10000;
            } else if (pending == bs_wc_command_t::k_down_or_close) {
                val->val.u16 = 0;
            }
            if (val->val.u16 > 10000) {
                val->val.u16 = 10000;
            }
        } else if (type == POST_UPDATE) {
            app_driver_set_target_percent100ths(endpoint_id, val->val.u16);
        }
    }

    return err;
}

extern "C" void app_main()
{
    esp_err_t err = ESP_OK;

    /* Initialize the ESP NVS layer */
    nvs_flash_init();

    MEMORY_PROFILER_DUMP_HEAP_STAT("Bootup");

    /* Create a Matter node and add the mandatory Root Node device type on endpoint 0 */
    node::config_t node_config;

    // node handle can be used to add/modify other endpoints.
    node_t *node = node::create(&node_config, app_attribute_update_cb, app_identification_cb);
    ABORT_APP_ON_FAILURE(node != nullptr, BS_LOG_ERROR("Failed to create Matter node"));

    MEMORY_PROFILER_DUMP_HEAP_STAT("node created");

    esp_matter::endpoint::window_covering::config_t wc_config;
    wc_config.window_covering.feature_flags = esp_matter::cluster::window_covering::feature::lift::get_id() |
                                              esp_matter::cluster::window_covering::feature::position_aware_lift::get_id();
    wc_config.window_covering.features.position_aware_lift.current_position_lift_percent_100ths =
        static_cast<uint16_t>(0);
    wc_config.window_covering.features.position_aware_lift.target_position_lift_percent_100ths =
        static_cast<uint16_t>(0);
    wc_config.window_covering.delegate = &s_wc_delegate;

    endpoint_t *endpoint = esp_matter::endpoint::window_covering::create(node, &wc_config, ENDPOINT_FLAG_NONE, nullptr);
    ABORT_APP_ON_FAILURE(endpoint != nullptr, BS_LOG_ERROR("Failed to create window covering endpoint"));

    window_covering_endpoint_id = endpoint::get_id(endpoint);
    BS_LOG_STATE("Window covering created with endpoint_id %d", window_covering_endpoint_id);

    cluster_t *wc_cluster = cluster::get(window_covering_endpoint_id, WindowCovering::Id);
    ABORT_APP_ON_FAILURE(wc_cluster != nullptr, BS_LOG_ERROR("Failed to get window covering cluster"));
    command::set_user_callback(command::get(wc_cluster, WindowCovering::Commands::UpOrOpen::Id, COMMAND_FLAG_ACCEPTED),
                               app_window_covering_command_pre_cb);
    command::set_user_callback(command::get(wc_cluster, WindowCovering::Commands::DownOrClose::Id, COMMAND_FLAG_ACCEPTED),
                               app_window_covering_command_pre_cb);
    command::set_user_callback(command::get(wc_cluster, WindowCovering::Commands::StopMotion::Id, COMMAND_FLAG_ACCEPTED),
                               app_window_covering_command_pre_cb);
    command::set_user_callback(command::get(wc_cluster, WindowCovering::Commands::GoToLiftPercentage::Id, COMMAND_FLAG_ACCEPTED),
                               app_window_covering_command_pre_cb);

#if CHIP_DEVICE_CONFIG_ENABLE_THREAD && CHIP_DEVICE_CONFIG_ENABLE_WIFI_STATION
    // Enable secondary network interface
    secondary_network_interface::config_t secondary_network_interface_config;
    endpoint = endpoint::secondary_network_interface::create(node, &secondary_network_interface_config, ENDPOINT_FLAG_NONE, nullptr);
    ABORT_APP_ON_FAILURE(endpoint != nullptr, BS_LOG_ERROR("Failed to create secondary network interface endpoint"));
#endif

#if CHIP_DEVICE_CONFIG_ENABLE_THREAD
    /* Set OpenThread platform config */
    esp_openthread_platform_config_t config = {
        .radio_config = ESP_OPENTHREAD_DEFAULT_RADIO_CONFIG(),
        .host_config = ESP_OPENTHREAD_DEFAULT_HOST_CONFIG(),
        .port_config = ESP_OPENTHREAD_DEFAULT_PORT_CONFIG(),
    };
    set_openthread_platform_config(&config);
#endif

#ifdef CONFIG_ENABLE_SET_CERT_DECLARATION_API
    auto * dac_provider = get_dac_provider();
#ifdef CONFIG_SEC_CERT_DAC_PROVIDER
    static_cast<ESP32SecureCertDACProvider *>(dac_provider)->SetCertificationDeclaration(cdSpan);
#elif defined(CONFIG_FACTORY_PARTITION_DAC_PROVIDER)
    static_cast<ESP32FactoryDataProvider *>(dac_provider)->SetCertificationDeclaration(cdSpan);
#endif
#endif // CONFIG_ENABLE_SET_CERT_DECLARATION_API

    /* Matter start */
    err = esp_matter::start(app_event_cb);
    ABORT_APP_ON_FAILURE(err == ESP_OK, BS_LOG_ERROR("Failed to start Matter, err:%d", err));

    MEMORY_PROFILER_DUMP_HEAP_STAT("matter started");

    err = app_driver_init(window_covering_endpoint_id);
    ABORT_APP_ON_FAILURE(err == ESP_OK, BS_LOG_ERROR("Failed to init motor driver, err:%d", err));

#if CONFIG_ENABLE_ENCRYPTED_OTA
    err = esp_matter_ota_requestor_encrypted_init(s_decryption_key, s_decryption_key_len);
    ABORT_APP_ON_FAILURE(err == ESP_OK, BS_LOG_ERROR("Failed to init encrypted OTA, err: %d", err));
#endif // CONFIG_ENABLE_ENCRYPTED_OTA

#if CONFIG_ENABLE_CHIP_SHELL
    esp_matter::console::diagnostics_register_commands();
    esp_matter::console::wifi_register_commands();
    esp_matter::console::factoryreset_register_commands();
    esp_matter::console::attribute_register_commands();
#if CONFIG_OPENTHREAD_CLI
    esp_matter::console::otcli_register_commands();
#endif
    esp_matter::console::init();
#endif

    while (true) {
        MEMORY_PROFILER_DUMP_HEAP_STAT("Idle");
        vTaskDelay(10000 / portTICK_PERIOD_MS);
    }
}
