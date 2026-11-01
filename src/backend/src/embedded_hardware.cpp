#include "embedded_hardware.hpp"
#include <sstream>
#include <iomanip>
#include <cmath>

namespace workbench {

static inline const char* state_to_string(DeviceConnectionState s) {
    switch (s) {
        case DeviceConnectionState::DISCONNECTED: return "DISCONNECTED";
        case DeviceConnectionState::CONNECTING: return "CONNECTING";
        case DeviceConnectionState::CONNECTED_READY: return "CONNECTED_READY";
        case DeviceConnectionState::STREAMING_TELEMETRY: return "STREAMING_TELEMETRY";
        case DeviceConnectionState::ERROR_FAULTED: return "ERROR_FAULTED";
    }
    return "UNKNOWN";
}

static inline const char* fault_to_string(SimulatedFaultType f) {
    switch (f) {
        case SimulatedFaultType::NONE: return "NONE";
        case SimulatedFaultType::COMMUNICATION_TIMEOUT: return "COMMUNICATION_TIMEOUT";
        case SimulatedFaultType::CRC_CHECKSUM_ERROR: return "CRC_CHECKSUM_ERROR";
        case SimulatedFaultType::TARGET_VOLTAGE_DROP: return "TARGET_VOLTAGE_DROP";
    }
    return "NONE";
}

std::string TelemetryPacket::to_json() const {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(3);
    ss << "{";
    ss << "\"device_id\":\"" << device_id << "\",";
    ss << "\"sequence_id\":" << sequence_id << ",";
    ss << "\"timestamp_sec\":" << timestamp_sec << ",";
    ss << "\"is_simulated\":true,";
    ss << "\"state\":\"" << state_to_string(state) << "\",";
    ss << "\"active_fault\":\"" << fault_to_string(active_fault) << "\",";
    ss << "\"status_message\":\"" << status_message << "\",";

    // MCU
    ss << "\"mcu\":{";
    ss << "\"core_temp_C\":" << mcu_core_temp_C << ",";
    ss << "\"vcc_volts\":" << mcu_vcc_volts << ",";
    ss << "\"uptime_ms\":" << mcu_uptime_ms << ",";
    ss << "\"adc_ch0\":" << mcu_adc_ch0 << ",";
    ss << "\"gpio_state\":" << static_cast<int>(mcu_gpio_state);
    ss << "},";

    // FPGA
    ss << "\"fpga\":{";
    ss << "\"die_temp_C\":" << fpga_die_temp_C << ",";
    ss << "\"vccint_volts\":" << fpga_vccint_volts << ",";
    ss << "\"vccaux_volts\":" << fpga_vccaux_volts << ",";
    ss << "\"lut_util_pct\":" << fpga_lut_util_pct << ",";
    ss << "\"pll_locked\":" << (fpga_pll_locked ? "true" : "false") << ",";
    ss << "\"bitstream_hash\":\"" << fpga_bitstream_hash << "\"";
    ss << "},";

    // JTAG
    ss << "\"jtag\":{";
    ss << "\"target_vref_volts\":" << jtag_target_vref_volts << ",";
    ss << "\"tck_frequency_mhz\":" << jtag_tck_frequency_mhz << ",";
    ss << "\"tap_detected_idcode\":\"0x" << std::hex << std::uppercase << jtag_tap_detected_idcode << std::dec << "\",";
    ss << "\"tap_device_name\":\"" << jtag_tap_device_name << "\",";
    ss << "\"packets_transferred\":" << jtag_packets_transferred;
    ss << "}";

    ss << "}";
    return ss.str();
}

// -------------------------------------------------------------
// Concrete Mock: Microcontroller Adapter (STM32H753VI ARM Cortex-M7)
// -------------------------------------------------------------
class McuMockAdapter : public IEmbeddedDeviceAdapter {
public:
    McuMockAdapter() {
        state_ = DeviceConnectionState::DISCONNECTED;
    }

