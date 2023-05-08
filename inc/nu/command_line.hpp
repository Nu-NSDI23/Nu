#pragma once

#include <boost/program_options.hpp>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "nu/commons.hpp"

namespace nu {

struct OptionsDesc {
  boost::program_options::options_description desc;
  boost::program_options::variables_map vm;
  std::vector<std::pair<std::string, std::string>> either_constraints;

  OptionsDesc(std::string desc_str, bool help);
  void parse(int argc, char **argv);
  void add_either_constraint(std::string opt1, std::string opt2);
};

struct NuOptionsDesc : public OptionsDesc {
  std::string ctrl_ip_str;
  lpid_t lpid;

  NuOptionsDesc(bool help = true);
};

struct CaladanOptionsDesc : public OptionsDesc {
  std::string conf_path;
  int kthreads;
  int guaranteed;
  int spinning;
  std::string ip;
  std::string netmask;
  std::string gateway;

  CaladanOptionsDesc(int default_guaranteed = 0, int default_spinning = 0,
                     std::optional<std::string> default_ip = std::nullopt,
                     bool help = true);
};

struct AllOptionsDesc : public OptionsDesc {
  NuOptionsDesc nu;
  CaladanOptionsDesc caladan;

  AllOptionsDesc();
};

void write_options_to_file(std::string path, const AllOptionsDesc &desc);
void write_options_to_file(std::string path, const CaladanOptionsDesc &desc);
}  // namespace nu
