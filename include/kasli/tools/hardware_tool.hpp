#pragma once

#include <kasli/tools/tool.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace kasli::tools {

namespace detail {

struct CpuSummary {
  std::string vendor_id;
  std::string model_name;
  std::size_t logical_processors = 0;
  std::size_t physical_cores = 0;
};

struct MemorySummary {
  std::uintmax_t total_bytes = 0;
  std::uintmax_t available_bytes = 0;
  std::uintmax_t swap_total_bytes = 0;
};

struct DmiSummary {
  std::string system_vendor;
  std::string product_name;
  std::string product_version;
  std::string board_vendor;
  std::string board_name;
  std::string bios_vendor;
  std::string bios_version;
  std::string chassis_type;
};

struct GpuDeviceRow {
  std::string name;
  std::string vendor_id;
  std::string device_id;
  std::string class_id;
  std::string driver;
  std::string source;
};

struct HardwareSummary {
  std::string architecture;
  CpuSummary cpu;
  MemorySummary memory;
  DmiSummary dmi;
  std::vector<GpuDeviceRow> gpu_devices;
  bool has_more_gpu_devices = false;
};

struct HardwareSummaryResult {
  bool ok = false;
  std::string error;
  HardwareSummary summary;
};

CpuSummary parse_cpuinfo_summary(std::string_view content);
MemorySummary parse_meminfo_summary(std::string_view content);
std::string truncate_hardware_input(std::string content);
std::string format_hardware_summary_body(const HardwareSummary& summary);

}  // namespace detail

using HardwareSummaryProvider = std::function<detail::HardwareSummaryResult()>;

HardwareSummaryProvider default_hardware_summary_provider();

class HardwareSummaryTool final : public Tool {
 public:
  explicit HardwareSummaryTool(HardwareSummaryProvider provider =
                                   default_hardware_summary_provider());

  std::string name() const override;
  core::RiskClass risk() const override;
  core::ToolResponse call(const core::ToolRequest& request) const override;

 private:
  HardwareSummaryProvider provider_;
};

}  // namespace kasli::tools