    DeviceInfo get_info() const override {
        DeviceInfo info;
        info.id = "mcu_stm32h7";
        info.type = DeviceType::MICROCONTROLLER;
        info.product_name = "STM32H753VI High-Performance MCU (ARM Cortex-M7 @ 480 MHz)";
        info.manufacturer = "STMicroelectronics";
        info.usb_vid_pid = "0x0483:0x374B (ST-Link / VCP)";
        info.serial_number = "STM32H7-2026-A482";
        info.firmware_version = "v2.4.1-workbench-mock";
        info.voltage_domains = "3.3V Digital I/O, 3.3V Analog VDDA, 1.2V Core VDD";
        info.physical_connector = "USB-C (Host CDC) / 20-Pin Cortex Debug Header";
        info.interface_protocol = "USB CDC-ACM (115200 8N1) & SWD/JTAG";
        info.is_simulated = true;

        info.capabilities = {
            {"CPU Core", "ARM Cortex-M7 with DP-FPU @ 480 MHz", "Dual-precision hardware floating point"},
            {"Internal RAM", "1024 KB SRAM (incl. 128 KB ITCM, 128 KB DTCM)", "Zero wait-state execution"},
            {"Peripherals", "3x 16-bit ADCs, 2x DACs, Ethernet MAC, USB OTG, 4x I2C, 6x SPI", "Onboard test peripherals"},
            {"Telemetry Rate", "10 Hz Streaming Telemetry", "Framed JSON over VCP"}
        };
        return info;
    }

    DeviceConnectionState get_state() const override { return state_; }

    bool connect() override {
        if (fault_ == SimulatedFaultType::COMMUNICATION_TIMEOUT) {
            state_ = DeviceConnectionState::ERROR_FAULTED;
            return false;
        }
        state_ = DeviceConnectionState::CONNECTED_READY;
        return true;
    }

    bool disconnect() override {
        state_ = DeviceConnectionState::DISCONNECTED;
        return true;
    }

    TelemetryPacket read_telemetry() override {
        seq_++;
        TelemetryPacket pkt;
        pkt.device_id = "mcu_stm32h7";
        pkt.sequence_id = seq_;
        pkt.timestamp_sec = static_cast<double>(seq_) * 0.1;
        pkt.is_simulated = true;
        pkt.active_fault = fault_;

        if (fault_ == SimulatedFaultType::COMMUNICATION_TIMEOUT) {
            pkt.state = DeviceConnectionState::ERROR_FAULTED;
            pkt.status_message = "ERROR: UART CDC-ACM response timeout (2000 ms elapsed waiting for ACK frame). Target MCU halted or bus unresponsive.";
            return pkt;
        }

        if (state_ == DeviceConnectionState::DISCONNECTED) {
            pkt.state = DeviceConnectionState::DISCONNECTED;
            pkt.status_message = "Device is disconnected. Click Connect to establish virtual link.";
            return pkt;
        }

        state_ = DeviceConnectionState::STREAMING_TELEMETRY;
        pkt.state = state_;
        pkt.status_message = "Telemetry nominal. 10 Hz telemetry streaming active.";

        // Simulate realistic values with small thermal drift
        double t_offset = std::sin(static_cast<double>(seq_) * 0.05);
        pkt.mcu_core_temp_C = 38.5 + 1.2 * t_offset;
        pkt.mcu_vcc_volts = 3.305 + 0.012 * std::cos(static_cast<double>(seq_) * 0.1);
        pkt.mcu_uptime_ms = static_cast<uint32_t>(seq_ * 100);
        pkt.mcu_adc_ch0 = static_cast<uint16_t>(2048 + 150 * t_offset);
        pkt.mcu_gpio_state = static_cast<uint8_t>((seq_ / 10) % 16);

        return pkt;
    }

    void inject_fault(SimulatedFaultType fault) override {
        fault_ = fault;
        state_ = DeviceConnectionState::ERROR_FAULTED;
    }

    bool recover_fault() override {
        // Recovery procedure: flush UART buffers, issue software DTR/RTS pulse, reset packet state machine
        fault_ = SimulatedFaultType::NONE;
        state_ = DeviceConnectionState::CONNECTED_READY;
        return true;
    }

private:
    DeviceConnectionState state_ = DeviceConnectionState::DISCONNECTED;
    SimulatedFaultType fault_ = SimulatedFaultType::NONE;
    uint64_t seq_ = 0;
};

// -------------------------------------------------------------
// Concrete Mock: FPGA Adapter (AMD/Xilinx Artix-7 XC7A35T)
// -------------------------------------------------------------
class FpgaMockAdapter : public IEmbeddedDeviceAdapter {
public:
    FpgaMockAdapter() {
        state_ = DeviceConnectionState::DISCONNECTED;
    }

