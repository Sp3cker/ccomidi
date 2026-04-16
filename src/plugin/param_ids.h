#ifndef CCOMIDI_PARAM_IDS_H
#define CCOMIDI_PARAM_IDS_H

#include <cstdint>

#include <clap/id.h>

#include "core/sender_core.h"

namespace ccomidi {

constexpr clap_id kParamOutputChannel = 0;
constexpr std::uint32_t kParamsPerRow = 6;
constexpr clap_id kFirstRowParam = 1;

enum class RowParamSlot : std::uint32_t {
  Enabled = 0,
  Type = 1,
  Value0 = 2,
  Value1 = 3,
  Value2 = 4,
  Value3 = 5,
};

constexpr clap_id row_param_id(std::uint32_t row, RowParamSlot slot) {
  return kFirstRowParam +
         static_cast<clap_id>(row * kParamsPerRow +
                              static_cast<std::uint32_t>(slot));
}

constexpr std::uint32_t total_param_count() {
  return 1 + static_cast<std::uint32_t>(kMaxCommandRows) * kParamsPerRow;
}
// ID is the index of the automation.
// Its divided by kParamsPerRow to know which row/command its for
// Modulo gives it's index inside that row
inline bool decode_param_id(clap_id id, ParamAddress *address) {
  if (!address)
    return false;

  if (id == kParamOutputChannel) {
    address->kind = ParamKind::OutputChannel;
    address->row = 0;
    return true;
  }

  if (id < kFirstRowParam)
    return false;

  const clap_id adjusted = id - kFirstRowParam;
  const std::uint32_t row =
      static_cast<std::uint32_t>(adjusted / kParamsPerRow);
  const std::uint32_t slot =
      static_cast<std::uint32_t>(adjusted % kParamsPerRow);
  if (row >= kMaxCommandRows)
    return false;

  address->row = static_cast<std::uint8_t>(row);
  switch (static_cast<RowParamSlot>(slot)) {
  case RowParamSlot::Enabled:
    address->kind = ParamKind::RowEnabled;
    return true;
  case RowParamSlot::Type:
    address->kind = ParamKind::RowType;
    return true;
  case RowParamSlot::Value0:
    address->kind = ParamKind::RowValue0;
    return true;
  case RowParamSlot::Value1:
    address->kind = ParamKind::RowValue1;
    return true;
  case RowParamSlot::Value2:
    address->kind = ParamKind::RowValue2;
    return true;
  case RowParamSlot::Value3:
    address->kind = ParamKind::RowValue3;
    return true;
  }

  return false;
}

} // namespace ccomidi

#endif
