#include <versionbitsinfo.h>

#include <consensus/params.h>

const struct VBDeploymentInfo VersionBitsDeploymentInfo[Consensus::MAX_VERSION_BITS_DEPLOYMENTS] = {
  {
    /*.name =*/ "testdummy",
    /*.gbt_force =*/ true,
    /*.check_mn_protocol =*/ false,
  },
  {
    /*.name =*/ "v17",
    /*.gbt_force =*/ true,
    /*.check_mn_protocol =*/ false,
  },
};