    DeviceInfo get_info() const override {
        DeviceInfo info;
        info.id = "fpga_artix7";
        info.type = DeviceType::FPGA;
        info.product_name = "Artix-7 XC7A35T FPGA Development Board";
        info.manufacturer = "AMD / Xilinx (Digilent Reference)";
        info.usb_vid_pid = "0x0403:0x6010 (FTDI FT2232H Dual Channel)";
        info.serial_number = "A7-XC7A35T-8841-B";
        info.firmware_version = "Bitstream: TOP_SYS_REL_v1.3";
        info.voltage_domains = "1.0V Core (VCCINT), 1.8V Auxiliary (VCCAUX), 3.3V I/O (VCCO_0..3)";
        info.physical_connector = "USB-B (FT2232H JTAG+UART) / 6-Pin Digilent JTAG Header";
        info.interface_protocol = "AXI4-Lite over High-Speed SPI (Channel B) & JTAG (Channel A)";
        info.is_simulated = true;

        info.capabilities = {
            {"Logic Cells", "33,280 Logic Cells (5,200 Slices)", "Configurable logic fabric"},
            {"Block RAM", "1,800 Kbits (50x 36Kb Blocks)", "Dual-port embedded memory"},
            {"DSP Slices", "90 DSP48E1 Slices", "Hardware 25x18 multipliers"},
            {"Clocking", "5x Clock Management Tiles (MMCM + PLL)", "Low-jitter multi-domain clock synthesis"}
        };
        return info;
    }

    DeviceConnectionState get_state() const override { return state_; }

    bool connect() override {
        if (fault_ == SimulatedFaultType::CRC_CHECKSUM_ERROR) {
            state_ = DeviceConnectionState::ERROR_FAULTED;
            return false;
        }
        state_ = DeviceConnectionState::CONNECTED_READY;
        return true;
    }

    bool disconnect() override {
        state_ = DeviceConnectionState::DISCONNECTED;
        return true;
    }

    TelemetryPacket read_telemetry() override {
        seq_++;
        TelemetryPacket pkt;
        pkt.device_id = "fpga_artix7";
        pkt.sequence_id = seq_;
        pkt.timestamp_sec = static_cast<double>(seq_) * 0.1;
        pkt.is_simulated = true;
        pkt.active_fault = fault_;

        if (fault_ == SimulatedFaultType::CRC_CHECKSUM_ERROR) {
            pkt.state = DeviceConnectionState::ERROR_FAULTED;
            pkt.status_message = "ERROR: SPI Telemetry Packet CRC32 mismatch (computed 0x8F32C01A != frame header 0x00000000). Communication line noise or corrupted transaction frame.";
            return pkt;
        }

        if (state_ == DeviceConnectionState::DISCONNECTED) {
            pkt.state = DeviceConnectionState::DISCONNECTED;
            pkt.status_message = "Device is disconnected. Click Connect to initialize AXI-Lite telemetry bridge.";
            return pkt;
        }

        state_ = DeviceConnectionState::STREAMING_TELEMETRY;
        pkt.state = state_;
        pkt.status_message = "Telemetry nominal. FPGA fabric and XADC monitoring operational.";

        double drift = std::cos(static_cast<double>(seq_) * 0.04);
        pkt.fpga_die_temp_C = 44.2 + 2.1 * drift;
        pkt.fpga_vccint_volts = 1.002 + 0.005 * drift;
        pkt.fpga_vccaux_volts = 1.801 + 0.003 * drift;
        pkt.fpga_lut_util_pct = 42.6 + 0.5 * std::abs(drift);
        pkt.fpga_pll_locked = true;
        pkt.fpga_bitstream_hash = "SHA256:d8a4f91b...7e3c";

        return pkt;
    }

    void inject_fault(SimulatedFaultType fault) override {
        fault_ = fault;
        state_ = DeviceConnectionState::ERROR_FAULTED;
    }

    bool recover_fault() override {
        // Recovery procedure: re-synchronize SPI framing word, clear hardware FIFO, re-request telemetry frame
        fault_ = SimulatedFaultType::NONE;
        state_ = DeviceConnectionState::CONNECTED_READY;
        return true;
    }

private:
    DeviceConnectionState state_ = DeviceConnectionState::DISCONNECTED;
    SimulatedFaultType fault_ = SimulatedFaultType::NONE;
    uint64_t seq_ = 0;
};

// -------------------------------------------------------------
// Concrete Mock: JTAG Debug Adapter (Segger J-Link BASE / FT2232H)
// -------------------------------------------------------------
class JtagMockAdapter : public IEmbeddedDeviceAdapter {
public:
    JtagMockAdapter() {
        state_ = DeviceConnectionState::DISCONNECTED;
    }

