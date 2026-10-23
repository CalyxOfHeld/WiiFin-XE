#pragma once
/* A DOL started in WiiFin's place: WiiFin's own again (Reload.cpp), or
 * another app (WiiMC-SS XE, Local Media). */
#include <string>
#include <vector>

namespace Reload {

/* the DOL at path read, put in place and started, args after its argv[0];
 * false (and nothing changed) if it cannot be read */
bool launch(const char* path, const std::vector<std::string>& args);
bool self(const char* path);

/* the arguments an app's meta.xml gives it */
std::vector<std::string> metaArgs(const char* metaPath);

}
