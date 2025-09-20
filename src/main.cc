#include "sim/config.h"
#include "sim/workload.h"
#include <iostream>

int main(int argc, char** argv){
  std::string cfg = (argc>1? argv[1] : "data/sim.yaml");
  SimConf conf = LoadConfig(cfg);

  std::string iname = "Sherman";
  std::cout << "=== Index=" << iname << " ===\n";
  WorkloadRunner R(conf);
  for (const auto& wl : conf.workloads){
    R.run_workload(wl, iname, conf.metrics.out_dir);
  }
  std::cout << "Done. Check " << conf.metrics.out_dir << " for CSV outputs." << std::endl;
  return 0;
}