    DeviceInfo get_info() const override {
        DeviceInfo info;
        info.id = "jtag_jlink";
        info.type = DeviceType::JTAG_DEBUG_ADAPTER;
        info.product_name = "Segger J-Link BASE Debug Probe / TAP Controller";
        info.manufacturer = "SEGGER Microcontroller";
        info.usb_vid_pid = "0x1366:0x0101 (Segger J-Link)";
        info.serial_number = "JL-59401293";
        info.firmware_version = "J-Link V12 compiled Mar 2026";
        info.voltage_domains = "Target Reference Sensing: 1.2V to 5.0V with Bidirectional Active Level Shifting";
        info.physical_connector = "Standard 20-Pin 0.1\" Header (ARM JTAG/SWD Pinout)";
        info.interface_protocol = "IEEE 1149.1 JTAG (TCK, TMS, TDI, TDO, nTRST) / ARM Serial Wire Debug (SWD)";
        info.is_simulated = true;

        info.capabilities = {
            {"TAP Controller", "IEEE 1149.1 State Machine Master", "Drives 16-state JTAG TAP controller synchronously"},
            {"Clock Speed", "TCK Programmable up to 15.0 MHz", "Adaptive clocking support (RTCK)"},
            {"Target Vref Sensing", "Analog ADC monitoring (1.20V - 5.00V)", "Protects against voltage domain mismatches"},
            {"Scan Chain Detection", "Automatic boundary-scan IR and IDCODE scan", "Scans Cortex-M and FPGA TAP controllers"}
        };
        return info;
    }

    DeviceConnectionState get_state() const override { return state_; }

    bool connect() override {
        if (fault_ == SimulatedFaultType::TARGET_VOLTAGE_DROP) {
            state_ = DeviceConnectionState::ERROR_FAULTED;
            return false;
        }
        state_ = DeviceConnectionState::CONNECTED_READY;
        return true;
    }

    bool disconnect() override {
        state_ = DeviceConnectionState::DISCONNECTED;
        return true;
    }

    TelemetryPacket read_telemetry() override {
        seq_++;
        TelemetryPacket pkt;
        pkt.device_id = "jtag_jlink";
        pkt.sequence_id = seq_;
        pkt.timestamp_sec = static_cast<double>(seq_) * 0.1;
        pkt.is_simulated = true;
        pkt.active_fault = fault_;

        if (fault_ == SimulatedFaultType::TARGET_VOLTAGE_DROP) {
            pkt.state = DeviceConnectionState::ERROR_FAULTED;
            pkt.jtag_target_vref_volts = 0.21; // Target lost power
            pkt.status_message = "ERROR: Target Vref dropped below operating threshold (measured 0.21V < 1.20V min). Target board is unpowered or JTAG Vref cable disconnected.";
            return pkt;
        }

        if (state_ == DeviceConnectionState::DISCONNECTED) {
            pkt.state = DeviceConnectionState::DISCONNECTED;
            pkt.status_message = "Device is disconnected. Click Connect to query target TAP state machine.";
            return pkt;
        }

        state_ = DeviceConnectionState::STREAMING_TELEMETRY;
        pkt.state = state_;
        pkt.status_message = "JTAG probe nominal. Target reference sensed, TAP chain locked.";

        pkt.jtag_target_vref_volts = 3.315;
        pkt.jtag_tck_frequency_mhz = 12.0;
        // Alternate detected IDCODE to illustrate both MCU (0x4BA00477) and Artix-7 (0x0362D093) scan chain nodes
        if ((seq_ / 20) % 2 == 0) {
            pkt.jtag_tap_detected_idcode = 0x4BA00477;
            pkt.jtag_tap_device_name = "ARM CoreSight SW-DP / JTAG-DP (STM32 Cortex-M7 TAP)";
        } else {
            pkt.jtag_tap_detected_idcode = 0x0362D093;
            pkt.jtag_tap_device_name = "Xilinx Artix-7 XC7A35T Boundary-Scan TAP";
        }
        pkt.jtag_packets_transferred = static_cast<uint32_t>(seq_ * 48);

        return pkt;
    }

