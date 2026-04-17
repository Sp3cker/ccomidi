#ifndef CCOMIDI_SMF_WRITER_H
#define CCOMIDI_SMF_WRITER_H

#include <cstdint>
#include <string>

#include "recorder/recorder_core.h"

namespace ccomidi {

struct SmfWriteOptions {
  std::uint16_t ppq = 480;
  double fallbackBpm = 120.0;
};

bool write_smf1(const std::string &path,
                const RecorderCore::Snapshot &snapshot,
                const SmfWriteOptions &options);

} // namespace ccomidi

#endif
