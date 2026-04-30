#include <catch2/catch_test_macros.hpp>
#include <kasli/tools/disk_tool.hpp>

#include <string>
#include <vector>

TEST_CASE("disk usage tool metadata is read-only") {
  kasli::tools::DiskUsageTool tool([] {
    return kasli::tools::detail::DiskUsageResult{.ok = true};
  });

  REQUIRE(tool.name() == "disk.usage");
  REQUIRE(tool.risk() == kasli::core::RiskClass::ReadOnly);
}

TEST_CASE("disk usage parser extracts mountinfo rows") {
  const auto mount = kasli::tools::detail::parse_mountinfo_line(
      "72 1 0:33 /root / rw,relatime shared:1 - btrfs /dev/nvme1n1p3 rw,seclabel");

  REQUIRE(mount.has_value());
  REQUIRE(mount->mount_point == "/");
  REQUIRE(mount->fs_type == "btrfs");
  REQUIRE(mount->source == "/dev/nvme1n1p3");
}

TEST_CASE("disk usage parser decodes mountinfo octal escapes") {
  const auto mount = kasli::tools::detail::parse_mountinfo_line(
      "100 72 8:1 / /media/My\\040Disk rw,relatime - ext4 LABEL=My\\040Disk rw");

  REQUIRE(mount.has_value());
  REQUIRE(mount->mount_point == "/media/My Disk");
  REQUIRE(mount->fs_type == "ext4");
  REQUIRE(mount->source == "LABEL=My Disk");
}

TEST_CASE("disk usage parser ignores malformed mountinfo rows") {
  REQUIRE_FALSE(kasli::tools::detail::parse_mountinfo_line("72 1 0:33 /root /").has_value());
  REQUIRE_FALSE(kasli::tools::detail::parse_mountinfo_line(
                    "72 1 0:33 /root / rw,relatime btrfs /dev/nvme1n1p3 rw")
                    .has_value());
}

TEST_CASE("disk usage mount candidate filter skips virtual filesystems") {
  REQUIRE(kasli::tools::detail::is_disk_usage_mount_candidate(
      kasli::tools::detail::DiskMountInfo{
          .mount_point = "/",
          .fs_type = "btrfs",
          .source = "/dev/nvme1n1p3",
      }));
  REQUIRE_FALSE(kasli::tools::detail::is_disk_usage_mount_candidate(
      kasli::tools::detail::DiskMountInfo{
          .mount_point = "/proc",
          .fs_type = "proc",
          .source = "proc",
      }));
  REQUIRE_FALSE(kasli::tools::detail::is_disk_usage_mount_candidate(
      kasli::tools::detail::DiskMountInfo{
          .mount_point = "/run",
          .fs_type = "tmpfs",
          .source = "tmpfs",
      }));
}

TEST_CASE("disk usage body is bounded and marks extra rows") {
  const auto body = kasli::tools::detail::format_disk_usage_body(
      std::vector<kasli::tools::detail::DiskUsageRow>{
          {
              .mount_point = "/",
              .fs_type = "btrfs",
              .source = "/dev/nvme1n1p3",
              .capacity_bytes = 1000,
              .free_bytes = 500,
              .available_bytes = 400,
              .used_percent = 60,
          },
          {
              .mount_point = "/boot",
              .fs_type = "ext4",
              .source = "/dev/nvme1n1p2",
              .capacity_bytes = 2000,
              .free_bytes = 1500,
              .available_bytes = 1400,
              .used_percent = 30,
          },
      },
      true);

  REQUIRE(body.find("disk_mounts_count=2") != std::string::npos);
  REQUIRE(body.find("mount_point=/ fs_type=btrfs source=/dev/nvme1n1p3 capacity_bytes=1000 "
                    "free_bytes=500 available_bytes=400 used_percent=60") != std::string::npos);
  REQUIRE(body.find("mount_point=/boot fs_type=ext4 source=/dev/nvme1n1p2") !=
          std::string::npos);
  REQUIRE(body.find("truncated=true") != std::string::npos);
}

TEST_CASE("disk usage body reports when no mounts are found") {
  const auto body = kasli::tools::detail::format_disk_usage_body({}, false);

  REQUIRE(body.find("disk_mounts_count=0") != std::string::npos);
  REQUIRE(body.find("no_disk_mounts=true") != std::string::npos);
  REQUIRE(body.find("truncated=true") == std::string::npos);
}

TEST_CASE("disk usage tool reads injected provider rows") {
  kasli::tools::DiskUsageTool tool([] {
    return kasli::tools::detail::DiskUsageResult{
        .ok = true,
        .rows = {kasli::tools::detail::DiskUsageRow{
            .mount_point = "/",
            .fs_type = "btrfs",
            .source = "/dev/nvme1n1p3",
            .capacity_bytes = 1000,
            .free_bytes = 500,
            .available_bytes = 400,
            .used_percent = 60,
        }},
    };
  });

  auto response = tool.call(kasli::core::ToolRequest{
      .id = "req-disk-usage",
      .tool_name = "disk.usage",
      .risk = kasli::core::RiskClass::ReadOnly,
  });

  REQUIRE(response.status == kasli::core::ToolStatus::Ok);
  REQUIRE(response.message == "disk usage listed");
  REQUIRE(response.evidence.size() == 1);
  REQUIRE(response.evidence.front().source == "disk.usage");
  REQUIRE(response.evidence.front().body.find("disk_mounts_count=1") != std::string::npos);
  REQUIRE(response.evidence.front().body.find("mount_point=/") != std::string::npos);
}

TEST_CASE("disk usage tool reports provider errors") {
  kasli::tools::DiskUsageTool tool([] {
    return kasli::tools::detail::DiskUsageResult{
        .ok = false,
        .error = "mountinfo is not available",
    };
  });

  auto response = tool.call(kasli::core::ToolRequest{
      .id = "req-disk-usage-error",
      .tool_name = "disk.usage",
      .risk = kasli::core::RiskClass::ReadOnly,
  });

  REQUIRE(response.status == kasli::core::ToolStatus::Error);
  REQUIRE(response.message == "mountinfo is not available");
  REQUIRE(response.evidence.empty());
}
