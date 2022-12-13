#include <gmock/gmock-matchers.h>
#include <gtest/gtest.h>

#include "emu/event_manager.h"

namespace cyder {
namespace {

// Link: http://0.0.0.0:8000/docs/mac/Toolbox/Toolbox-38.html#MARKER-9-112
constexpr uint16_t kEventEventMask = 0xFFFF;

constexpr Ptr kFakeWindowPtr = 0xDEADBEEF;

}  // namespace

TEST(EventManagerTests, NoEvents) {
  EventManager event_manager;

  EXPECT_EQ(event_manager.GetNextEvent(kEventEventMask).what, kNullEvent);
}

TEST(EventManagerTests, Priority) {
  EventManager event_manager;

  event_manager.QueueWindowUpdate(kFakeWindowPtr);
  event_manager.QueueMouseDown(0, 0);
  event_manager.QueueWindowActivate(kFakeWindowPtr);
  event_manager.QueueKeyDown();

  EXPECT_EQ(event_manager.GetNextEvent(kEventEventMask).what, kWindowActivate);
  // Mouse up/down and key up/down are returned in FIFO ordering:
  EXPECT_EQ(event_manager.GetNextEvent(kEventEventMask).what, kMouseDown);
  EXPECT_EQ(event_manager.GetNextEvent(kEventEventMask).what, kKeyDown);
  EXPECT_EQ(event_manager.GetNextEvent(kEventEventMask).what, kWindowUpdate);
  EXPECT_EQ(event_manager.GetNextEvent(kEventEventMask).what, kNullEvent);
}

TEST(EventManagerTests, InputIsFifo) {
  EventManager event_manager;

  event_manager.QueueMouseDown(0, 0);
  event_manager.QueueMouseDown(2, 0);
  event_manager.QueueMouseDown(4, 0);
  event_manager.QueueMouseDown(3, 0);
  event_manager.QueueMouseDown(5, 0);

  EXPECT_EQ(event_manager.GetNextEvent(kEventEventMask).where.x, 0);
  EXPECT_EQ(event_manager.GetNextEvent(kEventEventMask).where.x, 2);
  EXPECT_EQ(event_manager.GetNextEvent(kEventEventMask).where.x, 4);
  EXPECT_EQ(event_manager.GetNextEvent(kEventEventMask).where.x, 3);
  EXPECT_EQ(event_manager.GetNextEvent(kEventEventMask).where.x, 5);
  EXPECT_EQ(event_manager.GetNextEvent(kEventEventMask).what, kNullEvent);
}

TEST(EventManagerTests, MaskEvents) {
  EventManager event_manager;

  event_manager.QueueKeyDown();
  event_manager.QueueMouseDown(0, 0);
  event_manager.QueueKeyDown();

  EXPECT_EQ(event_manager.GetNextEvent(1 << kMouseDown).what, kMouseDown);
  // Events not selected should not be removed:
  EXPECT_EQ(event_manager.GetNextEvent(kEventEventMask).what, kKeyDown);
  EXPECT_EQ(event_manager.GetNextEvent(kEventEventMask).what, kKeyDown);
  EXPECT_EQ(event_manager.GetNextEvent(kEventEventMask).what, kNullEvent);
}

TEST(EventManagerTests, HasMouseEvent) {
  EventManager event_manager;

  event_manager.QueueKeyDown();
  event_manager.QueueMouseDown(0, 0);

  EXPECT_FALSE(event_manager.HasMouseEvent(kMouseUp));
  EXPECT_TRUE(event_manager.HasMouseEvent(kMouseDown));

  event_manager.QueueMouseUp(0, 0);
  EXPECT_TRUE(event_manager.HasMouseEvent(kMouseUp));
}

}  // namespace cyder
