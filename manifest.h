struct manifest
{
  char hasApplication;
  const char *applicationDatFileName;
  const char *applicationBinFileName;
  char hasSDBootloader;
  const char *sdBootloaderDatFileName;
  const char *sdBootloaderBinFileName;
  char hasBootloader;
  const char *bootloaderDatFileName;
  const char *bootloaderBinFileName;
};