    void inject_fault(SimulatedFaultType fault) override {
        fault_ = fault;
        state_ = DeviceConnectionState::ERROR_FAULTED;
    }

    bool recover_fault() override {
        // Recovery procedure: Re-sense Target Vref, drive TAP to Test-Logic-Reset (5 TCK with TMS=1), re-read IDCODE
        fault_ = SimulatedFaultType::NONE;
        state_ = DeviceConnectionState::CONNECTED_READY;
        return true;
    }

private:
    DeviceConnectionState state_ = DeviceConnectionState::DISCONNECTED;
    SimulatedFaultType fault_ = SimulatedFaultType::NONE;
    uint64_t seq_ = 0;
};

// -------------------------------------------------------------
// Embedded Hardware Manager
// -------------------------------------------------------------
EmbeddedHardwareManager::EmbeddedHardwareManager() {
    start_time_ = std::chrono::steady_clock::now();
    devices_["mcu_stm32h7"] = std::make_shared<McuMockAdapter>();
    devices_["fpga_artix7"] = std::make_shared<FpgaMockAdapter>();
    devices_["jtag_jlink"] = std::make_shared<JtagMockAdapter>();
}

std::vector<DeviceInfo> EmbeddedHardwareManager::discover_devices() {
    std::vector<DeviceInfo> list;
    for (const auto& pair : devices_) {
        list.push_back(pair.second->get_info());
    }
    return list;
}

DeviceInfo EmbeddedHardwareManager::get_device_info(const std::string& device_id) {
    auto it = devices_.find(device_id);
    if (it != devices_.end()) {
        return it->second->get_info();
    }
    return DeviceInfo();
}

bool EmbeddedHardwareManager::connect_device(const std::string& device_id) {
    auto it = devices_.find(device_id);
    if (it != devices_.end()) {
        return it->second->connect();
    }
    return false;
}

bool EmbeddedHardwareManager::disconnect_device(const std::string& device_id) {
    auto it = devices_.find(device_id);
    if (it != devices_.end()) {
        return it->second->disconnect();
    }
    return false;
}

TelemetryPacket EmbeddedHardwareManager::read_device_telemetry(const std::string& device_id) {
    auto it = devices_.find(device_id);
    if (it != devices_.end()) {
        return it->second->read_telemetry();
    }
    TelemetryPacket empty;
    empty.device_id = device_id;
    empty.state = DeviceConnectionState::DISCONNECTED;
    empty.status_message = "Device not found.";
    return empty;
}

void EmbeddedHardwareManager::inject_device_fault(const std::string& device_id, SimulatedFaultType fault) {
    auto it = devices_.find(device_id);
    if (it != devices_.end()) {
        it->second->inject_fault(fault);
    }
}

bool EmbeddedHardwareManager::recover_device_fault(const std::string& device_id) {
    auto it = devices_.find(device_id);
    if (it != devices_.end()) {
        return it->second->recover_fault();
    }
    return false;
}

std::string EmbeddedHardwareManager::get_architecture_summary_json() const {
    std::ostringstream ss;
    ss << "{"
       << "\"system_name\":\"Modular Host-Connected Engineering Workbench\","
       << "\"is_simulated\":true,"
       << "\"safe_integration_plan\":\"1. Verify single point star grounding between host PC and test instruments; 2. Ensure target Vref sensing is connected prior to asserting TCK clock lines; 3. Verify bidirectional level shifters match 1.8V (FPGA auxiliary) and 3.3V (MCU I/O) domains; 4. Use USB opto-isolators for ground loop prevention on high-noise power rails.\","
       << "\"jtag_architectural_explanation\":\"A JTAG debug adapter (IEEE 1149.1 / 1149.7) is a synchronous, state-machine driven debug and boundary-scan interface, NOT an interchangeable general-purpose compute node. It functions as a bus master driving Test Access Port (TAP) signals (TCK, TMS, TDI) while capturing TDO to access internal scan chains, On-Chip Emulation (CoreSight/ICE), instruction registers, and boundary-scan cells. It does not execute application workloads or provide general peripheral endpoints, and requires strict physical layer separation from application host buses.\""
       << "}";
    return ss.str();
}

} // namespace workbench
