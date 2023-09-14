#ifndef FILEMANAGER_h
#define FILEMANAGER_h

#include <FS.h>

class FileManager {
  public:
    FileManager();

    File getFile(String path);

  private:
  
};

#endif
