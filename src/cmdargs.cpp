// cmdargs.cpp

#include "cmdargs.hpp"

#include <algorithm>
#include <cassert>
#include <charconv>
#include <cstring>
#include <iomanip>
#include <iostream>

#include <getopt.h>

using namespace tep;

extern int opterr;
extern int optopt;
extern int optind;
extern char *optarg;

namespace {
std::string_view cpu_sensors_str{"cpu-sensors"};
std::string_view cpu_sockets_str{"cpu-sockets"};
std::string_view gpu_devices_str{"gpu-devices"};

std::optional<unsigned long long> parse_mask_argument(std::string_view option,
                                                      std::string_view value) {
  if (value == "all")
    return ~0x0ULL;
  unsigned long long retval;
  auto [ptr, ec] = std::from_chars(value.begin(), value.end(), retval, 16);
  if (auto err = std::make_error_code(ec)) {
    std::cerr << "--" << option << ": " << err << "\n";
    return std::nullopt;
  }
  if (ptr != value.end()) {
    std::cerr << "--" << option << ": "
              << "invalid hexadecimal characters in '" << value << "'"
              << "\n";
    return std::nullopt;
  }
  return retval;
}

struct parameter {
  inline static const auto pad = std::setw(30);

  const char *text;

  friend std::ostream &operator<<(std::ostream &os, const parameter &p) {
    os << "  " << std::left << pad << p.text;
    return os;
  }
};
} // namespace

optional_output_file::optional_output_file(const std::string &path) : _file() {
  if (!path.empty())
    _file.open(path);
}

optional_output_file::operator bool() const { return bool(_file); }

optional_output_file::operator std::ostream &() {
  if (_file && !_file.is_open())
    return std::cout;
  return _file;
}

optional_input_file::optional_input_file(const std::string &path) : _file() {
  if (!path.empty())
    _file.open(path);
}

bool arguments::same_target() const { return target == argv[0]; }

optional_input_file::operator bool() const { return bool(_file); }

optional_input_file::operator std::istream &() {
  if (_file && !_file.is_open())
    return std::cin;
  return _file;
}

std::ostream &tep::operator<<(std::ostream &os, const optional_output_file &f) {
  if (!f)
    os << "failed to open";
  else if (f._file.is_open())
    os << "file";
  else
    os << "stdout";
  return os;
}

std::ostream &tep::operator<<(std::ostream &os, const optional_input_file &f) {
  if (!f)
    os << "failed to open";
  else if (f._file.is_open())
    os << "file";
  else
    os << "stdin";
  return os;
}

std::ostream &tep::operator<<(std::ostream &os, const arguments &args) {
  os << "flags: " << args.profiler_flags;
  os << ", output: " << args.output;
  os << ", config: " << args.config;
  os << ", exec: " << args.target;
  return os;
}

void print_usage(const char *profiler_name) {
  std::cout << "Usage:\n\n";
  std::cout << profiler_name << " <options> [--] <executable>\n\n";

  std::ios::fmtflags flags(std::cout.flags());

  std::cout << "options:\n";

  std::cout << parameter{"-h, --help"} << "print this message and exit"
            << "\n";

  std::cout << parameter{"-c, --config <file>"}
            << "(optional) read from configuration file <file>; "
            << "if <file> is 'stdin' then stdin is used (default: stdin)"
            << "\n";

  std::cout << parameter{"-o, --output <file>"}
            << "(optional) write profiling results to <file>; "
            << "if <file> is 'stdout' then stdout is used (default: stdout)"
            << "\n";

  std::cout << parameter{"-q, --quiet"}
            << "suppress log messages except errors to stderr (default: off)"
            << "\n";

  std::cout << parameter{"-l, --log <file>"}
            << "(optional) write log to <file> (default: stdout)"
            << "\n";

  std::cout << parameter{"--debug-dump <file>"}
            << "(optional) dump gathered debug info in JSON format to <file>"
            << "\n";

  std::cout << parameter{"--idle"}
            << "gather idle readings at startup (default)"
            << "\n";

  std::cout << parameter{"--no-idle"} << "opposite of --idle"
            << "\n";

  std::cout << parameter{"--cpu-sensors {MASK,all}"}
            << "mask of CPU sensors to read in hexadecimal, "
            << "overwrites config value (default: use value in config)"
            << "\n";

  std::cout << parameter{"--cpu-sockets {MASK,all}"}
            << "mask of CPU sockets to profile in hexadecimal, "
            << "overwrites config value (default: use value in config)"
            << "\n";

  std::cout << parameter{"--gpu-devices {MASK,all}"}
            << "mask of GPU devices to profile in hexadecimal, "
            << "overwrites config value (default: use value in config)"
            << "\n";

  std::cout << parameter{"--exec <path>"}
            << "evaluate executable <path> instead of <executable>; "
            << "used when <executable> is some wrapper program "
            << "which launches <path> (default: <executable>)"
            << "\n";

  std::cout.flush();
  std::cout.flags(flags);
}

