#ifndef BITCOIN_VERSIONBITSINFO_H
#define BITCOIN_VERSIONBITSINFO_H

struct VBDeploymentInfo {
  const char *name;
  bool gbt_force;
  bool check_mn_protocol;
};

extern const struct VBDeploymentInfo VersionBitsDeploymentInfo[];

#endif