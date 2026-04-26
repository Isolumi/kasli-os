#include <kasli/tools/systemd_tool.hpp>

#include <kasli/core/json.hpp>

#include <cstddef>
#include <cstdlib>
#include <cstdint>
#include <string>

#if KASLI_HAS_SYSTEMD
#include <systemd/sd-bus.h>
#endif

namespace kasli::tools {
namespace {

constexpr std::size_t kMaxUnits = 200;
constexpr std::size_t kMaxBodyBytes = 64 * 1024;

void append_bounded(std::string& body, const std::string& text, bool& truncated) {
  if (body.size() + text.size() <= kMaxBodyBytes) {
    body += text;
    return;
  }

  const std::string marker = "truncated=true\n";
  const auto remaining = kMaxBodyBytes - body.size();
  if (remaining > marker.size()) {
    body += text.substr(0, remaining - marker.size());
  } else if (body.size() + marker.size() > kMaxBodyBytes) {
    body.resize(kMaxBodyBytes - marker.size());
  }
  body += marker;
  truncated = true;
}

core::ToolResponse unavailable_response(const core::ToolRequest& request) {
  return core::ToolResponse{
      .request_id = request.id,
      .status = core::ToolStatus::Error,
      .message = "systemd support was not built",
      .evidence = {},
  };
}

bool is_blank(const std::string& value) {
  return value.find_first_not_of(" \t\r\n") == std::string::npos;
}

core::ToolResponse missing_unit_response(const core::ToolRequest& request) {
  return core::ToolResponse{
      .request_id = request.id,
      .status = core::ToolStatus::Error,
      .message = "systemd.unit.status requires a non-empty unit parameter",
      .evidence = {},
  };
}

#if KASLI_HAS_SYSTEMD
struct BusHandle {
  sd_bus* bus = nullptr;
  ~BusHandle() {
    if (bus != nullptr) {
      sd_bus_unref(bus);
    }
  }
};

struct MessageHandle {
  sd_bus_message* message = nullptr;
  ~MessageHandle() {
    if (message != nullptr) {
      sd_bus_message_unref(message);
    }
  }
};

std::string value_or_empty(const char* value) {
  return value == nullptr ? "" : value;
}

std::string format_unit_row(const char* unit,
                            const char* load_state,
                            const char* active_state,
                            const char* sub_state,
                            const char* description) {
  return value_or_empty(unit) + " load=" + value_or_empty(load_state) +
         " active=" + value_or_empty(active_state) + " sub=" + value_or_empty(sub_state) +
         " description=" + core::redact_likely_secret(value_or_empty(description)) + '\n';
}

std::string read_unit_property(sd_bus* bus, const std::string& object_path, const char* property) {
  sd_bus_error error = SD_BUS_ERROR_NULL;
  char* value = nullptr;
  const int result = sd_bus_get_property_string(bus,
                                                "org.freedesktop.systemd1",
                                                object_path.c_str(),
                                                "org.freedesktop.systemd1.Unit",
                                                property,
                                                &error,
                                                &value);
  sd_bus_error_free(&error);
  if (result < 0) {
    return "unavailable";
  }

  std::string output = value_or_empty(value);
  std::free(value);
  return output;
}
#endif

}  // namespace

std::string SystemdUnitsTool::name() const {
  return "systemd.units.list";
}

core::RiskClass SystemdUnitsTool::risk() const {
  return core::RiskClass::ReadOnly;
}

core::ToolResponse SystemdUnitsTool::call(const core::ToolRequest& request) const {
#if KASLI_HAS_SYSTEMD
  BusHandle bus;
  int result = sd_bus_open_system(&bus.bus);
  if (result < 0) {
    return core::ToolResponse{
        .request_id = request.id,
        .status = core::ToolStatus::Error,
        .message = "failed to connect to systemd system bus",
        .evidence = {},
    };
  }

  sd_bus_error error = SD_BUS_ERROR_NULL;
  MessageHandle reply;
  result = sd_bus_call_method(bus.bus,
                              "org.freedesktop.systemd1",
                              "/org/freedesktop/systemd1",
                              "org.freedesktop.systemd1.Manager",
                              "ListUnits",
                              &error,
                              &reply.message,
                              "");
  sd_bus_error_free(&error);
  if (result < 0) {
    return core::ToolResponse{
        .request_id = request.id,
        .status = core::ToolStatus::Error,
        .message = "failed to list systemd units",
        .evidence = {},
    };
  }

  result = sd_bus_message_enter_container(reply.message, SD_BUS_TYPE_ARRAY, "(ssssssouso)");
  if (result < 0) {
    return core::ToolResponse{
        .request_id = request.id,
        .status = core::ToolStatus::Error,
        .message = "failed to read systemd unit list",
        .evidence = {},
    };
  }

  std::string body;
  std::size_t units = 0;
  bool truncated = false;

  while ((result = sd_bus_message_enter_container(reply.message, SD_BUS_TYPE_STRUCT, "ssssssouso")) >
         0) {
    if (units >= kMaxUnits) {
      sd_bus_message_exit_container(reply.message);
      append_bounded(body, "truncated=true\n", truncated);
      break;
    }

    const char* unit = "";
    const char* description = "";
    const char* load_state = "";
    const char* active_state = "";
    const char* sub_state = "";
    const char* following = "";
    const char* object_path = "";
    std::uint32_t job_id = 0;
    const char* job_type = "";
    const char* job_path = "";

    result = sd_bus_message_read(reply.message,
                                 "ssssssouso",
                                 &unit,
                                 &description,
                                 &load_state,
                                 &active_state,
                                 &sub_state,
                                 &following,
                                 &object_path,
                                 &job_id,
                                 &job_type,
                                 &job_path);
    if (result < 0) {
      return core::ToolResponse{
          .request_id = request.id,
          .status = core::ToolStatus::Error,
          .message = "failed to read systemd unit entry",
          .evidence = {},
      };
    }
    sd_bus_message_exit_container(reply.message);

    append_bounded(body,
                   format_unit_row(unit, load_state, active_state, sub_state, description),
                   truncated);
    if (truncated) {
      break;
    }
    ++units;

    (void)following;
    (void)object_path;
    (void)job_id;
    (void)job_type;
    (void)job_path;
  }

  if (result < 0) {
    return core::ToolResponse{
        .request_id = request.id,
        .status = core::ToolStatus::Error,
        .message = "failed to read systemd unit list",
        .evidence = {},
    };
  }

  result = sd_bus_message_exit_container(reply.message);
  if (result < 0) {
    return core::ToolResponse{
        .request_id = request.id,
        .status = core::ToolStatus::Error,
        .message = "failed to finish reading systemd unit list",
        .evidence = {},
    };
  }

  return core::ToolResponse{
      .request_id = request.id,
      .status = core::ToolStatus::Ok,
      .message = "systemd units listed",
      .evidence = {core::Evidence{
          .id = request.id + ":systemd.units.list",
          .source = "systemd.units.list",
          .summary = "bounded systemd unit list",
          .body = body,
          .timestamp = core::utc_timestamp(),
      }},
  };
#else
  return unavailable_response(request);
#endif
}

std::string SystemdUnitStatusTool::name() const {
  return "systemd.unit.status";
}

core::RiskClass SystemdUnitStatusTool::risk() const {
  return core::RiskClass::ReadOnly;
}

core::ToolResponse SystemdUnitStatusTool::call(const core::ToolRequest& request) const {
#if KASLI_HAS_SYSTEMD
  const auto unit = request.params.contains("unit") ? request.params.at("unit") : "";
  if (is_blank(unit)) {
    return missing_unit_response(request);
  }

  BusHandle bus;
  int result = sd_bus_open_system(&bus.bus);
  if (result < 0) {
    return core::ToolResponse{
        .request_id = request.id,
        .status = core::ToolStatus::Error,
        .message = "failed to connect to systemd system bus",
        .evidence = {},
    };
  }

  sd_bus_error error = SD_BUS_ERROR_NULL;
  MessageHandle reply;
  result = sd_bus_call_method(bus.bus,
                              "org.freedesktop.systemd1",
                              "/org/freedesktop/systemd1",
                              "org.freedesktop.systemd1.Manager",
                              "GetUnit",
                              &error,
                              &reply.message,
                              "s",
                              unit.c_str());
  sd_bus_error_free(&error);
  if (result < 0) {
    return core::ToolResponse{
        .request_id = request.id,
        .status = core::ToolStatus::Error,
        .message = "failed to get systemd unit",
        .evidence = {},
    };
  }

  const char* object_path = "";
  result = sd_bus_message_read(reply.message, "o", &object_path);
  if (result < 0 || object_path == nullptr || std::string(object_path).empty()) {
    return core::ToolResponse{
        .request_id = request.id,
        .status = core::ToolStatus::Error,
        .message = "failed to read systemd unit object path",
        .evidence = {},
    };
  }

  const std::string unit_path = object_path;
  std::string body;
  bool truncated = false;
  append_bounded(body, "unit=" + core::redact_likely_secret(unit) + '\n', truncated);
  append_bounded(body,
                 "id=" + core::redact_likely_secret(read_unit_property(bus.bus, unit_path, "Id")) +
                     '\n',
                 truncated);
  append_bounded(
      body,
      "description=" +
          core::redact_likely_secret(read_unit_property(bus.bus, unit_path, "Description")) + '\n',
      truncated);
  append_bounded(body,
                 "load_state=" +
                     core::redact_likely_secret(read_unit_property(bus.bus, unit_path, "LoadState")) +
                     '\n',
                 truncated);
  append_bounded(body,
                 "active_state=" +
                     core::redact_likely_secret(read_unit_property(bus.bus, unit_path, "ActiveState")) +
                     '\n',
                 truncated);
  append_bounded(body,
                 "sub_state=" +
                     core::redact_likely_secret(read_unit_property(bus.bus, unit_path, "SubState")) +
                     '\n',
                 truncated);
  append_bounded(
      body,
      "unit_file_state=" +
          core::redact_likely_secret(read_unit_property(bus.bus, unit_path, "UnitFileState")) + '\n',
      truncated);
  append_bounded(body,
                 "fragment_path=" +
                     core::redact_likely_secret(read_unit_property(bus.bus, unit_path, "FragmentPath")) +
                     '\n',
                 truncated);

  return core::ToolResponse{
      .request_id = request.id,
      .status = core::ToolStatus::Ok,
      .message = "systemd unit status read",
      .evidence = {core::Evidence{
          .id = request.id + ":systemd.unit.status",
          .source = "systemd.unit.status",
          .summary = "bounded systemd status for " + unit,
          .body = body,
          .timestamp = core::utc_timestamp(),
      }},
  };
#else
  return unavailable_response(request);
#endif
}

}  // namespace kasli::tools