std::optional<arguments> tep::parse_arguments(int argc, char *const argv[]) {
  int c;
  int option_index = 0;
  int idle = 1;
  bool quiet = false;
  std::string output;
  std::string config;
  std::string logpath;
  std::string executable;
  std::string debug_dump;

  unsigned long long cpu_sensors = 0;
  unsigned long long cpu_sockets = 0;
  unsigned long long gpu_devices = 0;

  struct option long_options[] = {
      {"help", no_argument, nullptr, 'h'},
      {"idle", no_argument, &idle, 1},
      {"no-idle", no_argument, &idle, 0},
      {"config", required_argument, nullptr, 'c'},
      {"output", required_argument, nullptr, 'o'},
      {"quiet", no_argument, nullptr, 'q'},
      {"log", required_argument, nullptr, 'l'},
      {cpu_sensors_str.data(), required_argument, nullptr, 0x100},
      {cpu_sockets_str.data(), required_argument, nullptr, 0x101},
      {gpu_devices_str.data(), required_argument, nullptr, 0x102},
      {"exec", required_argument, nullptr, 0x103},
      {"debug-dump", required_argument, nullptr, 0x104},
      {nullptr, 0, nullptr, 0}};

  while ((c = getopt_long(argc, argv, "hqc:o:l:", long_options,
                          &option_index)) != -1) {
    switch (c) {
    case 0:
      // do nothing
      break;
    case 0x100: {
      auto parsed_value =
          parse_mask_argument(long_options[option_index].name, optarg);
      if (!parsed_value)
        return std::nullopt;
      cpu_sensors = *parsed_value;
    } break;
    case 0x101: {
      auto parsed_value =
          parse_mask_argument(long_options[option_index].name, optarg);
      if (!parsed_value)
        return std::nullopt;
      cpu_sockets = *parsed_value;
    } break;
    case 0x102: {
      auto parsed_value =
          parse_mask_argument(long_options[option_index].name, optarg);
      if (!parsed_value)
        return std::nullopt;
      gpu_devices = *parsed_value;
    } break;
    case 0x103:
      executable = optarg;
      if (executable.empty()) {
        std::cerr << "--" << long_options[option_index].name
                  << " cannot be empty\n";
        return std::nullopt;
      }
      break;
    case 0x104:
      debug_dump = optarg;
      if (debug_dump.empty()) {
        std::cerr << "--" << long_options[option_index].name
                  << " cannot be empty\n";
        return std::nullopt;
      }
      break;
    case 'c':
      config = optarg;
      break;
    case 'o':
      output = optarg;
      break;
    case 'l':
      logpath = optarg;
      break;
    case 'q':
      quiet = true;
      break;
    case 'h':
    case '?':
      // getopt already printed and error message
      print_usage(argv[0]);
      return std::nullopt;
    default:
      assert(false);
    }
  }

  if (optind == argc) {
    std::cerr << "missing target executable name\n";
    return std::nullopt;
  }

  if (quiet && !logpath.empty()) {
    std::cerr << "both -q/--quiet and -l/--log provided\n";
    return std::nullopt;
  }

  auto create_debug_dump = [](const std::string &path) {
    if (path.empty())
      return std::ofstream{};
    return std::ofstream{path};
  };

  optional_output_file of(output);
  optional_input_file cfg(config);
  std::ofstream dd = create_debug_dump(debug_dump);

  if (!of) {
    std::cerr << "error opening output file '" << output
              << "': " << strerror(errno) << "\n";
    return std::nullopt;
  }
  if (!cfg) {
    std::cerr << "error opening config file '" << config
              << "': " << strerror(errno) << "\n";
    return std::nullopt;
  }
  if (!dd) {
    std::cerr << "error opening debug dump file '" << debug_dump
              << "': " << strerror(errno) << "\n";
    return std::nullopt;
  }

  if (executable.empty()) {
    executable = argv[optind];
  } else {
    auto it = std::find_if(
        argv + optind, argv + argc,
        [&executable](const char *arg) { return executable == arg; });
    if (it == argv + argc) {
      std::cerr << "Invalid --exec: '" << executable
                << "' not found in executable arguments\n";
      return std::nullopt;
    }
  }

  return arguments{flags{bool(idle), cpu_sensors, cpu_sockets, gpu_devices},
                   std::move(config),
                   std::move(of),
                   std::move(dd),
                   log_args{bool(quiet), std::move(logpath)},
                   std::move(executable),
                   &argv[optind]};
}
