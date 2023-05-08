#include <cstdlib>
#include <iostream>
#include <fstream>
#include <numa.h>

#include "nu/command_line.hpp"

namespace nu {

const auto kDescDefaultLen =
    boost::program_options::options_description::m_default_line_length;

OptionsDesc::OptionsDesc(std::string desc_str, bool help)
    : desc(desc_str, kDescDefaultLen * 2, kDescDefaultLen) {
  if (help) {
    desc.add_options()("help,h", "print help");
  }
}

void check_either_constraint(const boost::program_options::variables_map &vm,
                             const std::string &opt1, const std::string &opt2) {
  if (vm.count(opt1) && !vm[opt1].defaulted() && vm.count(opt2) &&
      !vm[opt2].defaulted()) {
    throw std::logic_error(std::string("Both '") + opt1 + "' and '" + opt2 +
                           "' are specified.");
  }
  if (!vm.count(opt1) && !vm[opt1].defaulted() && !vm.count(opt2) &&
      !vm[opt2].defaulted()) {
    throw std::logic_error(std::string("Neither '") + opt1 + "' nor '" + opt2 +
                           "' is specified.");
  }
}

void OptionsDesc::parse(int argc, char **argv) {
  try {
    boost::program_options::store(parse_command_line(argc, argv, desc), vm);
    boost::program_options::notify(vm);
    if (vm.count("help")) {
      std::cout << desc << std::endl;
      exit(0);
    }
    for (auto &c : either_constraints) {
      check_either_constraint(vm, c.first, c.second);
    }
  } catch (std::exception &e) {
    std::cout << desc << std::endl;
    std::cerr << e.what() << std::endl;
    exit(-EINVAL);
  }
}

void OptionsDesc::add_either_constraint(std::string opt1, std::string opt2) {
  either_constraints.emplace_back(opt1, opt2);
}

NuOptionsDesc::NuOptionsDesc(bool help) : OptionsDesc("Nu arguments", help) {
  desc.add_options()
    ("main,m", "execute the main function")
    ("controller,t", boost::program_options::value(&ctrl_ip_str)->default_value("18.18.1.1"), "controller ip")
    ("lpid,l", boost::program_options::value(&lpid)->required(), "logical process id (receive a free id if passing 0)")
    ("nomemps", "don't react to memory pressure")
    ("nocpups", "don't react to CPU pressure")
    ("isol", "as an isolated node");
}

CaladanOptionsDesc::CaladanOptionsDesc(int default_guaranteed,
                                       int default_spinning,
                                       std::optional<std::string> default_ip,
                                       bool help)
    : OptionsDesc("Caladan arguments", help) {
  auto num_cores_per_numa_node =
      numa_num_configured_cpus() / numa_num_configured_nodes();
  auto max_num_kthreads = num_cores_per_numa_node - 2;
  auto ip_opt =
      default_ip
          ? boost::program_options::value(&ip)->default_value(*default_ip)
          : boost::program_options::value(&ip);
  desc.add_options()
    ("conf,f", boost::program_options::value(&conf_path), "caladan configuration file")
    ("kthreads,k", boost::program_options::value(&kthreads)->default_value(max_num_kthreads), "number of kthreads (if conf unspecified)")
    ("guaranteed,g", boost::program_options::value(&guaranteed)->default_value(default_guaranteed), "number of guaranteed kthreads (if conf unspecified)")
    ("spinning,p", boost::program_options::value(&spinning)->default_value(default_spinning), "number of spinning kthreads (if conf unspecified)")
    ("ip,i", ip_opt, "IP address (if conf unspecified)")
    ("netmask,n", boost::program_options::value(&netmask)->default_value("255.255.255.0"), "netmask (if conf unspecified)")
    ("gateway,w", boost::program_options::value(&gateway)->default_value("18.18.1.1"), "gateway address (if conf unspecified)");
  add_either_constraint("conf", "kthreads");
  add_either_constraint("conf", "guaranteed");
  add_either_constraint("conf", "spinning");
  add_either_constraint("conf", "ip");
  add_either_constraint("conf", "netmask");
  add_either_constraint("conf", "gateway");
}

void write_options_to_file(std::string path, const AllOptionsDesc &desc) {
  write_options_to_file(path, desc.caladan);
  std::ofstream ofs(path, std::ios_base::app);
  if (!desc.vm.count("nomemps")) {
    ofs << "runtime_react_mem_pressure 1" << std::endl;
  }
  if (!desc.vm.count("nocpups")) {
    ofs << "runtime_react_cpu_pressure 1" << std::endl;
  }
}

void write_options_to_file(std::string path, const CaladanOptionsDesc &desc) {
  constexpr auto kMTU = 9000;
  constexpr auto kQDelayUs = 10;
  constexpr auto kLogLevel = 0;
  constexpr auto kPriority = "be";

  std::ofstream ofs(path);
  ofs << "host_addr " << desc.ip << std::endl;
  ofs << "host_netmask " << desc.netmask << std::endl;
  ofs << "host_gateway " << desc.gateway << std::endl;
  ofs << "host_mtu " << kMTU << std::endl;
  ofs << "runtime_kthreads " << desc.kthreads << std::endl;
  ofs << "runtime_guaranteed_kthreads " << desc.guaranteed << std::endl;
  ofs << "runtime_spinning_kthreads " << desc.spinning << std::endl;
  ofs << "runtime_priority " << kPriority << std::endl;
  ofs << "runtime_qdelay_us " << kQDelayUs << std::endl;
  ofs << "enable_directpath 1" << std::endl;
  ofs << "log_level " << kLogLevel << std::endl;
}

AllOptionsDesc::AllOptionsDesc()
    : OptionsDesc("Usage: nu_args caladan_args [--] [app_args]", true),
      nu(false),
      caladan(0, 0, std::nullopt, false) {
  desc.add(nu.desc).add(caladan.desc);
  either_constraints.insert(either_constraints.end(),
                            nu.either_constraints.begin(),
                            nu.either_constraints.end());
  either_constraints.insert(either_constraints.end(),
                            caladan.either_constraints.begin(),
                            caladan.either_constraints.end());
}

}  // namespace nu
