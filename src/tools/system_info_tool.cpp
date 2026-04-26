#include <kasli/tools/system_info_tool.hpp>

#include <fstream>
#include <sstream>
#include <string>

#include <sys/utsname.h>

namespace kasli::tools {

SystemInfoTool::SystemInfoTool(std::filesystem::path os_release_path)
    : os_release_path_(std::move(os_release_path)) {}

std::string SystemInfoTool::name() const {
  return "system.info";
}

core::RiskClass SystemInfoTool::risk() const {
  return core::RiskClass::ReadOnly;
}

core::ToolResponse SystemInfoTool::call(const core::ToolRequest& request) const {
  std::ostringstream body;

  std::ifstream os_release(os_release_path_);
  if (os_release) {
    body << os_release.rdbuf();
  } else {
    body << "os_release=unavailable\n";
  }

  utsname uname_info{};
  if (uname(&uname_info) == 0) {
    body << "sysname=" << uname_info.sysname << '\n';
    body << "release=" << uname_info.release << '\n';
    body << "machine=" << uname_info.machine << '\n';
  }

  return core::ToolResponse{
      .request_id = request.id,
      .status = core::ToolStatus::Ok,
      .message = "system info collected",
      .evidence = {core::Evidence{
          .id = request.id + ":system.info",
          .source = "system.info",
          .summary = "OS identity and kernel summary",
          .body = body.str(),
          .timestamp = core::utc_timestamp(),
      }},
  };
}

}  // namespace kasli::tools
