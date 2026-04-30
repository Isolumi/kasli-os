#include <catch2/catch_test_macros.hpp>
#include <kasli/tools/hardware_tool.hpp>

#include <string>
#include <vector>

TEST_CASE("hardware summary tool metadata is read-only") {
  kasli::tools::HardwareSummaryTool tool([] {
    return kasli::tools::detail::HardwareSummaryResult{
        .ok = true,
        .summary = kasli::tools::detail::HardwareSummary{.architecture = "x86_64"},
    };
  });

  REQUIRE(tool.name() == "hardware.summary");
  REQUIRE(tool.risk() == kasli::core::RiskClass::ReadOnly);
}

TEST_CASE("hardware summary parses cpuinfo") {
  const auto cpu = kasli::tools::detail::parse_cpuinfo_summary(
      "processor\t: 0\n"
      "vendor_id\t: GenuineIntel\n"
      "model name\t: Example CPU\n"
      "physical id\t: 0\n"
      "core id\t\t: 0\n"
      "cpu cores\t: 2\n"
      "\n"
      "processor\t: 1\n"
      "vendor_id\t: GenuineIntel\n"
      "model name\t: Example CPU\n"
      "physical id\t: 0\n"
      "core id\t\t: 1\n"
      "cpu cores\t: 2\n");

  REQUIRE(cpu.vendor_id == "GenuineIntel");
  REQUIRE(cpu.model_name == "Example CPU");
  REQUIRE(cpu.logical_processors == 2);
  REQUIRE(cpu.physical_cores == 2);
}

TEST_CASE("hardware summary sums socket core counts when core ids are absent") {
  const auto cpu = kasli::tools::detail::parse_cpuinfo_summary(
      "processor\t: 0\n"
      "vendor_id\t: GenuineIntel\n"
      "model name\t: Example CPU\n"
      "physical id\t: 0\n"
      "cpu cores\t: 8\n"
      "\n"
      "processor\t: 1\n"
      "vendor_id\t: GenuineIntel\n"
      "model name\t: Example CPU\n"
      "physical id\t: 1\n"
      "cpu cores\t: 8\n");

  REQUIRE(cpu.logical_processors == 2);
  REQUIRE(cpu.physical_cores == 16);
}

TEST_CASE("hardware summary parses meminfo kilobyte values") {
  const auto memory = kasli::tools::detail::parse_meminfo_summary(
      "MemTotal:       1024 kB\n"
      "MemAvailable:    512 kB\n"
      "SwapTotal:       256 kB\n");

  REQUIRE(memory.total_bytes == 1024ULL * 1024ULL);
  REQUIRE(memory.available_bytes == 512ULL * 1024ULL);
  REQUIRE(memory.swap_total_bytes == 256ULL * 1024ULL);
}

TEST_CASE("hardware summary input truncation is bounded and line aligned") {
  std::string input(70 * 1024, 'a');
  input[64 * 1024 - 1] = '\n';

  const auto truncated = kasli::tools::detail::truncate_hardware_input(input);

  REQUIRE(truncated.size() == 64 * 1024);
  REQUIRE(truncated.back() == '\n');
}

TEST_CASE("hardware summary body is bounded and marks extra GPUs") {
  const auto body = kasli::tools::detail::format_hardware_summary_body(
      kasli::tools::detail::HardwareSummary{
          .architecture = "x86_64",
          .cpu = kasli::tools::detail::CpuSummary{
              .vendor_id = "AuthenticAMD",
              .model_name = "Example CPU",
              .logical_processors = 24,
              .physical_cores = 12,
          },
          .memory = kasli::tools::detail::MemorySummary{
              .total_bytes = 64ULL * 1024ULL * 1024ULL * 1024ULL,
              .available_bytes = 48ULL * 1024ULL * 1024ULL * 1024ULL,
              .swap_total_bytes = 8ULL * 1024ULL * 1024ULL * 1024ULL,
          },
          .dmi = kasli::tools::detail::DmiSummary{
              .system_vendor = "Example Vendor",
              .product_name = "Example Workstation",
              .board_vendor = "Example Board Vendor",
              .board_name = "Example Board",
              .bios_vendor = "Example BIOS",
              .bios_version = "1.2.3",
              .chassis_type = "3",
          },
          .gpu_devices = {
              kasli::tools::detail::GpuDeviceRow{
                  .name = "card1",
                  .vendor_id = "0x1002",
                  .device_id = "0x13c0",
                  .class_id = "0x030000",
                  .driver = "amdgpu",
                  .source = "sysfs",
              },
              kasli::tools::detail::GpuDeviceRow{
                  .name = "card2",
                  .vendor_id = "0x10de",
                  .device_id = "0x2b85",
                  .class_id = "0x030000",
                  .driver = "nvidia",
                  .source = "sysfs",
              },
          },
          .has_more_gpu_devices = true,
      });

  REQUIRE(body.find("hardware_summary=1") != std::string::npos);
  REQUIRE(body.find("architecture=x86_64") != std::string::npos);
  REQUIRE(body.find("cpu_vendor=AuthenticAMD") != std::string::npos);
  REQUIRE(body.find("cpu_model=Example CPU") != std::string::npos);
  REQUIRE(body.find("cpu_logical_processors=24") != std::string::npos);
  REQUIRE(body.find("cpu_physical_cores=12") != std::string::npos);
  REQUIRE(body.find("memory_total_bytes=68719476736") != std::string::npos);
  REQUIRE(body.find("system_vendor=Example Vendor") != std::string::npos);
  REQUIRE(body.find("gpu_devices_count=2") != std::string::npos);
  REQUIRE(body.find("gpu=name=card2 vendor_id=0x10de device_id=0x2b85 "
                    "class=0x030000 driver=nvidia source=sysfs") != std::string::npos);
  REQUIRE(body.find("truncated=true") != std::string::npos);
}

TEST_CASE("hardware summary tool reads injected provider output") {
  kasli::tools::HardwareSummaryTool tool([] {
    return kasli::tools::detail::HardwareSummaryResult{
        .ok = true,
        .summary = kasli::tools::detail::HardwareSummary{
            .architecture = "x86_64",
            .cpu = kasli::tools::detail::CpuSummary{
                .vendor_id = "AuthenticAMD",
                .model_name = "Example CPU",
                .logical_processors = 24,
                .physical_cores = 12,
            },
        },
    };
  });

  auto response = tool.call(kasli::core::ToolRequest{
      .id = "req-hardware-summary",
      .tool_name = "hardware.summary",
      .risk = kasli::core::RiskClass::ReadOnly,
  });

  REQUIRE(response.status == kasli::core::ToolStatus::Ok);
  REQUIRE(response.message == "hardware summary collected");
  REQUIRE(response.evidence.size() == 1);
  REQUIRE(response.evidence.front().source == "hardware.summary");
  REQUIRE(response.evidence.front().body.find("hardware_summary=1") != std::string::npos);
  REQUIRE(response.evidence.front().body.find("cpu_logical_processors=24") !=
          std::string::npos);
}

TEST_CASE("hardware summary tool reports provider errors") {
  kasli::tools::HardwareSummaryTool tool([] {
    return kasli::tools::detail::HardwareSummaryResult{
        .ok = false,
        .error = "hardware summary is not available",
    };
  });

  auto response = tool.call(kasli::core::ToolRequest{
      .id = "req-hardware-summary-error",
      .tool_name = "hardware.summary",
      .risk = kasli::core::RiskClass::ReadOnly,
  });

  REQUIRE(response.status == kasli::core::ToolStatus::Error);
  REQUIRE(response.message == "hardware summary is not available");
  REQUIRE(response.evidence.empty());
}
