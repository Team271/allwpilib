// Copyright (c) FIRST and other WPILib contributors.
// Open Source Software; you can modify and/or share it under the terms of
// the WPILib BSD license file in the root directory of this project.

#include <stdint.h>

#include <concepts>
#include <span>
#include <string_view>
#include <vector>

#include "../MockLogger.h"
#include "../PubSubOptionsMatcher.h"
#include "../SpanMatcher.h"
#include "../TestPrinters.h"
#include "../ValueMatcher.h"
#include "Handle.h"
#include "MockNetworkInterface.h"
#include "MockWireConnection.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "net/Message.h"
#include "net/ServerImpl.h"
#include "net/WireEncoder.h"
#include "ntcore_c.h"
#include "ntcore_cpp.h"

using ::testing::_;
using ::testing::AllOf;
using ::testing::ElementsAre;
using ::testing::Field;
using ::testing::IsEmpty;
using ::testing::Property;
using ::testing::Return;

using MockSetPeriodicFunc = ::testing::MockFunction<void(uint32_t repeatMs)>;
using MockConnected3Func =
    ::testing::MockFunction<void(std::string_view name, uint16_t proto)>;

namespace nt {

class ServerImplTest : public ::testing::Test {
 public:
  ::testing::StrictMock<net::MockLocalInterface> local;
  wpi::MockLogger logger;
  net::ServerImpl server{logger};
};

TEST_F(ServerImplTest, AddClient) {
  ::testing::StrictMock<net::MockWireConnection> wire;
  EXPECT_CALL(wire, Flush());
  MockSetPeriodicFunc setPeriodic;
  auto [name, id] = server.AddClient("test", "connInfo", false, wire,
                                     setPeriodic.AsStdFunction());
  EXPECT_EQ(name, "test@1");
  EXPECT_NE(id, -1);
}

TEST_F(ServerImplTest, AddDuplicateClient) {
  ::testing::StrictMock<net::MockWireConnection> wire1;
  ::testing::StrictMock<net::MockWireConnection> wire2;
  MockSetPeriodicFunc setPeriodic1;
  MockSetPeriodicFunc setPeriodic2;
  EXPECT_CALL(wire1, Flush());
  EXPECT_CALL(wire2, Flush());

  auto [name1, id1] = server.AddClient("test", "connInfo", false, wire1,
                                       setPeriodic1.AsStdFunction());
  auto [name2, id2] = server.AddClient("test", "connInfo2", false, wire2,
                                       setPeriodic2.AsStdFunction());
  EXPECT_EQ(name1, "test@1");
  EXPECT_NE(id1, -1);
  EXPECT_EQ(name2, "test@2");
  EXPECT_NE(id1, id2);
  EXPECT_NE(id2, -1);
}

TEST_F(ServerImplTest, AddClient3) {}

template <typename T>
static std::string EncodeText(const T& msgs) {
  std::string data;
  wpi::raw_string_ostream os{data};
  bool first = true;
  for (auto&& msg : msgs) {
    if (first) {
      os << '[';
      first = false;
    } else {
      os << ',';
    }
    net::WireEncodeText(os, msg);
  }
  os << ']';
  return data;
}

template <typename T>
static std::vector<uint8_t> EncodeServerBinary(const T& msgs) {
  std::vector<uint8_t> data;
  wpi::raw_uvector_ostream os{data};
  for (auto&& msg : msgs) {
    if constexpr (std::same_as<typename T::value_type, net::ServerMessage>) {
      if (auto m = std::get_if<net::ServerValueMsg>(&msg.contents)) {
        net::WireEncodeBinary(os, m->topic, m->value.time(), m->value);
      }
    } else if constexpr (std::same_as<typename T::value_type,
                                      net::ClientMessage>) {
      if (auto m = std::get_if<net::ClientValueMsg>(&msg.contents)) {
        net::WireEncodeBinary(os, Handle{m->pubHandle}.GetIndex(),
                              m->value.time(), m->value);
      }
    }
  }
  return data;
}

TEST_F(ServerImplTest, PublishLocal) {
  // publish before client connect
  server.SetLocal(&local);
  NT_Publisher pubHandle = nt::Handle{0, 1, nt::Handle::kPublisher};
  NT_Topic topicHandle = nt::Handle{0, 1, nt::Handle::kTopic};
  NT_Publisher pubHandle2 = nt::Handle{0, 2, nt::Handle::kPublisher};
  NT_Topic topicHandle2 = nt::Handle{0, 2, nt::Handle::kTopic};
  NT_Publisher pubHandle3 = nt::Handle{0, 3, nt::Handle::kPublisher};
  NT_Topic topicHandle3 = nt::Handle{0, 3, nt::Handle::kTopic};
  {
    ::testing::InSequence seq;
    EXPECT_CALL(local, NetworkAnnounce("test", "double", wpi::json::object(),
                                       pubHandle));
    EXPECT_CALL(local, NetworkAnnounce("test2", "double", wpi::json::object(),
                                       pubHandle2));
    EXPECT_CALL(local, NetworkAnnounce("test3", "double", wpi::json::object(),
                                       pubHandle3));
  }

  {
    std::vector<net::ClientMessage> msgs;
    msgs.emplace_back(net::ClientMessage{net::PublishMsg{
        pubHandle, topicHandle, "test", "double", wpi::json::object(), {}}});
    server.HandleLocal(msgs);
  }

  // client connect; it should get already-published topic as soon as it
  // subscribes
  ::testing::StrictMock<net::MockWireConnection> wire;
  MockSetPeriodicFunc setPeriodic;
  {
    ::testing::InSequence seq;
    EXPECT_CALL(wire, Flush());                         // AddClient()
    EXPECT_CALL(setPeriodic, Call(100));                // ClientSubscribe()
    EXPECT_CALL(wire, Flush());                         // ClientSubscribe()
    EXPECT_CALL(wire, Ready()).WillOnce(Return(true));  // SendControl()
    {
      std::vector<net::ServerMessage> smsgs;
      smsgs.emplace_back(net::ServerMessage{net::AnnounceMsg{
          "test", 3, "double", std::nullopt, wpi::json::object()}});
      smsgs.emplace_back(net::ServerMessage{net::AnnounceMsg{
          "test2", 8, "double", std::nullopt, wpi::json::object()}});
      EXPECT_CALL(wire, Text(EncodeText(smsgs)));  // SendControl()
    }
    EXPECT_CALL(wire, Flush());                         // SendControl()
    EXPECT_CALL(wire, Ready()).WillOnce(Return(true));  // SendControl()
    {
      std::vector<net::ServerMessage> smsgs;
      smsgs.emplace_back(net::ServerMessage{net::AnnounceMsg{
          "test3", 11, "double", std::nullopt, wpi::json::object()}});
      EXPECT_CALL(wire, Text(EncodeText(smsgs)));  // SendControl()
    }
    EXPECT_CALL(wire, Flush());  // SendControl()
  }
  auto [name, id] = server.AddClient("test", "connInfo", false, wire,
                                     setPeriodic.AsStdFunction());

  {
    NT_Subscriber subHandle = nt::Handle{0, 1, nt::Handle::kSubscriber};
    std::vector<net::ClientMessage> msgs;
    msgs.emplace_back(net::ClientMessage{net::SubscribeMsg{
        subHandle, {{""}}, PubSubOptions{.prefixMatch = true}}});
    server.ProcessIncomingText(id, EncodeText(msgs));
  }

  // publish before send control
  {
    std::vector<net::ClientMessage> msgs;
    msgs.emplace_back(net::ClientMessage{net::PublishMsg{
        pubHandle2, topicHandle2, "test2", "double", wpi::json::object(), {}}});
    server.HandleLocal(msgs);
  }

  server.SendControl(100);

  // publish after send control
  {
    std::vector<net::ClientMessage> msgs;
    msgs.emplace_back(net::ClientMessage{net::PublishMsg{
        pubHandle3, topicHandle3, "test3", "double", wpi::json::object(), {}}});
    server.HandleLocal(msgs);
  }

  server.SendControl(200);
}

TEST_F(ServerImplTest, ClientSubTopicOnlyThenValue) {
  // publish before client connect
  server.SetLocal(&local);
  NT_Publisher pubHandle = nt::Handle{0, 1, nt::Handle::kPublisher};
  NT_Topic topicHandle = nt::Handle{0, 1, nt::Handle::kTopic};
  EXPECT_CALL(
      local, NetworkAnnounce("test", "double", wpi::json::object(), pubHandle));

  {
    std::vector<net::ClientMessage> msgs;
    msgs.emplace_back(net::ClientMessage{net::PublishMsg{
        pubHandle, topicHandle, "test", "double", wpi::json::object(), {}}});
    msgs.emplace_back(net::ClientMessage{
        net::ClientValueMsg{pubHandle, Value::MakeDouble(1.0, 10)}});
    server.HandleLocal(msgs);
  }

  ::testing::StrictMock<net::MockWireConnection> wire;
  MockSetPeriodicFunc setPeriodic;
  {
    ::testing::InSequence seq;
    EXPECT_CALL(wire, Flush());                         // AddClient()
    EXPECT_CALL(setPeriodic, Call(100));                // ClientSubscribe()
    EXPECT_CALL(wire, Flush());                         // ClientSubscribe()
    EXPECT_CALL(wire, Ready()).WillOnce(Return(true));  // SendValues()
    {
      std::vector<net::ServerMessage> smsgs;
      smsgs.emplace_back(net::ServerMessage{net::AnnounceMsg{
          "test", 3, "double", std::nullopt, wpi::json::object()}});
      EXPECT_CALL(wire, Text(EncodeText(smsgs)));  // SendValues()
    }
    EXPECT_CALL(wire, Flush());                         // SendValues()
    EXPECT_CALL(setPeriodic, Call(100));                // ClientSubscribe()
    EXPECT_CALL(wire, Flush());                         // ClientSubscribe()
    EXPECT_CALL(wire, Ready()).WillOnce(Return(true));  // SendValues()
    {
      std::vector<net::ServerMessage> smsgs;
      smsgs.emplace_back(net::ServerMessage{
          net::ServerValueMsg{3, Value::MakeDouble(1.0, 10)}});
      EXPECT_CALL(
          wire,
          Binary(wpi::SpanEq(EncodeServerBinary(smsgs))));  // SendValues()
    }
    EXPECT_CALL(wire, Flush());  // SendValues()
  }

  // connect client
  auto [name, id] = server.AddClient("test", "connInfo", false, wire,
                                     setPeriodic.AsStdFunction());

  // subscribe topics only; will not send value
  {
    NT_Subscriber subHandle = nt::Handle{0, 1, nt::Handle::kSubscriber};
    std::vector<net::ClientMessage> msgs;
    msgs.emplace_back(net::ClientMessage{net::SubscribeMsg{
        subHandle,
        {{""}},
        PubSubOptions{.topicsOnly = true, .prefixMatch = true}}});
    server.ProcessIncomingText(id, EncodeText(msgs));
  }

  server.SendValues(id, 100);

  // subscribe normal; will not resend announcement, but will send value
  {
    NT_Subscriber subHandle = nt::Handle{0, 2, nt::Handle::kSubscriber};
    std::vector<net::ClientMessage> msgs;
    msgs.emplace_back(net::ClientMessage{
        net::SubscribeMsg{subHandle, {{"test"}}, PubSubOptions{}}});
    server.ProcessIncomingText(id, EncodeText(msgs));
  }

  server.SendValues(id, 200);
}

TEST_F(ServerImplTest, ZeroTimestampNegativeTime) {
  // publish before client connect
  server.SetLocal(&local);
  NT_Publisher pubHandle = nt::Handle{0, 1, nt::Handle::kPublisher};
  NT_Topic topicHandle = nt::Handle{0, 1, nt::Handle::kTopic};
  NT_Subscriber subHandle = nt::Handle{0, 1, nt::Handle::kSubscriber};
  Value defaultValue = Value::MakeDouble(1.0, 10);
  defaultValue.SetTime(0);
  defaultValue.SetServerTime(0);
  Value value = Value::MakeDouble(5, -10);
  {
    ::testing::InSequence seq;
    EXPECT_CALL(local, NetworkAnnounce("test", "double", wpi::json::object(),
                                       pubHandle))
        .WillOnce(Return(topicHandle));
    EXPECT_CALL(local, NetworkSetValue(topicHandle, defaultValue));
    EXPECT_CALL(local, NetworkSetValue(topicHandle, value));
  }

  {
    std::vector<net::ClientMessage> msgs;
    msgs.emplace_back(net::ClientMessage{net::PublishMsg{
        pubHandle, topicHandle, "test", "double", wpi::json::object(), {}}});
    msgs.emplace_back(
        net::ClientMessage{net::ClientValueMsg{pubHandle, defaultValue}});
    msgs.emplace_back(
        net::ClientMessage{net::SubscribeMsg{subHandle, {"test"}, {}}});
    server.HandleLocal(msgs);
  }

  // client connect; it should get already-published topic as soon as it
  // subscribes
  ::testing::StrictMock<net::MockWireConnection> wire;
  MockSetPeriodicFunc setPeriodic;
  {
    ::testing::InSequence seq;
    EXPECT_CALL(wire, Flush());  // AddClient()
  }
  auto [name, id] = server.AddClient("test", "connInfo", false, wire,
                                     setPeriodic.AsStdFunction());

  // publish and send non-default value with negative time offset
  {
    NT_Subscriber pubHandle2 = nt::Handle{0, 2, nt::Handle::kPublisher};
    std::vector<net::ClientMessage> msgs;
    msgs.emplace_back(net::ClientMessage{net::PublishMsg{
        pubHandle2, topicHandle, "test", "double", wpi::json::object(), {}}});
    server.ProcessIncomingText(id, EncodeText(msgs));
    msgs.clear();
    msgs.emplace_back(
        net::ClientMessage{net::ClientValueMsg{pubHandle2, value}});
    server.ProcessIncomingBinary(id, EncodeServerBinary(msgs));
  }
}

}  // namespace nt
