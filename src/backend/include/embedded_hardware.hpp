#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <chrono>

namespace workbench {

enum class DeviceType {
    MICROCONTROLLER,
    FPGA,
    JTAG_DEBUG_ADAPTER
};

enum class DeviceConnectionState {
    DISCONNECTED,
    CONNECTING,
    CONNECTED_READY,
    STREAMING_TELEMETRY,
    ERROR_FAULTED
};

enum class SimulatedFaultType {
    NONE,
    COMMUNICATION_TIMEOUT,
    CRC_CHECKSUM_ERROR,
    TARGET_VOLTAGE_DROP
};

struct HardwareCapability {
    std::string name;
    std::string value;
    std::string description;
};

struct DeviceInfo {
    std::string id;
    DeviceType type;
    std::string product_name;
    std::string manufacturer;
    std::string usb_vid_pid;
    std::string serial_number;
    std::string firmware_version;
    std::string voltage_domains;
    std::string physical_connector;
    std::string interface_protocol;
    std::vector<HardwareCapability> capabilities;
    bool is_simulated = true;
};

struct TelemetryPacket {
    std::string device_id;
    uint64_t sequence_id = 0;
    double timestamp_sec = 0.0;
    bool is_simulated = true;
    
    // Status
    DeviceConnectionState state;
    SimulatedFaultType active_fault = SimulatedFaultType::NONE;
    std::string status_message;

    // MCU specific telemetry
    double mcu_core_temp_C = 0.0;
    double mcu_vcc_volts = 0.0;
    uint32_t mcu_uptime_ms = 0;
    uint16_t mcu_adc_ch0 = 0;
    uint8_t mcu_gpio_state = 0;

    // FPGA specific telemetry
    double fpga_die_temp_C = 0.0;
    double fpga_vccint_volts = 0.0;
    double fpga_vccaux_volts = 0.0;
    double fpga_lut_util_pct = 0.0;
    bool fpga_pll_locked = true;
    std::string fpga_bitstream_hash;

    // JTAG Adapter specific telemetry
    double jtag_target_vref_volts = 0.0;
    double jtag_tck_frequency_mhz = 0.0;
    uint32_t jtag_tap_detected_idcode = 0;
    std::string jtag_tap_device_name;
    uint32_t jtag_packets_transferred = 0;

    std::string to_json() const;
};

class IEmbeddedDeviceAdapter {
public:
    virtual ~IEmbeddedDeviceAdapter() = default;
    virtual DeviceInfo get_info() const = 0;
    virtual DeviceConnectionState get_state() const = 0;
    virtual bool connect() = 0;
    virtual bool disconnect() = 0;
    virtual TelemetryPacket read_telemetry() = 0;
    virtual void inject_fault(SimulatedFaultType fault) = 0;
    virtual bool recover_fault() = 0;
};

class EmbeddedHardwareManager {
public:
    EmbeddedHardwareManager();

    std::vector<DeviceInfo> discover_devices();
    DeviceInfo get_device_info(const std::string& device_id);
    
    bool connect_device(const std::string& device_id);
    bool disconnect_device(const std::string& device_id);
    TelemetryPacket read_device_telemetry(const std::string& device_id);
    void inject_device_fault(const std::string& device_id, SimulatedFaultType fault);
    bool recover_device_fault(const std::string& device_id);

    std::string get_architecture_summary_json() const;

private:
    std::map<std::string, std::shared_ptr<IEmbeddedDeviceAdapter>> devices_;
    std::chrono::steady_clock::time_point start_time_;
};

} // namespace workbench
