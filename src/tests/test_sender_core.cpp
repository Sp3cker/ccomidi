#include "core/sender_core.h"

#include <cstdio>
#include <cstdlib>

using ccomidi::AutomationEvent;
using ccomidi::CommandType;
using ccomidi::ParamAddress;
using ccomidi::ParamKind;
using ccomidi::PlannedEvents;
using ccomidi::SenderCore;
using ccomidi::TransportState;

namespace {

constexpr std::size_t kVolumeRow = 0;
constexpr std::size_t kPanRow = 1;
constexpr std::size_t kModRow = 2;
constexpr std::size_t kLfoSpeedRow = 3;
constexpr std::size_t kFirstConfigurableRow = ccomidi::kFixedCommandRowCount;

int g_testsRun = 0;
int g_testsPassed = 0;

#define ASSERT_TRUE(cond, msg)                                                 \
  do {                                                                         \
    ++g_testsRun;                                                              \
    if (!(cond)) {                                                             \
      std::fprintf(stderr, "FAIL: %s (line %d)\n", msg, __LINE__);             \
    } else {                                                                   \
      ++g_testsPassed;                                                         \
    }                                                                          \
  } while (0)

#define ASSERT_EQ(actual, expected, msg)                                       \
  do {                                                                         \
    ++g_testsRun;                                                              \
    if ((actual) != (expected)) {                                              \
      std::fprintf(stderr, "FAIL: %s: expected %d, got %d (line %d)\n", msg,   \
                   static_cast<int>(expected), static_cast<int>(actual),       \
                   __LINE__);                                                  \
    } else {                                                                   \
      ++g_testsPassed;                                                         \
    }                                                                          \
  } while (0)

AutomationEvent make_event(std::uint32_t time, ParamKind kind, std::uint8_t row,
                           double value) {
  AutomationEvent event = {};
  event.time = time;
  event.address = ParamAddress{kind, row};
  event.value = value;
  return event;
}

void configure_volume_row(SenderCore *core) {
  core->set_output_channel(2.0);
  core->set_row_enabled(kVolumeRow, 1.0);
  core->set_row_value(kVolumeRow, 0, 100.0);
}

void test_start_of_playback_emits_snapshot() {
  SenderCore core;
  PlannedEvents planned;
  configure_volume_row(&core);

  core.process_block(TransportState{true}, nullptr, 0, &planned);

  ASSERT_EQ(planned.count, 1, "playback start emits one direct CC");
  ASSERT_EQ(planned.events[0].time, 0, "playback snapshot is at time zero");
  ASSERT_EQ(planned.events[0].status, 0xB2,
            "configured channel overrides outgoing MIDI channel");
  ASSERT_EQ(planned.events[0].data1, 0x07, "volume row emits CC 7");
  ASSERT_EQ(planned.events[0].data2, 100, "volume row emits configured value");
}

void test_fixed_rows_emit_expected_controllers() {
  SenderCore core;
  PlannedEvents planned;
  core.set_row_enabled(kPanRow, 1.0);
  core.set_row_value(kPanRow, 0, 64.0);
  core.set_row_enabled(kModRow, 1.0);
  core.set_row_value(kModRow, 0, 17.0);
  core.set_row_enabled(kLfoSpeedRow, 1.0);
  core.set_row_value(kLfoSpeedRow, 0, 88.0);

  core.process_block(TransportState{true}, nullptr, 0, &planned);

  ASSERT_EQ(planned.count, 3, "fixed command rows emit without type selection");
  ASSERT_EQ(planned.events[0].data1, 0x0A, "pan row always emits CC 10");
  ASSERT_EQ(planned.events[1].data1, 0x01, "mod row always emits CC 1");
  ASSERT_EQ(planned.events[2].data1, 0x15, "lfo speed row always emits CC 21");
}

void test_floor_quantization_deduplicates_automation() {
  SenderCore core;
  PlannedEvents planned;
  configure_volume_row(&core);

  core.process_block(TransportState{true}, nullptr, 0, &planned);

  AutomationEvent events[] = {
      make_event(0, ParamKind::RowValue0, kVolumeRow, 27.1),
      make_event(1, ParamKind::RowValue0, kVolumeRow, 27.2),
      make_event(2, ParamKind::RowValue0, kVolumeRow, 27.3),
      make_event(3, ParamKind::RowValue0, kVolumeRow, 27.7),
      make_event(4, ParamKind::RowValue0, kVolumeRow, 28.0),
  };

  core.process_block(TransportState{true}, events,
                     sizeof(events) / sizeof(events[0]), &planned);

  ASSERT_EQ(planned.count, 2, "only quantized changes are emitted");
  ASSERT_EQ(planned.events[0].time, 0,
            "first changed value emits at its event time");
  ASSERT_EQ(planned.events[0].data2, 27, "value is floored before emission");
  ASSERT_EQ(planned.events[1].time, 4,
            "next quantized change emits when the integer changes");
  ASSERT_EQ(planned.events[1].data2, 28, "changed integer is emitted");
}

void test_channel_change_resends_snapshot() {
  SenderCore core;
  PlannedEvents planned;
  configure_volume_row(&core);
  core.set_output_channel(5.0);
  core.set_row_enabled(kVolumeRow, 1.0);
  core.set_row_value(kVolumeRow, 0, 64.0);
  core.set_output_channel(2.0);

  core.process_block(TransportState{true}, nullptr, 0, &planned);

  AutomationEvent event =
      make_event(12, ParamKind::OutputChannel, kVolumeRow, 5.9);
  core.process_block(TransportState{true}, &event, 1, &planned);

  ASSERT_EQ(planned.count, 1, "channel change resends snapshot");
  ASSERT_EQ(planned.events[0].time, 12, "channel resend preserves event time");
  ASSERT_EQ(planned.events[0].status, 0xB5,
            "resend uses the new floored output channel");
  ASSERT_EQ(planned.events[0].data2, 64,
            "resend carries the selected channel bank value");
}

void test_compound_xcmd_row_reemits_full_sequence() {
  SenderCore core;
  PlannedEvents planned;
  core.set_output_channel(3.0);
  core.set_row_enabled(kFirstConfigurableRow, 1.0);
  core.set_row_type(kFirstConfigurableRow,
                    static_cast<double>(CommandType::XcmdIecv));
  core.set_row_value(kFirstConfigurableRow, 0, 22.0);

  core.process_block(TransportState{true}, nullptr, 0, &planned);
  ASSERT_EQ(planned.count, 2, "xcmd snapshot emits selector and data");
  ASSERT_EQ(planned.events[0].data1, 0x1E,
            "xcmd selector controller emitted first");
  ASSERT_EQ(planned.events[0].data2, 0x08, "xcmd IECV selector value emitted");
  ASSERT_EQ(planned.events[1].data1, 0x1D,
            "xcmd data controller emitted second");
  ASSERT_EQ(planned.events[1].data2, 22, "xcmd data value emitted");

  AutomationEvent event =
      make_event(8, ParamKind::RowValue0, kFirstConfigurableRow, 23.9);
  core.process_block(TransportState{true}, &event, 1, &planned);

  ASSERT_EQ(planned.count, 2, "compound row change reemits the full sequence");
  ASSERT_EQ(planned.events[0].data1, 0x1E, "compound resend includes selector");
  ASSERT_EQ(planned.events[1].data1, 0x1D, "compound resend includes value");
  ASSERT_EQ(planned.events[1].data2, 23, "compound resend uses floored value");
}

void test_stopped_automation_is_applied_on_next_start() {
  SenderCore core;
  PlannedEvents planned;
  configure_volume_row(&core);

  core.process_block(TransportState{false}, nullptr, 0, &planned);
  ASSERT_EQ(planned.count, 0, "stopped transport emits nothing");

  AutomationEvent event =
      make_event(0, ParamKind::RowValue0, kVolumeRow, 43.8);
  core.process_block(TransportState{false}, &event, 1, &planned);
  ASSERT_EQ(planned.count, 0, "automation while stopped does not emit");

  core.process_block(TransportState{true}, nullptr, 0, &planned);
  ASSERT_EQ(planned.count, 1,
            "next playback start emits cached automated value");
  ASSERT_EQ(planned.events[0].data2, 43,
            "playback start uses current automated state");
}

void test_memacc_reemits_full_canonical_sequence() {
  SenderCore core;
  PlannedEvents planned;
  core.set_output_channel(1.0);
  core.set_row_enabled(kFirstConfigurableRow, 1.0);
  core.set_row_type(kFirstConfigurableRow,
                    static_cast<double>(CommandType::MemAcc0C));
  core.set_row_value(kFirstConfigurableRow, 0, 1.0);
  core.set_row_value(kFirstConfigurableRow, 1, 16.0);
  core.set_row_value(kFirstConfigurableRow, 2, 32.0);
  core.set_row_value(kFirstConfigurableRow, 3, 64.0);

  core.process_block(TransportState{true}, nullptr, 0, &planned);
  ASSERT_EQ(planned.count, 4, "memacc emits full setup sequence");
  ASSERT_EQ(planned.events[0].data1, 0x0D, "memacc op byte emitted");
  ASSERT_EQ(planned.events[1].data1, 0x0E, "memacc param1 byte emitted");
  ASSERT_EQ(planned.events[2].data1, 0x0F, "memacc param2 byte emitted");
  ASSERT_EQ(planned.events[3].data1, 0x0C, "memacc trigger byte emitted");

  AutomationEvent event =
      make_event(9, ParamKind::RowValue3, kFirstConfigurableRow, 80.9);
  core.process_block(TransportState{true}, &event, 1, &planned);
  ASSERT_EQ(planned.count, 4, "memacc change reemits all setup bytes");
  ASSERT_EQ(planned.events[3].data2, 80, "memacc trigger value is floored");
}

void test_runtime_reset_preserves_parameter_values() {
  SenderCore core;
  PlannedEvents planned;
  configure_volume_row(&core);

  core.process_block(TransportState{true}, nullptr, 0, &planned);
  core.reset_runtime_state();
  core.process_block(TransportState{true}, nullptr, 0, &planned);

  ASSERT_EQ(planned.count, 1, "runtime reset rearms playback snapshot");
  ASSERT_EQ(planned.events[0].data2, 100,
            "runtime reset preserves parameter values");
}

void test_preapplied_channel_change_resends_snapshot() {
  SenderCore core;
  PlannedEvents planned;
  configure_volume_row(&core);
  core.set_output_channel(7.0);
  core.set_row_enabled(kVolumeRow, 1.0);
  core.set_row_value(kVolumeRow, 0, 45.0);
  core.set_output_channel(2.0);

  core.process_block(TransportState{true}, nullptr, 0, &planned);

  std::array<bool, ccomidi::kMaxCommandRows> rowChanged = {};
  bool channelChanged = false;
  core.apply_parameter_change(
      make_event(0, ParamKind::OutputChannel, kVolumeRow, 7.2),
      &channelChanged, &rowChanged);
  core.emit_preapplied_changes(true, channelChanged, rowChanged, 0, &planned);

  ASSERT_EQ(planned.count, 1, "preapplied channel change resends snapshot");
  ASSERT_EQ(planned.events[0].status, 0xB7,
            "preapplied resend uses new output channel");
  ASSERT_EQ(planned.events[0].data2, 45,
            "preapplied resend carries selected channel bank value");
}

void test_fixed_rows_reject_type_changes() {
  SenderCore core;
  std::array<bool, ccomidi::kMaxCommandRows> rowChanged = {};
  bool channelChanged = false;

  core.set_row_type(kVolumeRow, static_cast<double>(CommandType::Tune));
  ASSERT_EQ(static_cast<int>(core.row_type(kVolumeRow)),
            static_cast<int>(CommandType::Volume),
            "fixed volume row ignores direct type changes");

  const bool changed = core.apply_parameter_change(
      make_event(0, ParamKind::RowType, kVolumeRow,
                 static_cast<double>(CommandType::MemAcc0C)),
      &channelChanged, &rowChanged);
  ASSERT_TRUE(!changed, "fixed row type automation is ignored");
  ASSERT_TRUE(!channelChanged, "fixed row type automation is not a channel change");
  ASSERT_TRUE(!rowChanged[kVolumeRow], "fixed row type automation does not mark row dirty");
}

void test_channel_switch_keeps_single_command_bank() {
  SenderCore core;
  PlannedEvents planned;
  configure_volume_row(&core);

  core.set_output_channel(0.0);
  ASSERT_EQ(static_cast<int>(core.row_value_raw(kVolumeRow, 0)), 100,
            "changing output channel does not change stored volume value");

  core.process_block(TransportState{true}, nullptr, 0, &planned);
  ASSERT_EQ(planned.count, 1, "playback snapshot emits active single bank");
  ASSERT_EQ(planned.events[0].status, 0xB0,
            "snapshot uses current output channel");
  ASSERT_EQ(planned.events[0].data2, 100,
            "snapshot keeps the configured single-bank value");

  AutomationEvent switchEvent =
      make_event(5, ParamKind::OutputChannel, kVolumeRow, 1.0);
  core.process_block(TransportState{true}, &switchEvent, 1, &planned);
  ASSERT_EQ(planned.count, 1, "channel switch resends the same single-bank snapshot");
  ASSERT_EQ(planned.events[0].status, 0xB1,
            "switch resend uses the new output channel");
  ASSERT_EQ(planned.events[0].data2, 100,
            "switch resend preserves the configured value");
}

void test_configurable_rows_reject_fixed_command_types() {
  SenderCore core;

  core.set_row_type(kFirstConfigurableRow, static_cast<double>(CommandType::Pan));
  ASSERT_EQ(static_cast<int>(core.row_type(kFirstConfigurableRow)),
            static_cast<int>(CommandType::None),
            "configurable rows cannot be reassigned to fixed commands");
}

} // namespace

int main() {
  test_start_of_playback_emits_snapshot();
  test_fixed_rows_emit_expected_controllers();
  test_floor_quantization_deduplicates_automation();
  test_channel_change_resends_snapshot();
  test_compound_xcmd_row_reemits_full_sequence();
  test_stopped_automation_is_applied_on_next_start();
  test_memacc_reemits_full_canonical_sequence();
  test_runtime_reset_preserves_parameter_values();
  test_preapplied_channel_change_resends_snapshot();
  test_fixed_rows_reject_type_changes();
  test_channel_switch_keeps_single_command_bank();
  test_configurable_rows_reject_fixed_command_types();

  std::printf("Passed %d/%d tests\n", g_testsPassed, g_testsRun);
  return g_testsPassed == g_testsRun ? EXIT_SUCCESS : EXIT_FAILURE;
}
