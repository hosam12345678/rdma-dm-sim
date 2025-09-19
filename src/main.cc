#include "sim/config.h"
#include "sim/workload.h"
#include <iostream>

int main(int argc, char** argv){
  std::string cfg = (argc>1? argv[1] : "data/sim.yaml");
  SimConf conf = LoadConfig(cfg);

  // Run twice: once as Sherman, once as DEX (you can set via YAML too)
  for (int pass=0; pass<2; ++pass){
    conf.index.kind = (pass==0? IndexKind::Sherman : IndexKind::DEX);
    std::string iname = (pass==0? "Sherman" : "DEX");
    std::cout << "=== Index=" << iname << " ===\n";
    WorkloadRunner R(conf);
    for (const auto& wl : conf.workloads){
      R.run_workload(wl, iname, conf.metrics.out_dir);
    }
  }
  std::cout << "Done. Check " << conf.metrics.out_dir << " for CSV outputs." << std::endl;
  return 0;
}
