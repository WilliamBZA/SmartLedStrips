#include <FS.h>
#include <SPIFFS.h>
#include "filemanager.h"

FileManager::FileManager() {
  SPIFFS.begin();
}

File FileManager::getFile(String path) {
  return SPIFFS.open(path.c_str(), "r");
}
